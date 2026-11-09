#pragma once

#include <async/core/queue/intrusive/Queue.h>
#include <async/core/scheduling/dispatcher/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/declarations/structs.h>

#include <atomic>
#include <cstdint>
#include <system_error>
#include <utility>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

// One driver at a time; inputs and outputs must outlive dispatching.
template <dispatch_source_c Inputs, typename Router>
requires dispatch_router_c<Router, typename Inputs::Value>
class BasicDispatcher
{
    using DispatchRequest = typename Inputs::Value;
    using Executable = std::remove_reference_t<decltype(std::declval<DispatchRequest &>().getExecutable())>;
    using DispatchRequestPendingQueue = queue::intrusive::Queue<DispatchRequest>;
public:
    ~BasicDispatcher() noexcept = default;
    BasicDispatcher() = delete;
    BasicDispatcher(Inputs inputs, Router router) noexcept(
        std::is_nothrow_move_constructible_v<Inputs> and std::is_nothrow_move_constructible_v<Router>) :
        m_inputs(std::move(inputs)), m_router(std::move(router)) {}
    BasicDispatcher(const BasicDispatcher &) = delete;
    BasicDispatcher(BasicDispatcher &&) = delete;
    BasicDispatcher & operator=(const BasicDispatcher &) = delete;
    BasicDispatcher & operator=(BasicDispatcher &&) = delete;
public:
    DispatchReport dispatch(std::uint64_t max_attempts = 64) noexcept
    {
        DispatchReport report;
        for (std::uint64_t attempt = 0; attempt < max_attempts; ++attempt) {
            DispatchRequest * request_p = this->takeRequest(report);
            if (not request_p) { break; }
            m_retry_pending = not m_retry_pending;
            const std::error_code error_code = m_router.route(*request_p);
            // Publication may destroy the operation. Do not touch request on success.
            if (not error_code) {
                ++report.m_dispatched;
                continue;
            }
            m_pending_queue.pushBack(*request_p);
            if (error_code == std::errc::no_buffer_space) { continue; }
            report.m_error_code = error_code;
            break;
        }
        m_has_pending.store(not m_pending_queue.isEmpty(), std::memory_order_release);
        return report;
    }
    bool isEmpty() const noexcept
    {
        return not m_has_pending.load(std::memory_order_acquire) and m_inputs.isEmpty();
    }
    // Producers and the dispatcher driver must be quiescent.
    void cancelPending() noexcept requires cancellable_executable_c<Executable>
    {
        while (DispatchRequest * request_p = m_pending_queue.popFront()) { request_p->getExecutable().cancel(); }
        m_has_pending.store(false, std::memory_order_release);
        while (DispatchRequest * request_p = m_inputs.tryPop()) {
            request_p->getExecutable().cancel();
        }
    }
private:
    DispatchRequest * takeRequest(DispatchReport & report) noexcept
    {
        if (m_retry_pending and not m_pending_queue.isEmpty()) { return m_pending_queue.popFront(); }
        if (DispatchRequest * request_p = m_inputs.tryPop()) {
            ++report.m_received;
            request_p->setNext(nullptr);
            return request_p;
        }
        return m_pending_queue.popFront();
    }
private:
    Inputs m_inputs;
    Router m_router;
    DispatchRequestPendingQueue m_pending_queue;
    bool m_retry_pending = false;
    std::atomic<bool> m_has_pending{false};
};

} // namespace lcf::async
