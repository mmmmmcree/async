#pragma once

#include <async/core/notify/AtomicSequenceSignal.h>
#include <async/core/notify/declarations/concepts.h>

#include <async/config/macros.h>

LCF_ASYNC_EXPORT
namespace lcf::async::notify {

template <signal_c Signal = AtomicSequenceSignal>
class SignalNotifier
{
public:
    ~SignalNotifier() noexcept = default;
    SignalNotifier() = delete;
    explicit SignalNotifier(Signal & signal) noexcept : m_signal_p(&signal) {}
    SignalNotifier(const SignalNotifier &) noexcept = default;
    SignalNotifier(SignalNotifier &&) noexcept = default;
    SignalNotifier & operator=(const SignalNotifier &) noexcept = default;
    SignalNotifier & operator=(SignalNotifier &&) noexcept = default;
public:
    void notifyOne() const noexcept { m_signal_p->notifyOne(); }
    void notifyAll() const noexcept { m_signal_p->notifyAll(); }
private:
    Signal * m_signal_p;
};

} // namespace lcf::async::notify
