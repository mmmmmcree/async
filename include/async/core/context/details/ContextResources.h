#pragma once

#include <async/core/context/details/DispatchRequestSink.h>
#include <async/core/context/details/declarations/structs.h>
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/queue/QueueView.h>
#include <async/core/queue/non_intrusive/spsc/Queue.h>
#include <async/core/queue/utils/drain.h>
#include <async/core/scheduling/QueueSubmitter.h>
#include <async/core/scheduling/SchedulingRuntime.h>
#include <async/core/scheduling/dispatcher/BasicDispatcher.h>
#include <async/core/scheduling/dispatcher/DispatchRequest.h>
#include <async/core/scheduling/dispatcher/DispatchRequestStream.h>
#include <async/core/scheduling/executor/BasicExecutor.h>
#include <async/core/scheduling/executor/WorkStealingStream.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <limits>

namespace lcf::async::details {

template <std::uint64_t queue_capacity, cancellable_executable_c Executable,
    notify::signal_c Signal, notify::notifier_c Notifier,
    template <typename> class Executor,
    template <typename> class ExecutableStream,
    template <typename> class RequestStream,
    template <typename, typename, typename> class RequestSink,
    template <typename, typename> class Dispatcher,
    template <typename, typename, typename, typename> class Runtime>
requires linked_node_c<Executable>
struct ContextResources
{
    using dispatch_request_t = ::lcf::async::DispatchRequest<Executable>;
    using shared_execution_queue_t = queue::intrusive::mpmc::Queue<Executable>;
    using thread_execution_queue_t = queue::non_intrusive::spsc::Queue<Executable, queue_capacity>;
    using request_queue_t = queue::intrusive::mpmc::Queue<dispatch_request_t>;
    using submission_queue_t = queue::non_intrusive::spsc::Queue<dispatch_request_t, queue_capacity>;
    using request_queue_view_t = queue::QueueView<dispatch_request_t>;
    using shared_submitter_t = QueueSubmitter<request_queue_t, Notifier>;
    using dedicated_submitter_t = QueueSubmitter<submission_queue_t, Notifier>;
    using shared_executable_stream_t = ExecutableStream<shared_execution_queue_t>;
    static_assert(consumer_queue_c<shared_executable_stream_t>
        and std::same_as<typename shared_executable_stream_t::value_t, Executable>);
    using thread_shared_executor_t = Executor<shared_executable_stream_t>;
    using thread_local_executor_t = Executor<thread_execution_queue_t>;
    using request_sink_t = RequestSink<shared_execution_queue_t, thread_execution_queue_t, request_queue_t>;

    using dispatch_request_stream_t = RequestStream<request_queue_view_t>;
    static_assert(consumer_queue_c<dispatch_request_stream_t>
        and std::same_as<typename dispatch_request_stream_t::value_t, dispatch_request_t>);
    using dispatcher_t = Dispatcher<dispatch_request_stream_t, request_sink_t>;
    using runtime_t = Runtime<dispatcher_t, thread_shared_executor_t, thread_local_executor_t, Signal>;
    using storage_requirements_result_t = std::expected<std::pair<std::size_t, std::size_t>, std::error_code>;

    static storage_requirements_result_t calc_storage_requirements(std::uint64_t worker_count,
        std::uint64_t dispatcher_count, std::uint64_t port_count, std::uint64_t scheduler_count) noexcept
    {
        struct StorageSegment
        {
            std::uint64_t m_count;
            std::size_t m_element_size;
            std::size_t m_alignment;
        };
        const std::uint64_t shared_queue_count = std::max<std::uint64_t>(worker_count, 1);
        const std::array<StorageSegment, 12> segments {{
            {shared_queue_count, sizeof(shared_execution_queue_t), alignof(shared_execution_queue_t)},
            {port_count, sizeof(thread_execution_queue_t), alignof(thread_execution_queue_t)},
            {dispatcher_count + 1, sizeof(request_queue_t), alignof(request_queue_t)},
            {port_count, sizeof(submission_queue_t), alignof(submission_queue_t)},
            {1, sizeof(shared_submitter_t), alignof(shared_submitter_t)},
            {port_count, sizeof(dedicated_submitter_t), alignof(dedicated_submitter_t)},
            {shared_queue_count, sizeof(shared_executable_stream_t), alignof(shared_executable_stream_t)},
            {port_count + 2 * dispatcher_count, sizeof(request_queue_view_t), alignof(request_queue_view_t)},
            {shared_queue_count, sizeof(thread_shared_executor_t), alignof(thread_shared_executor_t)},
            {port_count, sizeof(thread_local_executor_t), alignof(thread_local_executor_t)},
            {dispatcher_count, sizeof(dispatcher_t), alignof(dispatcher_t)},
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
        std::ranges::for_each(m_submission_queues, &submission_queue_t::close);
    }
    // All producers and runtime drivers must have stopped before cancellation.
    void cancelPending() noexcept
    {
        std::ranges::for_each(m_dispatchers, &dispatcher_t::cancelPending);
        for (auto & execution_queue : m_shared_execution_queues) {
            std::ranges::for_each(queue::utils::drain(execution_queue), &Executable::cancel);
        }
        for (auto & execution_queue : m_thread_execution_queues) {
            std::ranges::for_each(queue::utils::drain(execution_queue), &Executable::cancel);
        }
    }

    FixedCapacityVector<shared_execution_queue_t> m_shared_execution_queues;
    FixedCapacityVector<thread_execution_queue_t> m_thread_execution_queues;
    FixedCapacityVector<request_queue_t> m_request_queues;
    FixedCapacityVector<submission_queue_t> m_submission_queues;
    FixedCapacityVector<shared_submitter_t> m_shared_submitters;
    FixedCapacityVector<dedicated_submitter_t> m_dedicated_submitters;
    FixedCapacityVector<shared_executable_stream_t> m_shared_executable_streams;
    FixedCapacityVector<request_queue_view_t> m_request_queue_views;
    FixedCapacityVector<thread_shared_executor_t> m_thread_shared_executors;
    FixedCapacityVector<thread_local_executor_t> m_thread_local_executors;
    FixedCapacityVector<dispatcher_t> m_dispatchers;
    FixedCapacityVector<TargetRoute> m_target_routes;
};

} // namespace lcf::async::details
