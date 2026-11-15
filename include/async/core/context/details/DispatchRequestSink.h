#pragma once

#include <async/core/context/details/declarations/structs.h>
#include <async/core/queue/utils/enqueue_one.h>
#include <async/core/scheduling/dispatcher/declarations/concepts.h>
#include <async/core/scheduling/policy/declarations/concepts.h>
#include <async/core/scheduling/utils/selected.h>

#include <span>
#include <system_error>
#include <utility>

namespace lcf::async::details {

template <producer_queue_c SharedExecutionQueue, producer_queue_c ThreadExecutionQueue,
    producer_queue_c ForwardRequestQueue, selection_policy_c<std::span<SharedExecutionQueue>> PlacementPolicy>
requires dispatch_record_c<typename ForwardRequestQueue::value_t>
    and std::same_as<typename SharedExecutionQueue::value_t, typename ThreadExecutionQueue::value_t>
    and std::same_as<typename SharedExecutionQueue::value_t,
        typename ForwardRequestQueue::value_t::executable_t>
class DispatchRequestSink
{
public:
    using value_t = typename ForwardRequestQueue::value_t;
public:
    ~DispatchRequestSink() noexcept = default;
    DispatchRequestSink() = delete;
    DispatchRequestSink(std::span<const TargetRoute> target_routes,
        std::span<SharedExecutionQueue> shared_execution_queues,
        std::span<ThreadExecutionQueue> thread_execution_queues,
        std::span<ForwardRequestQueue> forward_request_queues,
        std::uint64_t dispatcher_index, PlacementPolicy placement_policy = {}) :
        m_target_routes(target_routes), m_shared_execution_queues(shared_execution_queues),
        m_thread_execution_queues(thread_execution_queues), m_forward_request_queues(forward_request_queues),
        m_dispatcher_index(dispatcher_index), m_placement_policy(std::move(placement_policy)) {}
    DispatchRequestSink(const DispatchRequestSink &) = default;
    DispatchRequestSink(DispatchRequestSink &&) = default;
    DispatchRequestSink & operator=(const DispatchRequestSink &) = default;
    DispatchRequestSink & operator=(DispatchRequestSink &&) = default;
public:
    std::error_code tryEnqueue(value_t & request) noexcept
    {
        const TargetRoute & route = m_target_routes[request.getTargetIndex()];
        if (route.m_target_kind == SchedulingTargetKind::eShared) {
            return queue::utils::enqueue_one(
                m_shared_execution_queues | utils::selected(m_placement_policy), request.getExecutable());
        }
        if (m_forward_request_queues.empty()) { return std::make_error_code(std::errc::invalid_argument); }
        const std::uint64_t target_dispatcher_index =
            route.m_thread_execution_queue_index % m_forward_request_queues.size();
        if (target_dispatcher_index != m_dispatcher_index) {
            return m_forward_request_queues[target_dispatcher_index].tryEnqueue(request);
        }
        return m_thread_execution_queues[route.m_thread_execution_queue_index].tryEnqueue(request.getExecutable());
    }
private:
    std::span<const TargetRoute> m_target_routes;
    std::span<SharedExecutionQueue> m_shared_execution_queues;
    std::span<ThreadExecutionQueue> m_thread_execution_queues;
    std::span<ForwardRequestQueue> m_forward_request_queues;
    std::uint64_t m_dispatcher_index;
    PlacementPolicy m_placement_policy;
};

} // namespace lcf::async::details
