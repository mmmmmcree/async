#pragma once

#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/queue/non_intrusive/spsc/Queue.h>
#include <async/core/queue/utils/drain.h>
#include <async/core/scheduling/QueueSubmitter.h>
#include <async/core/scheduling/SchedulingRuntime.h>
#include <async/core/scheduling/dispatcher/BasicDispatcher.h>
#include <async/core/scheduling/dispatcher/DispatchRequest.h>
#include <async/core/scheduling/executor/BasicExecutor.h>
#include <async/core/scheduling/router/TargetRouter.h>
#include <async/core/scheduling/selector/WorkStealingSelector.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <limits>
#include <memory_resource>
#include <span>

namespace lcf::async::details {

template <std::uint64_t queue_capacity, cancellable_executable_c Executable,
    notify::signal_c Signal, notify::notifier_c Notifier,
    index_selection_policy_c InputSelectionPolicy, index_selection_policy_c RequestSelectionPolicy,
    index_selection_policy_c PlacementPolicy, index_selection_policy_c StealSelectionPolicy,
    index_selection_policy_c DispatchSelectionPolicy, template <typename> class Executor>
requires linked_node_c<Executable>
struct ContextResources
{
    using DispatchRequest = ::lcf::async::DispatchRequest<Executable>;
    using SharedExecutionQueue = queue::intrusive::mpmc::Queue<Executable>;
    using ThreadExecutionQueue = queue::non_intrusive::spsc::Queue<Executable, queue_capacity>;
    using RequestQueue = queue::intrusive::mpmc::Queue<DispatchRequest>;
    using SubmissionQueue = queue::non_intrusive::spsc::Queue<DispatchRequest, queue_capacity>;
    using SharedSubmitter = QueueSubmitter<RequestQueue, Notifier>;
    using DedicatedSubmitter = QueueSubmitter<SubmissionQueue, Notifier>;
    using WorkSelector = WorkStealingSelector<SharedExecutionQueue, StealSelectionPolicy>;
    using SharedExecutor = Executor<WorkSelector>;
    using ThreadExecutor = Executor<ThreadExecutionQueue>;
    using Router = TargetRouter<SharedExecutionQueue, ThreadExecutionQueue, RequestQueue, PlacementPolicy>;

    class DispatchRequestInput
    {
    public:
        using Value = DispatchRequest;

        DispatchRequestInput(RequestQueue & shared_requests, RequestQueue & forwarded_requests,
            std::span<SubmissionQueue> submission_queues, InputSelectionPolicy input_selection_policy) :
            m_shared_requests_p(&shared_requests), m_forwarded_requests_p(&forwarded_requests),
            m_submission_queues(submission_queues), m_input_selection_policy(std::move(input_selection_policy)) {}

        DispatchRequest * tryPop() noexcept
        {
            const std::uint64_t input_count = m_submission_queues.size() + 2;
            const std::uint64_t first_input_index = m_input_selection_policy.select(input_count);
            for (std::uint64_t attempt = 0; attempt < input_count; ++attempt) {
                const std::uint64_t input_index = (first_input_index + attempt) % input_count;
                if (DispatchRequest * request_p = this->tryPopFrom(input_index)) { return request_p; }
            }
            return nullptr;
        }
        bool isEmpty() const noexcept
        {
            return m_shared_requests_p->isEmpty() and m_forwarded_requests_p->isEmpty()
                and std::ranges::all_of(m_submission_queues, &SubmissionQueue::isEmpty);
        }
    private:
        DispatchRequest * tryPopFrom(std::uint64_t input_index) noexcept
        {
            if (input_index < m_submission_queues.size()) { return m_submission_queues[input_index].tryPop(); }
            if (input_index == m_submission_queues.size()) { return m_forwarded_requests_p->tryPop(); }
            return m_shared_requests_p->tryPop();
        }
        RequestQueue * m_shared_requests_p;
        RequestQueue * m_forwarded_requests_p;
        std::span<SubmissionQueue> m_submission_queues;
        InputSelectionPolicy m_input_selection_policy;
    };

    using Dispatcher = BasicDispatcher<DispatchRequestInput, Router, RequestSelectionPolicy>;
    using Runtime = SchedulingRuntime<Dispatcher, SharedExecutor, ThreadExecutor, Signal, DispatchSelectionPolicy>;
    using DispatcherState = typename Runtime::DispatcherState;
    using StorageRequirementsResult = std::expected<std::pair<std::size_t, std::size_t>, std::error_code>;

    static StorageRequirementsResult calc_storage_requirements(std::uint64_t worker_count,
        std::uint64_t dispatcher_count, std::uint64_t port_count, std::uint64_t scheduler_count,
        std::uint64_t thread_scheduler_count) noexcept
    {
        struct StorageSegment
        {
            std::uint64_t m_count;
            std::size_t m_element_size;
            std::size_t m_alignment;
        };
        const std::uint64_t shared_queue_count = std::max<std::uint64_t>(worker_count, 1);
        const std::array<StorageSegment, 11> segments {{
            {shared_queue_count, sizeof(SharedExecutionQueue), alignof(SharedExecutionQueue)},
            {thread_scheduler_count, sizeof(ThreadExecutionQueue), alignof(ThreadExecutionQueue)},
            {dispatcher_count + 1, sizeof(RequestQueue), alignof(RequestQueue)},
            {port_count, sizeof(SubmissionQueue), alignof(SubmissionQueue)},
            {1, sizeof(SharedSubmitter), alignof(SharedSubmitter)},
            {port_count, sizeof(DedicatedSubmitter), alignof(DedicatedSubmitter)},
            {shared_queue_count, sizeof(WorkSelector), alignof(WorkSelector)},
            {shared_queue_count, sizeof(SharedExecutor), alignof(SharedExecutor)},
            {thread_scheduler_count, sizeof(ThreadExecutor), alignof(ThreadExecutor)},
            {dispatcher_count, sizeof(DispatcherState), alignof(DispatcherState)},
            {scheduler_count, sizeof(TargetRoute), alignof(TargetRoute)}
        }};
        const std::size_t max_size = std::numeric_limits<std::size_t>::max();
        std::size_t size_in_bytes = 0;
        std::size_t storage_alignment = 1;
        for (const auto & [count, element_size, alignment] : segments) {
            if (count == 0) { continue; }
            const std::size_t padding = (alignment - size_in_bytes % alignment) % alignment;
            if (size_in_bytes > max_size - padding or count > (max_size - size_in_bytes - padding) / element_size) {
                return std::unexpected{std::make_error_code(std::errc::value_too_large)};
            }
            size_in_bytes += padding + static_cast<std::size_t>(count) * element_size;
            storage_alignment = std::max(storage_alignment, alignment);
        }
        return std::pair{size_in_bytes, storage_alignment};
    }

    void closeSubmissions() noexcept
    {
        if (not m_request_queues.empty()) { m_request_queues.front().close(); }
        std::ranges::for_each(m_submission_queues, &SubmissionQueue::close);
    }
    // All producers and runtime drivers must have stopped before cancellation.
    void cancelPending() noexcept
    {
        for (auto & state : m_dispatchers) { state.m_dispatcher.cancelPending(); }
        for (auto & execution_queue : m_shared_execution_queues) {
            std::ranges::for_each(queue::utils::drain(execution_queue), &Executable::cancel);
        }
        for (auto & execution_queue : m_thread_execution_queues) {
            std::ranges::for_each(queue::utils::drain(execution_queue), &Executable::cancel);
        }
    }

    FixedCapacityVector<SharedExecutionQueue> m_shared_execution_queues;
    FixedCapacityVector<ThreadExecutionQueue> m_thread_execution_queues;
    FixedCapacityVector<RequestQueue> m_request_queues;
    FixedCapacityVector<SubmissionQueue> m_submission_queues;
    FixedCapacityVector<SharedSubmitter> m_shared_submitters;
    FixedCapacityVector<DedicatedSubmitter> m_dedicated_submitters;
    FixedCapacityVector<WorkSelector> m_work_selectors;
    FixedCapacityVector<SharedExecutor> m_shared_executors;
    FixedCapacityVector<ThreadExecutor> m_thread_executors;
    FixedCapacityVector<DispatcherState> m_dispatchers;
    FixedCapacityVector<TargetRoute> m_target_routes;
};

} // namespace lcf::async::details
