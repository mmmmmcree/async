#pragma once

#include <async/config/macros.h>
#include <async/core/notify/AtomicSequenceSignal.h>
#include <async/core/notify/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async::notify {

template <event_pump_c EventPump>
class EventPumpSignal
{
public:
    using observation_t = AtomicSequenceSignal::observation_t;
public:
    ~EventPumpSignal() noexcept = default;
    EventPumpSignal() = delete;
    explicit EventPumpSignal(EventPump & event_pump) noexcept : m_event_pump_p(&event_pump) {}
    EventPumpSignal(const EventPumpSignal &) = delete;
    EventPumpSignal(EventPumpSignal &&) = delete;
    EventPumpSignal & operator=(const EventPumpSignal &) = delete;
    EventPumpSignal & operator=(EventPumpSignal &&) = delete;
public:
    observation_t observe() const noexcept { return m_signal.observe(); }
    void wait(observation_t observed) const noexcept { m_signal.wait(observed); }
    void notifyOne() noexcept { m_signal.notifyOne(); m_event_pump_p->wake(); }
    void notifyAll() noexcept { m_signal.notifyAll(); m_event_pump_p->wake(); }
private:
    AtomicSequenceSignal m_signal;
    EventPump * m_event_pump_p;
};

} // namespace lcf::async::notify
