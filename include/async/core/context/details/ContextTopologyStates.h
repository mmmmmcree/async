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
#include <async/core/worker/details/WorkSignal.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>
#include <utility>

namespace lcf::async::details {

using ContextOwnerQueue = queue::intrusive::mpmc::Queue<>;

class ContextTopologyNotifier
{
public:
    explicit ContextTopologyNotifier(WorkSignal & signal) noexcept : m_signal_p(&signal) {}
    ~ContextTopologyNotifier() noexcept = default;
    void operator()() const noexcept { m_signal_p->notifyAll(); }
private:
    WorkSignal * m_signal_p;
};

template <std::size_t queue_capacity,
    template <typename, typename, typename, typename> class CpuExecutor = WorkStealingExecutor,
    template <typename> class VictimSelector = RoundRobinVictimSelector,
    typename Notifier = ContextTopologyNotifier>
requires (queue_capacity > 0)
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
    ContextCpuState(std::span<Self> cpus, std::size_t index, Notifier notify) :
        m_executor(m_inbox, m_ready, Selector{cpus, index}, std::move(notify)) {}
    ~ContextCpuState() noexcept = default;
    ContextCpuState() = delete;
    std::error_code tryEnqueue(Value & executable) noexcept { return m_inbox.tryEnqueue(executable); }
    bool executeOne() noexcept { return m_executor.executeOne(); }
    bool isEmpty() const noexcept { return m_inbox.isEmpty() and m_ready.isEmpty(); }
private:
    Inbox m_inbox;
    ReadyQueue m_ready;
    Executor m_executor;
};

template <std::size_t queue_capacity, typename Notifier = ContextTopologyNotifier>
requires (queue_capacity > 0)
class ContextPortState
{
    using Node = queue::intrusive::ExecutionNode;
    using SubmitQueue = queue::non_intrusive::spsc::Queue<DispatchRequest, queue_capacity>;
    using ReturnQueue = queue::non_intrusive::spsc::Queue<Node, queue_capacity>;
    using Port = SubmissionPort<SubmitQueue, Notifier>;
public:
    ~ContextPortState() noexcept = default;
    ContextPortState() = delete;
    ContextPortState(
        std::uint32_t domain_id,
        std::uint32_t scheduler_count,
        Notifier & notify) noexcept :
        m_port(m_submissions, domain_id, scheduler_count, notify),
        m_executor(m_returns) {}
    auto & getPort() noexcept { return m_port; }
    DispatchRequest * tryPopSubmission() noexcept { return m_submissions.tryPop(); }
    bool areSubmissionsEmpty() const noexcept { return m_submissions.isEmpty(); }
    std::error_code tryEnqueueReturn(Node & executable) noexcept { return m_returns.tryEnqueue(executable); }
    bool executeReturn() noexcept { return m_executor.executeOne(); }
    bool isReturnEmpty() const noexcept { return m_returns.isEmpty(); }
    void close() noexcept { m_submissions.close(); }
private:
    SubmitQueue m_submissions;
    ReturnQueue m_returns;
    Port m_port;
    BasicExecutor<ReturnQueue> m_executor;
};

} // namespace lcf::async::details
