#pragma once

#include <async/core/operations.h>
#include <async/core/scheduling/LogicalQueue.h>

namespace lcf::async::details {

template <work_executor_c ControlExecutor, multi_worker_executor_c CpuExecutor>
class ContextExecutorRoute
{
public:
    ContextExecutorRoute(
        ControlExecutor & control_executor,
        CpuExecutor & cpu_executor,
        bool is_cpu) noexcept
        : m_control_executor_p(&control_executor),
          m_cpu_executor_p(&cpu_executor),
          m_is_cpu(is_cpu) {}

    template <typename Record>
        requires executor_c<ControlExecutor, Record>
            and executor_c<CpuExecutor, Record>
    EnqueueResult tryEnqueue(Record & record) const noexcept
    {
        if (m_is_cpu) {
            return try_enqueue(*m_cpu_executor_p, record);
        }
        return try_enqueue(*m_control_executor_p, record);
    }

private:
    ControlExecutor * m_control_executor_p;
    CpuExecutor * m_cpu_executor_p;
    bool m_is_cpu;
};

template <work_executor_c ControlExecutor, multi_worker_executor_c CpuExecutor>
class ContextLogicalQueue
{
public:
    using Route = ContextExecutorRoute<ControlExecutor, CpuExecutor>;

    ContextLogicalQueue(
        ControlExecutor & control_executor,
        CpuExecutor & cpu_executor,
        bool is_cpu) noexcept
        : m_route(control_executor, cpu_executor, is_cpu), m_queue(m_route) {}

    const LogicalQueue<Route> & getLogicalQueue() const noexcept { return m_queue; }

private:
    Route m_route;
    LogicalQueue<Route> m_queue;
};

} // namespace lcf::async::details
