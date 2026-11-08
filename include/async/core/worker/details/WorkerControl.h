#pragma once

#include <async/core/notify/AtomicSequenceSignal.h>
#include <async/core/notify/declarations/concepts.h>

#include <atomic>

namespace lcf::async::details {

template <notify::signal_c Signal = notify::AtomicSequenceSignal>
class WorkerControl
{
public:
    using Observation = typename Signal::Observation;
public:
    ~WorkerControl() noexcept = default;
    WorkerControl() = delete;
    explicit WorkerControl(Signal & signal) noexcept : m_signal_p(&signal) {}
    WorkerControl(const WorkerControl &) = delete;
    WorkerControl(WorkerControl &&) = delete;
    WorkerControl & operator=(const WorkerControl &) = delete;
    WorkerControl & operator=(WorkerControl &&) = delete;
public:
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
    Signal * m_signal_p;
    std::atomic<bool> m_stop_requested{false};
    std::atomic<bool> m_closed{false};
};

} // namespace lcf::async::details
