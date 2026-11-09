#pragma once

#include <async/core/executable/declarations/concepts.h>
#include <async/core/queue/non_intrusive/spsc/Queue.h>
#include <async/core/scheduling/dispatcher/DispatchRequest.h>
#include <async/core/scheduling/executor/BasicExecutor.h>
#include <async/core/notify/declarations/concepts.h>

#include <cstddef>
#include <system_error>

namespace lcf::async::details {

template <std::size_t queue_capacity, typename Notifier, cancellable_executable_c Executable>
requires (queue_capacity > 0) and notify::notifier_c<Notifier>
class ContextPort
{
public:
    using DispatchRequest = ::lcf::async::DispatchRequest<Executable>;
private:
    using SubmissionQueue = queue::non_intrusive::spsc::Queue<DispatchRequest, queue_capacity>;
    using ExecutionQueue = queue::non_intrusive::spsc::Queue<Executable, queue_capacity>;
public:
    ~ContextPort() noexcept = default;
    ContextPort() = delete;
    explicit ContextPort(Notifier & notifier) noexcept :
        m_notifier_p(&notifier), m_executor(m_execution_queue) {}
    ContextPort(const ContextPort &) = delete;
    ContextPort(ContextPort &&) = delete;
    ContextPort & operator=(const ContextPort &) = delete;
    ContextPort & operator=(ContextPort &&) = delete;
public:
    std::error_code submit(DispatchRequest & request) noexcept
    {
        const auto error = m_submission_queue.tryEnqueue(request);
        if (not error) { m_notifier_p->notifyAll(); }
        return error;
    }
    DispatchRequest * tryPopSubmission() noexcept { return m_submission_queue.tryPop(); }
    bool areSubmissionsEmpty() const noexcept { return m_submission_queue.isEmpty(); }
    void closeSubmissions() noexcept { m_submission_queue.close(); }
    std::error_code tryEnqueueExecution(Executable & executable) noexcept { return m_execution_queue.tryEnqueue(executable); }
    bool executeOne() noexcept { return m_executor.executeOne(); }
    bool areExecutionsEmpty() const noexcept { return m_execution_queue.isEmpty(); }
    void cancelPendingExecutions() noexcept
    {
        while (auto * executable_p = m_execution_queue.tryPop()) { executable_p->cancel(); }
    }
private:
    SubmissionQueue m_submission_queue;
    ExecutionQueue m_execution_queue;
    Notifier * m_notifier_p;
    BasicExecutor<ExecutionQueue> m_executor;
};

} // namespace lcf::async::details
