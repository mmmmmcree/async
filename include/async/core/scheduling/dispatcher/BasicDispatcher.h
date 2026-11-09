#pragma once

#include <async/core/scheduling/dispatcher/declarations/concepts.h>

#include <cassert>
#include <atomic>
#include <cstddef>
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
    bool dispatch(std::size_t max_requests = 64) noexcept
    {
        bool progressed = false;
        for (std::size_t attempt = 0; attempt < max_requests; ++attempt) {
            auto * pending_p = m_pending_head_p;
            auto * request_p = m_retry_pending and pending_p ? pending_p : m_inputs.tryPop();
            const bool received = request_p and request_p != pending_p;
            if (not request_p) { request_p = pending_p; }
            if (not request_p) { break; }
            if (request_p == pending_p) {
                m_pending_head_p = request_p->getNext();
                if (not m_pending_head_p) { m_pending_tail_p = nullptr; }
            }
            m_retry_pending = not m_retry_pending;
            request_p->setNext(nullptr);
            const auto error = m_router.route(*request_p);
            // Publication may destroy the operation. Do not touch request on success.
            progressed |= received or not error;
            if (error) {
                assert(error == std::errc::no_buffer_space and "Close ingress before draining and closing outputs");
                if (m_pending_tail_p) { m_pending_tail_p->setNext(request_p); }
                else { m_pending_head_p = request_p; }
                m_pending_tail_p = request_p;
            }
        }
        m_has_pending.store(m_pending_head_p != nullptr, std::memory_order_release);
        return progressed;
    }
    bool isEmpty() const noexcept
    {
        return not m_has_pending.load(std::memory_order_acquire) and m_inputs.isEmpty();
    }
    // Producers and the dispatcher driver must be quiescent.
    void cancelPending() noexcept requires cancellable_executable_c<Executable>
    {
        while (auto * request_p = m_pending_head_p) {
            m_pending_head_p = request_p->getNext();
            if (not m_pending_head_p) { m_pending_tail_p = nullptr; }
            m_has_pending.store(m_pending_head_p != nullptr, std::memory_order_release);
            request_p->setNext(nullptr);
            request_p->getExecutable().cancel();
        }
        while (auto * request_p = m_inputs.tryPop()) {
            request_p->getExecutable().cancel();
        }
    }
private:
    Inputs m_inputs;
    Router m_router;
    DispatchRequest * m_pending_head_p = nullptr;
    DispatchRequest * m_pending_tail_p = nullptr;
    bool m_retry_pending = false;
    std::atomic<bool> m_has_pending{false};
};

} // namespace lcf::async
