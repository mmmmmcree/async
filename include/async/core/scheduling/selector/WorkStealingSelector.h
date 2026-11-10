#pragma once

#include <async/core/queue/declarations/concepts.h>
#include <async/core/scheduling/policy/declarations/concepts.h>

#include <algorithm>
#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <executable_input_c ExecutionQueue, index_selection_policy_c StealSelectionPolicy>
requires ExecutionQueue::k_supports_multiple_consumers
class WorkStealingSelector
{
public:
    using Value = typename ExecutionQueue::Value;

    template <std::size_t queue_extent>
    WorkStealingSelector(std::span<ExecutionQueue, queue_extent> execution_queues, std::uint64_t local_queue_index,
        StealSelectionPolicy steal_selection_policy) :
        m_execution_queues(execution_queues), m_local_queue_index(local_queue_index),
        m_steal_selection_policy(std::move(steal_selection_policy)) {}

    Value * tryPop() noexcept
    {
        if (auto * executable_p = m_execution_queues[m_local_queue_index].tryPop()) { return executable_p; }
        const std::uint64_t candidate_count = m_execution_queues.size() - 1;
        if (candidate_count == 0) { return nullptr; }
        const std::uint64_t first_candidate_index = m_steal_selection_policy.select(candidate_count);
        for (std::uint64_t attempt = 0; attempt < candidate_count; ++attempt) {
            const std::uint64_t candidate_index = (first_candidate_index + attempt) % candidate_count;
            const std::uint64_t queue_index = candidate_index + (candidate_index >= m_local_queue_index);
            if (auto * executable_p = m_execution_queues[queue_index].tryPop()) { return executable_p; }
        }
        return nullptr;
    }
    bool isEmpty() const noexcept
    {
        return std::ranges::all_of(m_execution_queues, &ExecutionQueue::isEmpty);
    }
private:
    std::span<ExecutionQueue> m_execution_queues;
    std::uint64_t m_local_queue_index;
    StealSelectionPolicy m_steal_selection_policy;
};

} // namespace lcf::async
