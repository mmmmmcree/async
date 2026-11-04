#pragma once

#include "WorkSignal.h"

#include <atomic>
#include <cstdint>

namespace lcf::async::details {

class WorkerControl
{
public:
    using Observation = std::uint64_t;
public:
    ~WorkerControl() noexcept = default;
    WorkerControl() noexcept = default;
    WorkerControl(const WorkerControl &) = delete;
    WorkerControl(WorkerControl &&) = delete;
    WorkerControl & operator=(const WorkerControl &) = delete;
    WorkerControl & operator=(WorkerControl &&) = delete;
public:
    void create(WorkSignal & signal) noexcept
    {
        m_signal_p = &signal;
        m_stop_requested.store(false, std::memory_order_relaxed);
        m_closed.store(false, std::memory_order_relaxed);
    }
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
        m_closed.store(true, std::memory_order_release);
        m_signal_p->notifyAll();
    }
private:
    WorkSignal * m_signal_p = nullptr;
    std::atomic<bool> m_stop_requested{false};
    std::atomic<bool> m_closed{false};
};

} // namespace lcf::async::details
