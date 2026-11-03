#pragma once

#include "NotifiedExecutor.h"
#include <async/core/concepts.h>

#include <atomic>
#include <cassert>
#include <exception>
#include <thread>

namespace lcf::async::details {

template <work_executor_c Executor, work_executor_c SharedExecutors>
class ContextEventLoop
{
public:
    ~ContextEventLoop() noexcept = default;
    ContextEventLoop() = delete;
    template <context_executor_factory_c<Executor> Factory>
    ContextEventLoop(Factory & factory, SharedExecutors & shared_executors, WorkSignal & signal) :
        m_executor(factory, signal, signal), m_shared_executors_p(&shared_executors), m_signal_p(&signal) {}
    ContextEventLoop(const ContextEventLoop &) = delete;
    ContextEventLoop(ContextEventLoop &&) = delete;
    ContextEventLoop & operator=(const ContextEventLoop &) = delete;
    ContextEventLoop & operator=(ContextEventLoop &&) = delete;
public:
    void enter() noexcept
    {
        if (m_driving.test_and_set(std::memory_order_acquire)) { std::terminate(); }
        const auto thread_id = std::this_thread::get_id();
        if (m_owner == std::thread::id{}) { m_owner = thread_id; }
        assert(m_owner == thread_id and "Context must be driven by the same owner thread");
    }
    void leave() noexcept { m_driving.clear(std::memory_order_release); }
    NotifiedExecutor<Executor> & getExecutor() noexcept { return m_executor; }
    bool executeOne(bool include_shared) noexcept
    {
        const bool shared_first = include_shared and m_shared_first;
        m_shared_first = not m_shared_first;
        if (shared_first and m_shared_executors_p->executeOne()) { return true; }
        if (m_executor.executeOne()) { return true; }
        return include_shared and not shared_first and m_shared_executors_p->executeOne();
    }
    bool waitAndExecuteOne(bool include_shared) noexcept
    {
        for (;;) {
            const auto observed = m_signal_p->observe();
            if (m_stop_requested.load(std::memory_order_acquire)) { return false; }
            if (this->executeOne(include_shared)) { return true; }
            if (m_closed.load(std::memory_order_acquire) and m_executor.isEmpty()
                and (not include_shared or m_shared_executors_p->isEmpty())) { return false; }
            m_signal_p->wait(observed);
        }
    }
    bool isEmpty() const noexcept { return m_executor.isEmpty(); }
    void requestStop() noexcept
    {
        m_stop_requested.store(true, std::memory_order_release);
        m_signal_p->notifyAll();
    }
    void close() noexcept
    {
        m_executor.close();
        m_closed.store(true, std::memory_order_release);
        m_signal_p->notifyAll();
    }
private:
    NotifiedExecutor<Executor> m_executor;
    SharedExecutors * m_shared_executors_p;
    WorkSignal * m_signal_p;
    std::atomic<bool> m_stop_requested{false};
    std::atomic<bool> m_closed{false};
    std::atomic_flag m_driving{};
    std::thread::id m_owner;
    bool m_shared_first = false;
};

} // namespace lcf::async::details
