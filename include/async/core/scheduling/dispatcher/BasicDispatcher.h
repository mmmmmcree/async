#pragma once

#include <async/core/queue/intrusive/Queue.h>
#include <async/core/scheduling/dispatcher/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/declarations/structs.h>
#include <async/core/scheduling/policy/declarations/concepts.h>

#include <atomic>
#include <cstdint>
#include <system_error>
#include <utility>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

// One driver at a time; inputs and outputs must outlive dispatching.
template <dispatch_request_input_c RequestInput, typename Router,
    index_selection_policy_c RequestSelectionPolicy>
requires dispatch_router_c<Router, typename RequestInput::Value>
class BasicDispatcher
{
    using DispatchRequest = typename RequestInput::Value;
    using Executable = std::remove_reference_t<decltype(std::declval<DispatchRequest &>().getExecutable())>;
    using DispatchRequestPendingQueue = queue::intrusive::Queue<DispatchRequest>;
public:
    ~BasicDispatcher() noexcept = default;
    BasicDispatcher() = delete;
    BasicDispatcher(RequestInput request_input, Router router, RequestSelectionPolicy request_selection_policy) noexcept(
        std::is_nothrow_move_constructible_v<RequestInput> and std::is_nothrow_move_constructible_v<Router>
        and std::is_nothrow_move_constructible_v<RequestSelectionPolicy>) :
        m_request_input(std::move(request_input)), m_router(std::move(router)),
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
        // Successful publication may destroy the request.
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
        return not m_has_pending.load(std::memory_order_acquire) and m_request_input.isEmpty();
    }
    // Producers and the dispatcher driver must be quiescent.
    void cancelPending() noexcept requires cancellable_executable_c<Executable>
    {
        while (DispatchRequest * request_p = m_pending_queue.popFront()) { request_p->getExecutable().cancel(); }
        m_has_pending.store(false, std::memory_order_release);
        while (DispatchRequest * request_p = m_request_input.tryPop()) {
            request_p->getExecutable().cancel();
        }
    }
private:
    DispatchRequest * takeRequest(DispatchReport & report) noexcept
    {
        const std::uint64_t first_request_input_index = m_request_selection_policy.select(2);
        if (DispatchRequest * request_p = this->takeRequestFrom(first_request_input_index, report)) { return request_p; }
        return this->takeRequestFrom(1 - first_request_input_index, report);
    }
    DispatchRequest * takeRequestFrom(std::uint64_t input_index, DispatchReport & report) noexcept
    {
        if (input_index == 1) { return m_pending_queue.popFront(); }
        if (DispatchRequest * request_p = m_request_input.tryPop()) {
            ++report.m_received;
            return request_p;
        }
        return nullptr;
    }
private:
    RequestInput m_request_input;
    Router m_router;
    RequestSelectionPolicy m_request_selection_policy;
    DispatchRequestPendingQueue m_pending_queue;
    std::atomic<bool> m_has_pending{false};
};

} // namespace lcf::async
