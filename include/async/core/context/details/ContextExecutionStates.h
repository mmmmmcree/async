#pragma once

#include <async/core/queue/intrusive/ExecutionNode.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/queue/non_intrusive/spsc/Queue.h>
#include <async/core/queue/non_intrusive/spmc/Deque.h>
#include <async/core/scheduling/declarations/dispatch_structs.h>
#include <async/core/scheduling/SubmissionPort.h>
#include <async/core/scheduling/executor/BasicExecutor.h>
#include <async/core/scheduling/executor/WorkStealingExecutor.h>
#include <async/core/scheduling/executor/selectors/RoundRobinVictimSelector.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/notify/declarations/concepts.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>
#include <utility>

namespace lcf::async::details {

using ContextOwnerQueue = queue::intrusive::mpmc::Queue<>;

template <std::size_t queue_capacity,
    template <typename, typename, typename, typename> class CpuExecutor = WorkStealingExecutor,
    template <typename> class VictimSelector = RoundRobinVictimSelector,
    typename Notifier = notify::SignalNotifier<>>
requires (queue_capacity > 0) and notify::notifier_c<Notifier>
class ContextCpuState
{
public:
    using Value = queue::intrusive::ExecutionNode;
    Value * trySteal() noexcept
    {
        if (auto * executable_p = m_ready.trySteal()) { return executable_p; }
        return m_inbox.tryPop();
    }
private:
    using Inbox = ContextOwnerQueue;
    using ReadyQueue = queue::non_intrusive::spmc::Deque<queue::intrusive::ExecutionNode, queue_capacity>;
    using Self = ContextCpuState<queue_capacity, CpuExecutor, VictimSelector, Notifier>;
    using Selector = VictimSelector<Self>;
    using Executor = CpuExecutor<Inbox, ReadyQueue, Selector, Notifier>;
public:
    ContextCpuState(std::span<Self> cpus, std::size_t index, Notifier notifier) :
        m_executor(m_inbox, m_ready, Selector{cpus, index}, std::move(notifier)) {}
    ~ContextCpuState() noexcept = default;
    ContextCpuState() = delete;
    ContextCpuState(const ContextCpuState &) = delete;
    ContextCpuState(ContextCpuState &&) = delete;
    ContextCpuState & operator=(const ContextCpuState &) = delete;
    ContextCpuState & operator=(ContextCpuState &&) = delete;
    std::error_code tryEnqueue(Value & executable) noexcept { return m_inbox.tryEnqueue(executable); }
    bool executeOne() noexcept { return m_executor.executeOne(); }
    bool isEmpty() const noexcept { return m_inbox.isEmpty() and m_ready.isEmpty(); }
    void cancelPending() noexcept
    {
        while (auto * executable_p = this->trySteal()) { executable_p->cancel(); }
    }
private:
    Inbox m_inbox;
    ReadyQueue m_ready;
    Executor m_executor;
};

template <std::size_t queue_capacity, typename Notifier = notify::SignalNotifier<>>
requires (queue_capacity > 0) and notify::notifier_c<Notifier>
class ContextPortState
{
    using Node = queue::intrusive::ExecutionNode;
    using SubmitQueue = queue::non_intrusive::spsc::Queue<DispatchRequest, queue_capacity>;
    using ExecutionQueue = queue::non_intrusive::spsc::Queue<Node, queue_capacity>;
    using Port = SubmissionPort<SubmitQueue, Notifier>;
public:
    ~ContextPortState() noexcept = default;
    ContextPortState() = delete;
    ContextPortState(
        std::uint32_t domain_id,
        Notifier & notifier) noexcept :
        m_port(m_submissions, domain_id, notifier),
        m_executor(m_executions) {}
    ContextPortState(const ContextPortState &) = delete;
    ContextPortState(ContextPortState &&) = delete;
    ContextPortState & operator=(const ContextPortState &) = delete;
    ContextPortState & operator=(ContextPortState &&) = delete;
    auto & getPort() noexcept { return m_port; }
    DispatchRequest * tryPopSubmission() noexcept { return m_submissions.tryPop(); }
    bool areSubmissionsEmpty() const noexcept { return m_submissions.isEmpty(); }
    std::error_code tryEnqueueExecution(Node & executable) noexcept { return m_executions.tryEnqueue(executable); }
    bool executeOne() noexcept { return m_executor.executeOne(); }
    bool isExecutionEmpty() const noexcept { return m_executions.isEmpty(); }
    void closeSubmissions() noexcept { m_submissions.close(); }
    void cancelPending() noexcept
    {
        while (auto * executable_p = m_executions.tryPop()) { executable_p->cancel(); }
    }
private:
    SubmitQueue m_submissions;
    ExecutionQueue m_executions;
    Port m_port;
    BasicExecutor<ExecutionQueue> m_executor;
};

} // namespace lcf::async::details
