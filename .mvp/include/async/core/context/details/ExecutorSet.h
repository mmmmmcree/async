#pragma once

#include "NotifiedExecutor.h"

#include <atomic>
#include <cstddef>
#include <span>

namespace lcf::async::details {

template <multi_worker_executor_c Executor>
class ExecutorSet
{
public:
    static constexpr bool k_supports_multiple_workers = true;
public:
    ~ExecutorSet() noexcept = default;
    ExecutorSet() = delete;
    ExecutorSet(std::span<NotifiedExecutor<Executor>> executors, WorkSignal & signal) noexcept :
        m_executors(executors), m_signal_p(&signal) {}
    ExecutorSet(const ExecutorSet &) = delete;
    ExecutorSet(ExecutorSet &&) = delete;
    ExecutorSet & operator=(const ExecutorSet &) = delete;
    ExecutorSet & operator=(ExecutorSet &&) = delete;
public:
    bool executeOne() noexcept
    {
        if (m_executors.empty()) { return false; }
        auto index = m_next_executor.fetch_add(1, std::memory_order_relaxed) % m_executors.size();
        for (std::size_t count = 0; count < m_executors.size(); ++count) {
            if (m_executors[index].executeOne()) { return true; }
            if (++index == m_executors.size()) { index = 0; }
        }
        return false;
    }
    bool waitAndExecuteOne() noexcept
    {
        for (;;) {
            const auto observed = m_signal_p->observe();
            if (m_stop_requested.load(std::memory_order_acquire)) { return false; }
            if (this->executeOne()) { return true; }
            if (m_closed.load(std::memory_order_acquire) and this->isEmpty()) { return false; }
            m_signal_p->wait(observed);
        }
    }
    bool isEmpty() const noexcept
    {
        for (const auto & executor : m_executors) {
            if (not executor.isEmpty()) { return false; }
        }
        return true;
    }
    void requestStop() noexcept
    {
        m_stop_requested.store(true, std::memory_order_release);
        m_signal_p->notifyAll();
    }
    void close() noexcept
    {
        for (auto & executor : m_executors) { executor.close(); }
        m_closed.store(true, std::memory_order_release);
        m_signal_p->notifyAll();
    }
private:
    std::span<NotifiedExecutor<Executor>> m_executors;
    WorkSignal * m_signal_p;
    std::atomic<std::size_t> m_next_executor{0};
    std::atomic<bool> m_stop_requested{false};
    std::atomic<bool> m_closed{false};
};

} // namespace lcf::async::details
