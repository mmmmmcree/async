#pragma once

#include <async/core/scheduling/Executor.h>
#include <coroutine>
#include <memory_resource>
#include <stop_token>

namespace lcf::async::details {

struct CoroutineEnvironment
{
    Executor query(exec::get_scheduler_t) const noexcept { return m_executor; }
    std::stop_token query(exec::get_stop_token_t) const noexcept { return m_stop_token; }
    std::pmr::polymorphic_allocator<std::byte> query(exec::get_allocator_t) const noexcept { return {m_resource_p}; }
    Executor m_executor;
    std::stop_token m_stop_token;
    std::pmr::memory_resource * m_resource_p = std::pmr::get_default_resource();
};

struct CoroutineState
{
    CoroutineState() noexcept : m_final_callback(Callback::make<&CoroutineState::finish_queued>(this)) {}
    CoroutineState(const CoroutineState &) = delete;
    CoroutineState & operator=(const CoroutineState &) = delete;
    std::coroutine_handle<> finish() noexcept
    {
        if (m_return_executor and m_return_executor != m_environment.m_executor) {
            m_return_executor.enqueue(&m_final_callback);
            return std::noop_coroutine();
        }
        return this->dispatch();
    }
    std::coroutine_handle<> dispatch() noexcept
    {
        if (m_parent_p) { return m_continuation; }
        auto complete = m_complete;
        auto * owner_p = m_owner_p;
        complete(owner_p);
        return std::noop_coroutine();
    }
    void stop() noexcept
    {
        auto * root_p = this;
        while (root_p->m_parent_p) { root_p = root_p->m_parent_p; }
        root_p->m_stopped = true;
        auto continuation = root_p->finish();
        continuation.resume();
    }
    static void finish_queued(Callback * callback_p) noexcept
    {
        auto continuation = static_cast<CoroutineState *>(callback_p->m_owner_p)->dispatch();
        continuation.resume();
    }
    CoroutineEnvironment m_environment;
    Executor m_return_executor;
    CoroutineState * m_parent_p = nullptr;
    std::coroutine_handle<> m_continuation;
    void (*m_complete)(void *) noexcept = nullptr;
    void * m_owner_p = nullptr;
    bool m_stopped = false;
    Callback m_final_callback;
};

struct YieldRequest {};
struct RescheduleRequest { Executor m_executor; };

}
