#pragma once

#include <async/core/executable/declarations/concepts.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/queue/non_intrusive/spmc/Deque.h>
#include <async/core/notify/declarations/concepts.h>

#include <cstddef>
#include <span>
#include <system_error>
#include <utility>

namespace lcf::async::details {

template <std::size_t queue_capacity,
    template <typename, typename, typename, typename> class ExecutorPolicy,
    template <typename> class VictimSelector, typename Notifier,
    cancellable_executable_c Executable>
requires (queue_capacity > 0) and notify::notifier_c<Notifier> and linked_node_c<Executable>
class ContextExecutor
{
public:
    using Value = Executable;
    Value * trySteal() noexcept
    {
        if (auto * executable_p = m_ready_queue.trySteal()) { return executable_p; }
        return m_inbox.tryPop();
    }
private:
    using Inbox = queue::intrusive::mpmc::Queue<Value>;
    using ReadyQueue = queue::non_intrusive::spmc::Deque<Value, queue_capacity>;
    using Self = ContextExecutor<queue_capacity, ExecutorPolicy, VictimSelector, Notifier, Executable>;
    using Selector = VictimSelector<Self>;
    using Executor = ExecutorPolicy<Inbox, ReadyQueue, Selector, Notifier>;
public:
    ~ContextExecutor() noexcept = default;
    ContextExecutor() = delete;
    ContextExecutor(std::span<Self> executors, std::size_t executor_index, Notifier notifier) :
        m_executor(m_inbox, m_ready_queue, Selector {executors, executor_index}, std::move(notifier)) {}
    ContextExecutor(const ContextExecutor &) = delete;
    ContextExecutor(ContextExecutor &&) = delete;
    ContextExecutor & operator=(const ContextExecutor &) = delete;
    ContextExecutor & operator=(ContextExecutor &&) = delete;
public:
    std::error_code tryEnqueue(Value & executable) noexcept { return m_inbox.tryEnqueue(executable); }
    bool executeOne() noexcept { return m_executor.executeOne(); }
    bool isEmpty() const noexcept { return m_inbox.isEmpty() and m_ready_queue.isEmpty(); }
    void cancelPending() noexcept
    {
        while (auto * executable_p = this->trySteal()) { executable_p->cancel(); }
    }
private:
    Inbox m_inbox;
    ReadyQueue m_ready_queue;
    Executor m_executor;
};

} // namespace lcf::async::details
