#pragma once

#include <async/core/details/concepts/constructible.h>
#include <async/core/queue/QueueView.h>
#include <async/core/queue/intrusive/Queue.h>
#include <async/core/queue/utils/drain.h>
#include <async/core/queue/utils/pop_one.h>
#include <async/core/scheduling/dispatcher/declarations/concepts.h>
#include <async/core/scheduling/policy/declarations/concepts.h>
#include <async/core/scheduling/utils/selected.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <system_error>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <consumer_queue_c Queue, typename InputSelectionPolicy, typename RequestSelectionPolicy>
requires dispatch_record_c<typename Queue::value_t> and linked_node_c<typename Queue::value_t>
    and cancellable_executable_c<typename Queue::value_t::executable_t>
    and selection_policy_c<InputSelectionPolicy, std::span<Queue>>
    and selection_policy_c<RequestSelectionPolicy,
        std::span<queue::QueueView<typename Queue::value_t>>>
class DispatchRequestStream
{
    using QueueSpan = std::span<Queue>;
    using DispatchRequest = typename Queue::value_t;
    using PendingQueue = queue::intrusive::Queue<DispatchRequest>;
    using RequestQueueView = queue::QueueView<DispatchRequest>;
    class IncomingRequests
    {
    public:
        using value_t = DispatchRequest;
    public:
        ~IncomingRequests() noexcept = default;
        IncomingRequests() = delete;
        IncomingRequests(QueueSpan queues, InputSelectionPolicy selection_policy) noexcept(
            details::all_nothrow_move_constructible<InputSelectionPolicy>) :
            m_queues(queues), m_selection_policy(std::move(selection_policy)) {}
        IncomingRequests(const IncomingRequests &) = delete;
        IncomingRequests(IncomingRequests &&) = default;
        IncomingRequests & operator=(const IncomingRequests &) = delete;
        IncomingRequests & operator=(IncomingRequests &&) = delete;
    public:
        DispatchRequest * tryPop() noexcept
        {
            DispatchRequest * request_p = queue::utils::pop_one(m_queues | utils::selected(m_selection_policy));
            if (request_p) { ++m_received_count; }
            return request_p;
        }
        bool isEmpty() const noexcept { return std::ranges::all_of(m_queues, &Queue::isEmpty); }
        std::uint64_t getReceivedCount() const noexcept { return m_received_count; }
    private:
        QueueSpan m_queues;
        InputSelectionPolicy m_selection_policy;
        std::uint64_t m_received_count = 0;
    };
public:
    using value_t = DispatchRequest;
public:
    ~DispatchRequestStream() noexcept = default;
    DispatchRequestStream() = delete;
    explicit DispatchRequestStream(QueueSpan queues, InputSelectionPolicy input_selection_policy = {},
        RequestSelectionPolicy request_selection_policy = {}) noexcept(
            details::all_nothrow_move_constructible<InputSelectionPolicy, RequestSelectionPolicy>) :
        m_incoming_requests(queues, std::move(input_selection_policy)),
        m_request_selection_policy(std::move(request_selection_policy)) {}
    DispatchRequestStream(const DispatchRequestStream &) = delete;
    DispatchRequestStream(DispatchRequestStream && other) noexcept(
        details::all_nothrow_move_constructible<InputSelectionPolicy, RequestSelectionPolicy>) :
        m_incoming_requests(std::move(other.m_incoming_requests)),
        m_request_selection_policy(std::move(other.m_request_selection_policy))
    {
        std::ranges::for_each(queue::utils::drain(other.m_pending_queue), [this](DispatchRequest & request) noexcept {
            m_pending_queue.pushBack(request);
        });
        m_has_pending.store(not m_pending_queue.isEmpty(), std::memory_order_release);
        other.m_has_pending.store(false, std::memory_order_release);
    }
    DispatchRequestStream & operator=(const DispatchRequestStream &) = delete;
    DispatchRequestStream & operator=(DispatchRequestStream &&) = delete;
public:
    DispatchRequest * tryPop() noexcept
    {
        DispatchRequest * request_p = queue::utils::pop_one(
            std::span<RequestQueueView> {m_request_sources} | utils::selected(m_request_selection_policy));
        if (request_p) { m_has_pending.store(not m_pending_queue.isEmpty(), std::memory_order_release); }
        return request_p;
    }
    void requeue(DispatchRequest & request) noexcept
    {
        m_pending_queue.pushBack(request);
        m_has_pending.store(true, std::memory_order_release);
    }
    bool isEmpty() const noexcept
    {
        return not m_has_pending.load(std::memory_order_acquire) and m_incoming_requests.isEmpty();
    }
    std::uint64_t getReceivedCount() const noexcept { return m_incoming_requests.getReceivedCount(); }
    void cancelPending() noexcept
    {
        const auto cancel_request = [](DispatchRequest & request) noexcept { request.getExecutable().cancel(); };
        std::ranges::for_each(queue::utils::drain(m_pending_queue), cancel_request);
        m_has_pending.store(false, std::memory_order_release);
        std::ranges::for_each(queue::utils::drain(m_incoming_requests), cancel_request);
    }
private:
    IncomingRequests m_incoming_requests;
    PendingQueue m_pending_queue;
    std::array<RequestQueueView, 2> m_request_sources { RequestQueueView {m_incoming_requests}, RequestQueueView {m_pending_queue}};
    RequestSelectionPolicy m_request_selection_policy;
    std::atomic<bool> m_has_pending {false};
};

} // namespace lcf::async
