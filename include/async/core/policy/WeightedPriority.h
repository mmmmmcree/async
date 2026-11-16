#pragma once

#include <async/core/policy/RoundRobin.h>

#include <array>

LCF_ASYNC_EXPORT
namespace lcf::async::policy {

// Rotate the first candidate with weights 4:2:1; still inspect every band.
class WeightedPriority
{
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
        if (count == 0) { return {std::move(candidates), 0, 0}; }
        constexpr std::array<std::uint64_t, 7> order {0, 0, 1, 0, 1, 0, 2};
        const std::uint64_t first = order[m_next] % count;
        m_next = (m_next + 1) % order.size();
        return {std::move(candidates), first, count};
    }
private:
    std::size_t m_next = 0;
};

} // namespace lcf::async::policy
