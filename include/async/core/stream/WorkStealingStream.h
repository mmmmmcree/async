#pragma once

#include <async/core/details/concepts/constructible.h>
#include <async/core/queue/utils/pop_one.h>
#include <async/core/policy/utils/selected.h>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <consumer_queue_c Queue, typename StealSelectionPolicy>
requires Queue::k_supports_multiple_consumers
class WorkStealingStream
{
    using QueueSpan = std::span<Queue *>;
public:
    using value_t = typename Queue::value_t;
public:
    ~WorkStealingStream() noexcept = default;
    WorkStealingStream() = delete;
    WorkStealingStream(QueueSpan queues, std::uint64_t local_index, StealSelectionPolicy selection_policy = {}) noexcept(
        details::all_nothrow_move_constructible_c<StealSelectionPolicy>) :
        m_queues(queues), m_local_index(local_index), m_selection_policy(std::move(selection_policy)) {}
    WorkStealingStream(const WorkStealingStream &) = default;
    WorkStealingStream(WorkStealingStream &&) = default;
    WorkStealingStream & operator=(const WorkStealingStream &) = default;
    WorkStealingStream & operator=(WorkStealingStream &&) = default;
public:
    value_t * pullOne() noexcept
    {
        if (value_t * value_p = queue::utils::pop_one<queue::utils::from_back>(m_queues[m_local_index])) { return value_p; }
        return queue::utils::pop_one(m_queues | utils::selected(m_selection_policy));
    }
    bool isEmpty() const noexcept { return std::ranges::all_of(m_queues, [](Queue * queue_p) { return queue_p->isEmpty(); }); }
private:
    QueueSpan m_queues;
    std::uint64_t m_local_index;
    StealSelectionPolicy m_selection_policy;
};

} // namespace lcf::async
