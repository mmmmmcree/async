#pragma once

#include <async/core/scheduling/policy/declarations/concepts.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async::policy {

namespace details {

template <std::ranges::view Candidates>
requires std::ranges::forward_range<Candidates>
class RoundRobinSelectionView : public std::ranges::view_interface<RoundRobinSelectionView<Candidates>>
{
    using CandidateIterator = std::ranges::iterator_t<Candidates>;
    using CandidateSentinel = std::ranges::sentinel_t<Candidates>;
    class Iterator
    {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::ranges::range_value_t<Candidates>;
        using difference_type = std::ranges::range_difference_t<Candidates>;
    public:
        ~Iterator() noexcept = default;
        Iterator() noexcept = default;
        Iterator(CandidateIterator first, CandidateIterator current, CandidateSentinel last,
            std::uint64_t candidate_count) noexcept :
            m_first(first), m_current(current), m_last(last), m_remaining_count(candidate_count) {}
        Iterator(const Iterator &) noexcept = default;
        Iterator(Iterator &&) noexcept = default;
        Iterator & operator=(const Iterator &) noexcept = default;
        Iterator & operator=(Iterator &&) noexcept = default;
    public:
        std::ranges::range_reference_t<Candidates> operator*() const noexcept { return *m_current; }
        Iterator & operator++() noexcept
        {
            --m_remaining_count;
            if (++m_current == m_last) { m_current = m_first; }
            return *this;
        }
        Iterator operator++(int) noexcept
        {
            const Iterator previous {*this};
            ++*this;
            return previous;
        }
        bool operator==(const Iterator & other) const noexcept
        {
            return m_current == other.m_current and m_remaining_count == other.m_remaining_count;
        }
        bool operator==(std::default_sentinel_t) const noexcept { return m_remaining_count == 0; }
    private:
        CandidateIterator m_first {};
        CandidateIterator m_current {};
        CandidateSentinel m_last {};
        std::uint64_t m_remaining_count = 0;
    };
public:
    ~RoundRobinSelectionView() noexcept = default;
    RoundRobinSelectionView() noexcept = default;
    RoundRobinSelectionView(Candidates candidates, std::uint64_t start_index, std::uint64_t candidate_count) noexcept :
        m_candidates(std::move(candidates)), m_start_index(start_index), m_candidate_count(candidate_count) {}
    RoundRobinSelectionView(const RoundRobinSelectionView &) noexcept = default;
    RoundRobinSelectionView(RoundRobinSelectionView &&) noexcept = default;
    RoundRobinSelectionView & operator=(const RoundRobinSelectionView &) noexcept = default;
    RoundRobinSelectionView & operator=(RoundRobinSelectionView &&) noexcept = default;
public:
    Iterator begin() noexcept
    {
        const CandidateIterator first = std::ranges::begin(m_candidates);
        const CandidateSentinel last = std::ranges::end(m_candidates);
        return {first, std::ranges::next(first, m_start_index, last), last, m_candidate_count};
    }
    std::default_sentinel_t end() const noexcept { return {}; }
private:
    Candidates m_candidates;
    std::uint64_t m_start_index = 0;
    std::uint64_t m_candidate_count = 0;
};

} // namespace details

class RoundRobin
{
    template <typename Candidates>
    using Selection = details::RoundRobinSelectionView<Candidates>;
public:
    ~RoundRobin() noexcept = default;
    RoundRobin() noexcept = default;
    explicit RoundRobin(std::uint64_t start_index) noexcept : m_next_index(start_index) {}
    RoundRobin(const RoundRobin &) noexcept = default;
    RoundRobin(RoundRobin &&) noexcept = default;
    RoundRobin & operator=(const RoundRobin &) noexcept = default;
    RoundRobin & operator=(RoundRobin &&) noexcept = default;
public:
    template <std::ranges::view Candidates>
    requires std::ranges::forward_range<Candidates>
    Selection<Candidates> select(Candidates candidates) noexcept
    {
        const std::uint64_t candidate_count = std::ranges::distance(candidates);
        if (candidate_count == 0) { return {std::move(candidates), 0, 0}; }
        const std::uint64_t index = m_next_index % candidate_count;
        m_next_index = index + 1;
        return {std::move(candidates), index, candidate_count};
    }
private:
    std::uint64_t m_next_index = 0;
};

} // namespace lcf::async
