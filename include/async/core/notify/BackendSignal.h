#pragma once
#include <async/config/macros.h>
#include <async/core/notify/AtomicSequenceSignal.h>
#include <async/core/event/declarations/concepts.h>
LCF_ASYNC_EXPORT
namespace lcf::async::notify {
template <event_backend_c Backend>
class BackendSignal
{
public:
    using observation_t = AtomicSequenceSignal::observation_t;
    ~BackendSignal() noexcept = default;
    BackendSignal() = delete;
    explicit BackendSignal(Backend & backend) noexcept : m_backend_p(&backend) {}
    BackendSignal(const BackendSignal &) = delete;
    BackendSignal(BackendSignal &&) = delete;
    BackendSignal & operator=(const BackendSignal &) = delete;
    BackendSignal & operator=(BackendSignal &&) = delete;
    observation_t observe() const noexcept { return m_signal.observe(); }
    void wait(observation_t observed) const noexcept { m_signal.wait(observed); }
    void notifyOne() noexcept { m_signal.notifyOne(); m_backend_p->wake(); }
    void notifyAll() noexcept { m_signal.notifyAll(); m_backend_p->wake(); }
private:
    AtomicSequenceSignal m_signal;
    Backend * m_backend_p;
};
}
