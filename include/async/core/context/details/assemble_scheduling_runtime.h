#pragma once

#include <async/core/context/details/ContextResources.h>

namespace lcf::async::details {

template <std::uint64_t queue_capacity, cancellable_executable_c Executable,
    notify::signal_c Signal, notify::notifier_c Notifier,
    template <typename> class Executor,
    template <typename> class ExecutableStream, template <typename> class RequestStream,
    template <typename, typename, typename> class RequestSink, template <typename, typename> class Dispatcher,
    template <typename, typename, typename, typename> class Runtime>
void assemble_scheduling_runtime(
    ContextResources<queue_capacity, Executable, Signal, Notifier,
        Executor, ExecutableStream, RequestStream, RequestSink, Dispatcher, Runtime> & destination,
    typename std::remove_reference_t<decltype(destination)>::runtime_t & runtime,
    std::uint32_t worker_count, std::uint32_t dispatcher_count, std::uint32_t port_count,
    std::span<const SchedulingTargetKind> scheduler_kinds, Notifier & notifier, std::pmr::memory_resource & resource)
{
    using Resources = std::remove_reference_t<decltype(destination)>;
    Resources resources;
    const std::uint64_t shared_queue_count = std::max<std::uint64_t>(worker_count, 1);
    resources.m_shared_execution_queues = {shared_queue_count, {&resource}};
    resources.m_thread_execution_queues = {port_count, {&resource}};
    resources.m_request_queues = {std::uint64_t{dispatcher_count} + 1, {&resource}};
    resources.m_submission_queues = {port_count, {&resource}};
    resources.m_shared_submitters = {1, {&resource}};
    resources.m_dedicated_submitters = {port_count, {&resource}};
    resources.m_shared_executable_streams = {shared_queue_count, {&resource}};
    resources.m_request_queue_views = {std::uint64_t {port_count} + 2 * std::uint64_t {dispatcher_count}, {&resource}};
    resources.m_thread_shared_executors = {shared_queue_count, {&resource}};
    resources.m_thread_local_executors = {port_count, {&resource}};
    resources.m_dispatchers = {dispatcher_count, {&resource}};
    resources.m_target_routes = {scheduler_kinds.size(), {&resource}};

    for (std::uint64_t index = 0; index < shared_queue_count; ++index) { resources.m_shared_execution_queues.emplace_back(); }
    for (std::uint64_t index = 0; index < port_count; ++index) { resources.m_thread_execution_queues.emplace_back(); }
    for (std::uint64_t index = 0; index <= dispatcher_count; ++index) { resources.m_request_queues.emplace_back(); }
    for (std::uint64_t index = 0; index < port_count; ++index) { resources.m_submission_queues.emplace_back(); }
    resources.m_shared_submitters.emplace_back(resources.m_request_queues.front(), notifier);
    for (auto & submission_queue : resources.m_submission_queues) {
        resources.m_dedicated_submitters.emplace_back(submission_queue, notifier);
    }
    std::span shared_queues {resources.m_shared_execution_queues};
    for (std::uint64_t index = 0; index < shared_queue_count; ++index) {
        resources.m_shared_executable_streams.emplace_back(shared_queues, index);
    }
    for (auto & stream : resources.m_shared_executable_streams) { resources.m_thread_shared_executors.emplace_back(stream); }
    for (auto & execution_queue : resources.m_thread_execution_queues) { resources.m_thread_local_executors.emplace_back(execution_queue); }

    std::uint64_t next_thread_queue = 0;
    for (const SchedulingTargetKind kind : scheduler_kinds) {
        const std::uint64_t thread_queue_index = kind == SchedulingTargetKind::eLocal ? next_thread_queue++ : 0;
        resources.m_target_routes.emplace_back(kind, thread_queue_index);
    }
    const std::uint64_t ports_per_dispatcher = (std::uint64_t{port_count} + dispatcher_count - 1) / dispatcher_count;
    std::span remaining_submissions{resources.m_submission_queues};
    for (std::uint64_t index = 0; index < dispatcher_count; ++index) {
        const std::uint64_t assigned_port_count = std::min<std::uint64_t>(ports_per_dispatcher, remaining_submissions.size());
        const std::uint64_t first_input_index = resources.m_request_queue_views.size();
        for (auto & queue : remaining_submissions.first(assigned_port_count)) {
            resources.m_request_queue_views.emplace_back(queue);
        }
        resources.m_request_queue_views.emplace_back(resources.m_request_queues[index + 1]);
        resources.m_request_queue_views.emplace_back(resources.m_request_queues.front());
        resources.m_dispatchers.emplace_back(
            typename Resources::dispatch_request_stream_t {
                std::span {resources.m_request_queue_views}.subspan(first_input_index, assigned_port_count + 2)},
            typename Resources::request_sink_t {std::span<const TargetRoute> {resources.m_target_routes},
                std::span{resources.m_shared_execution_queues}, std::span{resources.m_thread_execution_queues},
                std::span{resources.m_request_queues}.subspan(1), index});
        remaining_submissions = remaining_submissions.subspan(assigned_port_count);
    }
    destination = std::move(resources);
    runtime.attach(std::span{destination.m_dispatchers}, std::span{destination.m_thread_shared_executors},
        std::span{destination.m_thread_local_executors});
}

} // namespace lcf::async::details
