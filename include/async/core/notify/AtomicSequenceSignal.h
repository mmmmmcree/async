#pragma once

#include <async/config/macros.h>

#include <atomic>
#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async::notify {

class AtomicSequenceSignal
{
public:
    using Observation = std::uint64_t;
public:
    ~AtomicSequenceSignal() noexcept = default;
    AtomicSequenceSignal() noexcept = default;
    AtomicSequenceSignal(const AtomicSequenceSignal &) = delete;
    AtomicSequenceSignal(AtomicSequenceSignal &&) = delete;
    AtomicSequenceSignal & operator=(const AtomicSequenceSignal &) = delete;
    AtomicSequenceSignal & operator=(AtomicSequenceSignal &&) = delete;
public:
    Observation observe() const noexcept { return m_sequence.load(std::memory_order_acquire); }
    void wait(Observation observed) const noexcept { m_sequence.wait(observed, std::memory_order_acquire); }
    void notifyOne() noexcept
    {
        m_sequence.fetch_add(1, std::memory_order_release);
        m_sequence.notify_one();
    }
    void notifyAll() noexcept
    {
        m_sequence.fetch_add(1, std::memory_order_release);
        m_sequence.notify_all();
    }
private:
    std::atomic<Observation> m_sequence {0};
};

} // namespace lcf::async::notify
