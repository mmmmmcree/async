#pragma once

#include <async/core/queue/ExecutionNode.h>
#include <async/core/queue/non_intrusive/spsc/Queue.h>
#include <async/core/scheduling/declarations/dispatch_structs.h>
#include <async/core/scheduling/SubmissionPort.h>
#include <async/core/scheduling/executor/BasicExecutor.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/notify/declarations/concepts.h>

#include <cstddef>
#include <cstdint>
#include <system_error>

namespace lcf::async::details {

template <std::size_t queue_capacity, typename Notifier = notify::SignalNotifier<>>
requires (queue_capacity > 0) and notify::notifier_c<Notifier>
class ContextPort
{
    using Executable = queue::ExecutionNode;
    using SubmissionQueue = queue::non_intrusive::spsc::Queue<DispatchRequest, queue_capacity>;
    using ExecutionQueue = queue::non_intrusive::spsc::Queue<Executable, queue_capacity>;
    using Port = SubmissionPort<SubmissionQueue, Notifier>;
public:
    ~ContextPort() noexcept = default;
    ContextPort() = delete;
    ContextPort(std::uint32_t domain_id, Notifier & notifier) noexcept :
        m_submission_port(m_submission_queue, domain_id, notifier), m_executor(m_execution_queue) {}
    ContextPort(const ContextPort &) = delete;
    ContextPort(ContextPort &&) = delete;
    ContextPort & operator=(const ContextPort &) = delete;
    ContextPort & operator=(ContextPort &&) = delete;
public:
    Port & getSubmissionPort() noexcept { return m_submission_port; }
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
    Port m_submission_port;
    BasicExecutor<ExecutionQueue> m_executor;
};

} // namespace lcf::async::details
