#pragma once

#include <async/core/operations.h>
#include <async/core/scheduling/queue/MutexInbox.h>

#include <cassert>
#include <concepts>
#include <utility>

namespace lcf::async {

template <schedule_queue_c Queue = MutexInbox>
class BasicExecutor
{
public:
    static constexpr bool supports_multiple_producers = [] {
        if constexpr (requires { Queue::supports_multiple_producers; }) {
            return Queue::supports_multiple_producers;
        } else {
            return false;
        }
    }();

    static constexpr bool supports_multiple_workers = [] {
        if constexpr (requires { Queue::supports_multiple_consumers; }) {
            return Queue::supports_multiple_consumers;
        } else {
            return false;
        }
    }();

    template <typename... QueueArgs>
        requires std::constructible_from<Queue, QueueArgs...>
    explicit BasicExecutor(QueueArgs &&... queue_args)
        : m_queue(std::forward<QueueArgs>(queue_args)...) {}

    BasicExecutor(const BasicExecutor &) = delete;
    BasicExecutor & operator=(const BasicExecutor &) = delete;

    ~BasicExecutor() noexcept { assert(m_queue.empty()); }

    template <schedule_record_c Record>
        requires requires(Queue & queue, Record & record) {
            { queue.tryEnqueue(get_schedule_node(record)) } noexcept
                -> std::same_as<EnqueueResult>;
        }
    EnqueueResult tryEnqueue(Record & record) noexcept
    {
        return m_queue.tryEnqueue(get_schedule_node(record));
    }

    bool pollOne() noexcept
    {
        if (auto * node_p = m_queue.tryPop()) {
            node_p->execute();
            return true;
        }
        return false;
    }

    bool waitOne() noexcept
        requires blocking_schedule_queue_c<Queue>
    {
        if (auto * node_p = m_queue.waitPop()) {
            node_p->execute();
            return true;
        }
        return false;
    }

    void requestStop() noexcept
        requires stoppable_schedule_queue_c<Queue>
    {
        m_queue.requestStop();
    }

    void close() noexcept { m_queue.close(); }
    bool empty() const noexcept { return m_queue.empty(); }

private:
    Queue m_queue;
};

template <work_executor_c ControlExecutor, work_executor_c CpuExecutor = ControlExecutor>
struct DefaultExecutorFactory
{
    ControlExecutor makeControlExecutor() const
        requires std::default_initializable<ControlExecutor>
    {
        return ControlExecutor{};
    }

    CpuExecutor makeCpuExecutor() const requires std::default_initializable<CpuExecutor>
    {
        return CpuExecutor{};
    }
};

} // namespace lcf::async
