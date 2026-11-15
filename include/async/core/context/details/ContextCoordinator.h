#pragma once
#include <async/core/notify/declarations/concepts.h>
#include <async/core/details/ActivityGate.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <system_error>
namespace lcf::async::details {
template <notify::signal_c Signal>
class ContextCoordinator
{
public:
    using observation_t = typename Signal::observation_t;
public:
    ~ContextCoordinator() noexcept = default;
    ContextCoordinator() = delete;
    explicit ContextCoordinator(Signal & signal) noexcept : m_signal_p(&signal) {}
    ContextCoordinator(const ContextCoordinator &) = delete;
    ContextCoordinator(ContextCoordinator &&) = delete;
    ContextCoordinator & operator=(const ContextCoordinator &) = delete;
    ContextCoordinator & operator=(ContextCoordinator &&) = delete;
    bool beginSubmission() noexcept
    {
        if (not m_submissions.tryEnter()) { return false; }
        m_outstanding.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }
    void endSubmission() noexcept
    {
        m_submissions.leave();
    }
    void complete() noexcept
    {
        m_outstanding.fetch_sub(1, std::memory_order_acq_rel);
        m_signal_p->notifyAll();
    }
    void closeSubmissions() noexcept
    {
        m_submissions.close();
        m_submissions.wait();
        m_signal_p->notifyAll();
    }
    void requestStop() noexcept { m_executions.close(); m_signal_p->notifyAll(); }
    bool isStopRequested() const noexcept { return m_executions.isClosed(); }
    bool areSubmissionsClosed() const noexcept { return m_submissions.isClosed(); }
    bool beginExecution() noexcept { return m_executions.tryEnter(); }
    void endExecution() noexcept { m_executions.leave(); }
    void waitForExecutions() const noexcept { m_executions.wait(); }
    void reportErrorCode(std::error_code ec) noexcept
    {
        { const std::lock_guard guard {m_error_mutex}; if (not m_error_code) { m_error_code = ec; } }
        this->requestStop();
    }
    std::error_code getErrorCode() const noexcept { const std::lock_guard guard {m_error_mutex}; return m_error_code; }
    bool isEmpty() const noexcept { return m_outstanding.load(std::memory_order_acquire) == 0; }
    bool isFinished() const noexcept { return m_submissions.isClosedAndQuiescent() and this->isEmpty(); }
    observation_t observe() const noexcept { return m_signal_p->observe(); }
    void wait(observation_t observed) const noexcept { m_signal_p->wait(observed); }
private:
    Signal * m_signal_p;
    ActivityGate m_submissions;
    ActivityGate m_executions;
    std::atomic<std::uint64_t> m_outstanding {0};
    mutable std::mutex m_error_mutex;
    std::error_code m_error_code;
};
}
