#pragma once

#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/details/WorkSignal.h>

#include <atomic>
#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <work_executor_c Executor>
class BasicEventLoop
{
public:
    using Observation = std::uint64_t;
public:
    ~BasicEventLoop() noexcept = default;
    BasicEventLoop() noexcept = default;
    BasicEventLoop(const BasicEventLoop &) = delete;
    BasicEventLoop(BasicEventLoop &&) = delete;
    BasicEventLoop & operator=(const BasicEventLoop &) = delete;
    BasicEventLoop & operator=(BasicEventLoop &&) = delete;
public:
    void create(Executor & executor, details::WorkSignal & signal) noexcept
    {
        m_executor_p = &executor;
        m_signal_p = &signal;
        m_stop_requested.store(false, std::memory_order_relaxed);
        m_closed.store(false, std::memory_order_relaxed);
    }
    void reset() noexcept
    {
        m_executor_p = nullptr;
        m_signal_p = nullptr;
    }
    bool executeOne() noexcept { return m_executor_p->executeOne(); }
    bool isEmpty() const noexcept { return not m_executor_p or m_executor_p->isEmpty(); }
    Observation observe() const noexcept { return m_signal_p->observe(); }
    void wait(Observation observed) const noexcept { m_signal_p->wait(observed); }
    bool isStopRequested() const noexcept { return m_stop_requested.load(std::memory_order_acquire); }
    bool isClosed() const noexcept { return m_closed.load(std::memory_order_acquire); }
    void requestStop() noexcept
    {
        m_stop_requested.store(true, std::memory_order_release);
        m_signal_p->notifyAll();
    }
    void close() noexcept
    {
        m_executor_p->close();
        m_closed.store(true, std::memory_order_release);
        m_signal_p->notifyAll();
    }
private:
    Executor * m_executor_p = nullptr;
    details::WorkSignal * m_signal_p = nullptr;
    std::atomic<bool> m_stop_requested{false};
    std::atomic<bool> m_closed{false};
};

} // namespace lcf::async
