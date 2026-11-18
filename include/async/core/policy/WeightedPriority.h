#pragma once

#include <async/core/policy/RoundRobin.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async::policy {

// Weights are ordered by candidate position and control how often each is tried first.
template <std::size_t... Weights>
requires (sizeof...(Weights) > 0) and ((Weights > 0) and ...)
class WeightedPriority
{
    inline static constexpr std::array<std::size_t, sizeof...(Weights)> k_weights {Weights...};
    inline static constexpr std::size_t k_max_weight = std::ranges::max(k_weights);
    template <typename Candidates>
    using Selection = details::RoundRobinSelectionView<Candidates>;
public:
    ~WeightedPriority() noexcept = default;
    WeightedPriority() noexcept = default;
    WeightedPriority(const WeightedPriority &) noexcept = default;
    WeightedPriority(WeightedPriority &&) noexcept = default;
    WeightedPriority & operator=(const WeightedPriority &) noexcept = default;
    WeightedPriority & operator=(WeightedPriority &&) noexcept = default;
public:
    template <std::ranges::view Candidates>
    requires std::ranges::forward_range<Candidates>
    Selection<Candidates> select(Candidates candidates) noexcept
    {
        const std::uint64_t count = std::ranges::distance(candidates);
        if (count != k_weights.size()) { return m_unweighted.select(std::move(candidates)); }
        return {std::move(candidates), this->getNextWeightedIndex(), count};
    }
private:
    std::size_t getNextWeightedIndex() noexcept
    {
        // Each round visits every candidate whose weight exceeds the round number.
        while (true) {
            const std::size_t index = m_next_index;
            const bool eligible = m_round < k_weights[index];
            if (++m_next_index == k_weights.size()) {
                m_next_index = 0;
                if (++m_round == k_max_weight) { m_round = 0; }
            }
            if (eligible) { return index; }
        }
    }
private:
    RoundRobin m_unweighted;
    std::size_t m_next_index = 0;
    std::size_t m_round = 0;
};

} // namespace lcf::async::policy
