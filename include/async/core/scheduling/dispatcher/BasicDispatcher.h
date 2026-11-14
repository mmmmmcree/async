#pragma once

#include <async/core/details/concepts/constructible.h>
#include <async/core/queue/intrusive/Queue.h>
#include <async/core/queue/utils/drain.h>
#include <async/core/scheduling/dispatcher/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/declarations/structs.h>
#include <async/core/scheduling/policy/declarations/concepts.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <system_error>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <dispatch_request_stream_c RequestStream, typename Router,
    index_selection_policy_c RequestSelectionPolicy>
requires dispatch_router_c<Router, typename RequestStream::value_t>
class BasicDispatcher
{
    using DispatchRequest = typename RequestStream::value_t;
    using Executable = typename DispatchRequest::executable_t;
    using DispatchRequestPendingQueue = queue::intrusive::Queue<DispatchRequest>;
public:
    ~BasicDispatcher() noexcept = default;
    BasicDispatcher() = delete;
    BasicDispatcher(RequestStream request_stream, Router router, RequestSelectionPolicy request_selection_policy) noexcept(
        details::all_nothrow_move_constructible<RequestStream, Router, RequestSelectionPolicy>) :
        m_request_stream(std::move(request_stream)),
        m_router(std::move(router)),
        m_request_selection_policy(std::move(request_selection_policy)) {}
    BasicDispatcher(const BasicDispatcher &) = delete;
    BasicDispatcher(BasicDispatcher &&) = delete;
    BasicDispatcher & operator=(const BasicDispatcher &) = delete;
    BasicDispatcher & operator=(BasicDispatcher &&) = delete;
public:
    DispatchReport dispatchOne() noexcept
    {
        DispatchReport report;
        DispatchRequest * request_p = this->takeRequest(report);
        if (not request_p) { return report; }
        report.m_attempted = 1;
        const std::error_code error_code = m_router.tryRoute(*request_p);
        if (not error_code) { report.m_dispatched = 1; }
        else {
            m_pending_queue.pushBack(*request_p);
            if (error_code != std::errc::no_buffer_space) { report.m_error_code = error_code; }
        }
        m_has_pending.store(not m_pending_queue.isEmpty(), std::memory_order_release);
        return report;
    }
    bool isEmpty() const noexcept
    {
        return not m_has_pending.load(std::memory_order_acquire) and m_request_stream.isEmpty();
    }
    void cancelPending() noexcept requires cancellable_executable_c<Executable>
    {
        const auto cancel_request = [](DispatchRequest & request) noexcept { request.getExecutable().cancel(); };
        std::ranges::for_each(queue::utils::drain(m_pending_queue), cancel_request);
        m_has_pending.store(false, std::memory_order_release);
        std::ranges::for_each(queue::utils::drain(m_request_stream), cancel_request);
    }
private:
    DispatchRequest * takeRequest(DispatchReport & report) noexcept
    {
        const std::uint64_t first_source_index = m_request_selection_policy.select(2);
        if (DispatchRequest * request_p = this->takeRequestFrom(first_source_index, report)) { return request_p; }
        return this->takeRequestFrom(1 - first_source_index, report);
    }
    DispatchRequest * takeRequestFrom(std::uint64_t source_index, DispatchReport & report) noexcept
    {
        if (source_index == 1) { return m_pending_queue.tryPop(); }
        if (DispatchRequest * request_p = m_request_stream.tryPop()) {
            ++report.m_received;
            return request_p;
        }
        return nullptr;
    }
private:
    RequestStream m_request_stream;
    Router m_router;
    RequestSelectionPolicy m_request_selection_policy;
    DispatchRequestPendingQueue m_pending_queue;
    std::atomic<bool> m_has_pending{false};
};

} // namespace lcf::async
