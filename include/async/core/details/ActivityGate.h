#pragma once
#include <atomic>
#include <cstdint>
namespace lcf::async::details {
class ActivityGate
{
    static constexpr std::uint64_t k_closed = std::uint64_t {1} << 63;
public:
    ~ActivityGate() noexcept = default;
    ActivityGate() noexcept = default;
    ActivityGate(const ActivityGate &) = delete;
    ActivityGate(ActivityGate &&) = delete;
    ActivityGate & operator=(const ActivityGate &) = delete;
    ActivityGate & operator=(ActivityGate &&) = delete;
    bool tryEnter() noexcept
    {
        std::uint64_t state = m_state.load(std::memory_order_relaxed);
        while (not (state & k_closed)) {
            if (m_state.compare_exchange_weak(state, state + 1, std::memory_order_acquire, std::memory_order_relaxed)) { return true; }
        }
        return false;
    }
    void leave() noexcept { m_state.fetch_sub(1, std::memory_order_release); m_state.notify_all(); }
    void close() noexcept { m_state.fetch_or(k_closed, std::memory_order_acq_rel); }
    bool isClosed() const noexcept { return (m_state.load(std::memory_order_acquire) & k_closed) != 0; }
    bool isClosedAndQuiescent() const noexcept { return m_state.load(std::memory_order_acquire) == k_closed; }
    void wait() const noexcept
    {
        std::uint64_t state = m_state.load(std::memory_order_acquire);
        while ((state & ~k_closed) != 0) { m_state.wait(state, std::memory_order_acquire); state = m_state.load(std::memory_order_acquire); }
    }
private:
    std::atomic<std::uint64_t> m_state {0};
};

template <typename Waiter>
class ExecutionGuard
{
public:
    ~ExecutionGuard() noexcept { if (m_entered) { m_waiter_p->endExecution(); } }
    ExecutionGuard() = delete;
    explicit ExecutionGuard(Waiter & waiter) noexcept : m_waiter_p(&waiter), m_entered(waiter.beginExecution()) {}
    ExecutionGuard(const ExecutionGuard &) = delete;
    ExecutionGuard(ExecutionGuard &&) = delete;
    ExecutionGuard & operator=(const ExecutionGuard &) = delete;
    ExecutionGuard & operator=(ExecutionGuard &&) = delete;
    explicit operator bool() const noexcept { return m_entered; }
private:
    Waiter * m_waiter_p;
    bool m_entered;
};
}
