#pragma once

#include <async/core/scheduling/dispatcher/declarations/concepts.h>
#include <async/core/scheduling/policy/declarations/concepts.h>
#include <async/core/scheduling/router/RouteTable.h>

#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <queue_producer_c SharedExecutionQueue, queue_producer_c ThreadExecutionQueue,
    queue_producer_c ForwardRequestQueue, index_selection_policy_c PlacementPolicy>
requires dispatch_record_c<typename ForwardRequestQueue::Value>
    and std::same_as<typename SharedExecutionQueue::Value, typename ThreadExecutionQueue::Value>
    and std::same_as<typename SharedExecutionQueue::Value,
        std::remove_reference_t<decltype(std::declval<typename ForwardRequestQueue::Value &>().getExecutable())>>
class TargetRouter
{
    using DispatchRequest = typename ForwardRequestQueue::Value;
public:
    TargetRouter(RouteTable route_table, std::span<SharedExecutionQueue> shared_execution_queues,
        std::span<ThreadExecutionQueue> thread_execution_queues, std::span<ForwardRequestQueue> forward_request_queues,
        std::uint64_t dispatcher_index, PlacementPolicy placement_policy) :
        m_route_table(route_table), m_shared_execution_queues(shared_execution_queues),
        m_thread_execution_queues(thread_execution_queues), m_forward_request_queues(forward_request_queues),
        m_dispatcher_index(dispatcher_index), m_placement_policy(std::move(placement_policy)) {}

    std::error_code tryRoute(DispatchRequest & request) noexcept
    {
        const TargetRoute & route = m_route_table[request.getTargetIndex()];
        if (route.m_kind == SchedulingTargetKind::eShared) {
            const std::uint64_t queue_index = m_placement_policy.select(m_shared_execution_queues.size());
            return m_shared_execution_queues[queue_index].tryEnqueue(request.getExecutable());
        }
        if (route.m_dispatcher_index != m_dispatcher_index) {
            return m_forward_request_queues[route.m_dispatcher_index].tryEnqueue(request);
        }
        return m_thread_execution_queues[route.m_queue_index].tryEnqueue(request.getExecutable());
    }
private:
    RouteTable m_route_table;
    std::span<SharedExecutionQueue> m_shared_execution_queues;
    std::span<ThreadExecutionQueue> m_thread_execution_queues;
    std::span<ForwardRequestQueue> m_forward_request_queues;
    std::uint64_t m_dispatcher_index;
    PlacementPolicy m_placement_policy;
};

} // namespace lcf::async
