#pragma once

#include <async/core/concepts.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>

#include <cassert>
#include <concepts>
#include <utility>

namespace lcf::async {

template <work_queue_c Queue = queue::intrusive::mpmc::Queue<>>
class BasicExecutor
{
public:
    static constexpr bool k_supports_multiple_producers = Queue::k_supports_multiple_producers;
    static constexpr bool k_supports_multiple_workers = Queue::k_supports_multiple_consumers;
public:
    ~BasicExecutor() noexcept { assert(m_queue.empty()); }
    BasicExecutor() requires std::default_initializable<Queue> = default;
    template <typename... QueueArgs>
        requires (sizeof...(QueueArgs) > 0) and std::constructible_from<Queue, QueueArgs...>
    explicit BasicExecutor(QueueArgs &&... queue_args)
        : m_queue(std::forward<QueueArgs>(queue_args)...) {}

    BasicExecutor(const BasicExecutor &) = delete;
    BasicExecutor(BasicExecutor &&) = delete;
    BasicExecutor & operator=(const BasicExecutor &) = delete;
    BasicExecutor & operator=(BasicExecutor &&) = delete;
public:
    template <work_record_c Record>
    requires requires(Queue & queue, Record & record) {
        { queue.tryEnqueue(record.getExecutable()) } noexcept -> std::same_as<EnqueueResult>;
    }
    EnqueueResult tryEnqueue(Record & record) noexcept
    {
        return m_queue.tryEnqueue(record.getExecutable());
    }
    bool executeOne() noexcept
    {
        if (auto * executable_p = m_queue.tryPop()) {
            executable_p->execute();
            return true;
        }
        return false;
    }
    bool waitAndExecuteOne() noexcept
    requires waiting_work_queue_c<Queue>
    {
        if (auto * executable_p = m_queue.waitPop()) {
            executable_p->execute();
            return true;
        }
        return false;
    }
    void requestStop() noexcept
        requires stoppable_work_queue_c<Queue>
    {
        m_queue.requestStop();
    }
    void close() noexcept { m_queue.close(); }
    bool empty() const noexcept { return m_queue.empty(); }
private:
    Queue m_queue;
};

template <work_executor_c Executor>
struct DefaultContextExecutorFactory
{
    ~DefaultContextExecutorFactory() noexcept = default;
    DefaultContextExecutorFactory() noexcept = default;
    DefaultContextExecutorFactory(const DefaultContextExecutorFactory &) noexcept = default;
    DefaultContextExecutorFactory(DefaultContextExecutorFactory &&) noexcept = default;
    DefaultContextExecutorFactory & operator=(const DefaultContextExecutorFactory &) noexcept = default;
    DefaultContextExecutorFactory & operator=(DefaultContextExecutorFactory &&) noexcept = default;
public:
    Executor makeExecutor() const requires std::default_initializable<Executor>
    {
        return Executor{};
    }
};

} // namespace lcf::async
