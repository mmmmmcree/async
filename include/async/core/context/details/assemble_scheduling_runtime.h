#pragma once

#include <async/core/context/details/ContextResources.h>

namespace lcf::async::details {

template <std::uint64_t queue_capacity, cancellable_executable_c Executable,
    notify::signal_c Signal, notify::notifier_c Notifier,
    index_selection_policy_c InputSelectionPolicy, index_selection_policy_c RequestSelectionPolicy,
    index_selection_policy_c PlacementPolicy, index_selection_policy_c StealSelectionPolicy,
    index_selection_policy_c DispatchSelectionPolicy, template <typename> class Executor>
void assemble_scheduling_runtime(
    ContextResources<queue_capacity, Executable, Signal, Notifier, InputSelectionPolicy, RequestSelectionPolicy,
        PlacementPolicy, StealSelectionPolicy, DispatchSelectionPolicy, Executor> & destination,
    typename std::remove_reference_t<decltype(destination)>::Runtime & runtime,
    std::uint32_t worker_count, std::uint32_t dispatcher_count, std::uint32_t port_count,
    std::span<const SchedulingTargetKind> scheduler_kinds, Notifier & notifier, std::pmr::memory_resource & resource)
{
    using Resources = std::remove_reference_t<decltype(destination)>;
    Resources resources;
    const std::uint64_t shared_queue_count = std::max<std::uint64_t>(worker_count, 1);
    const std::uint64_t thread_queue_count = std::ranges::count(scheduler_kinds, SchedulingTargetKind::eThread);
    resources.m_shared_execution_queues = {shared_queue_count, {&resource}};
    resources.m_thread_execution_queues = {thread_queue_count, {&resource}};
    resources.m_request_queues = {std::uint64_t{dispatcher_count} + 1, {&resource}};
    resources.m_submission_queues = {port_count, {&resource}};
    resources.m_shared_submitters = {1, {&resource}};
    resources.m_dedicated_submitters = {port_count, {&resource}};
    resources.m_work_selectors = {shared_queue_count, {&resource}};
    resources.m_shared_executors = {shared_queue_count, {&resource}};
    resources.m_thread_executors = {thread_queue_count, {&resource}};
    resources.m_dispatchers = {dispatcher_count, {&resource}};
    resources.m_target_routes = {scheduler_kinds.size(), {&resource}};

    for (std::uint64_t index = 0; index < shared_queue_count; ++index) { resources.m_shared_execution_queues.emplace_back(); }
    for (std::uint64_t index = 0; index < thread_queue_count; ++index) { resources.m_thread_execution_queues.emplace_back(); }
    for (std::uint64_t index = 0; index <= dispatcher_count; ++index) { resources.m_request_queues.emplace_back(); }
    for (std::uint64_t index = 0; index < port_count; ++index) { resources.m_submission_queues.emplace_back(); }
    resources.m_shared_submitters.emplace_back(resources.m_request_queues.front(), notifier);
    for (auto & submission_queue : resources.m_submission_queues) {
        resources.m_dedicated_submitters.emplace_back(submission_queue, notifier);
    }
    for (std::uint64_t index = 0; index < shared_queue_count; ++index) {
        resources.m_work_selectors.emplace_back(std::span{resources.m_shared_execution_queues}, index, StealSelectionPolicy{});
    }
    for (auto & selector : resources.m_work_selectors) { resources.m_shared_executors.emplace_back(selector); }
    for (auto & execution_queue : resources.m_thread_execution_queues) { resources.m_thread_executors.emplace_back(execution_queue); }

    std::uint64_t next_thread_queue = 0;
    for (const SchedulingTargetKind kind : scheduler_kinds) {
        const std::uint64_t thread_queue_index = kind == SchedulingTargetKind::eThread ? next_thread_queue++ : 0;
        resources.m_target_routes.emplace_back(kind, thread_queue_index, thread_queue_index % dispatcher_count);
    }
    const std::uint64_t ports_per_dispatcher = (std::uint64_t{port_count} + dispatcher_count - 1) / dispatcher_count;
    std::span remaining_submissions{resources.m_submission_queues};
    for (std::uint64_t index = 0; index < dispatcher_count; ++index) {
        const std::uint64_t assigned_port_count = std::min<std::uint64_t>(ports_per_dispatcher, remaining_submissions.size());
        resources.m_dispatchers.emplace_back(
            typename Resources::DispatchRequestInput{resources.m_request_queues.front(), resources.m_request_queues[index + 1],
                remaining_submissions.first(assigned_port_count), InputSelectionPolicy{}},
            typename Resources::Router{RouteTable{std::span{resources.m_target_routes}},
                std::span{resources.m_shared_execution_queues}, std::span{resources.m_thread_execution_queues},
                std::span{resources.m_request_queues}.subspan(1), index, PlacementPolicy{}},
            RequestSelectionPolicy{});
        remaining_submissions = remaining_submissions.subspan(assigned_port_count);
    }
    destination = std::move(resources);
    runtime.attach(std::span{destination.m_dispatchers}, std::span{destination.m_shared_executors},
        std::span{destination.m_thread_executors});
}

} // namespace lcf::async::details
