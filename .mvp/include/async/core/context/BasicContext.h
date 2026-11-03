#pragma once

#include "ContextInfo.h"
#include <async/core/operations.h>
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/scheduling/poll_structs.h>
#include <async/core/scheduling/BasicExecutor.h>
#include <async/core/scheduling/BasicWorker.h>
#include <async/core/scheduling/details/ContextExecutorRoute.h>
#include <async/core/scheduling/LogicalQueue.h>
#include <async/core/scheduling/SchedulerHandle.h>

#include <cassert>
#include <chrono>
#include <concepts>
#include <exception>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <stdexcept>
#include <thread>
#include <utility>

namespace lcf::async {

namespace details {

template <work_executor_c Executor>
PollReport poll_executor(Executor & executor, PollBudget budget = {}) noexcept
{
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const bool finite_time = budget.m_max_duration != Clock::duration::max();
    PollReport report;
    while (report.m_records < budget.m_max_records) {
        if (finite_time && Clock::now() - started >= budget.m_max_duration) { break; }
        if (not executor.pollOne()) { break; }
        ++report.m_records;
    }
    report.m_elapsed = Clock::now() - started;
    report.m_count_limit_reached = report.m_records == budget.m_max_records;
    report.m_time_limit_reached = finite_time && report.m_elapsed >= budget.m_max_duration;
    return report;
}

} // namespace details

template <work_executor_c ControlExecutor = BasicExecutor<>,
    multi_worker_executor_c CpuExecutor = BasicExecutor<>,
    thread_c Thread = std::thread>
class BasicContext
{
public:
    using Worker = BasicWorker<CpuExecutor, Thread>;
    using Route = details::ContextExecutorRoute<ControlExecutor, CpuExecutor>;
    using RouteBinding = details::ContextLogicalQueue<ControlExecutor, CpuExecutor>;

    template <typename ExecutorFactory = DefaultExecutorFactory<ControlExecutor, CpuExecutor>,
        thread_factory_c<Thread> ThreadFactory = StdThreadFactory>
        requires work_executor_factory_c<ExecutorFactory, ControlExecutor, CpuExecutor>
    explicit BasicContext(ContextInfo context_info, ContextAllocators allocators,
        ExecutorFactory executor_factory = {}, ThreadFactory thread_factory = {})
        : m_domain_id(validate_info(context_info)),
          m_control_executor(executor_factory.makeControlExecutor()),
          m_cpu_executor(executor_factory.makeCpuExecutor()),
          m_workers(context_info.getWorkerCount(),
              std::pmr::polymorphic_allocator<Worker>{allocators.m_workers}),
          m_queues(context_info.getQueueBindings().size(),
              std::pmr::polymorphic_allocator<RouteBinding>{allocators.m_routes})
    {
        for (const auto & binding : context_info.getQueueBindings()) {
            m_queues.emplaceBack(m_control_executor, m_cpu_executor,
                binding.m_target == QueueTarget::eCpu);
        }
        try {
            for (WorkerCount index = 0; index < context_info.getWorkerCount(); ++index) {
                m_workers.emplaceBack(m_cpu_executor, thread_factory);
            }
        } catch (...) {
            this->close();
            this->joinWorkers();
            throw;
        }
    }

    BasicContext(const BasicContext &) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    ~BasicContext() noexcept { assert(this->empty()); }

    static constexpr ContextStorageRequirements storageRequirements(
        WorkerCount worker_count, std::size_t queue_count) noexcept
    {
        return {storage_requirement<Worker>(worker_count),
            storage_requirement<RouteBinding>(queue_count)};
    }

    static ContextStorageRequirements storageRequirements(const ContextInfo & info) noexcept
    {
        return storageRequirements(info.getWorkerCount(), info.getQueueBindings().size());
    }

    SchedulerHandle<Route> getSchedulerHandle(LogicalQueueToken token) const noexcept
    {
        return SchedulerHandle<Route>{this->findLogicalQueue(token)};
    }

    const LogicalQueue<Route> & findLogicalQueue(LogicalQueueToken token) const noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return m_queues[token.getIndex()].getLogicalQueue();
    }

    bool pollOne() noexcept { return m_control_executor.pollOne(); }
    PollReport poll(PollBudget budget = {}) noexcept
    {
        return details::poll_executor(m_control_executor, budget);
    }
    void run() noexcept requires stoppable_work_executor_c<ControlExecutor>
    {
        while (m_control_executor.waitOne()) {}
    }
    void requestStop() noexcept requires stoppable_work_executor_c<ControlExecutor>
    {
        m_control_executor.requestStop();
    }
    void close() noexcept
    {
        m_control_executor.close();
        m_cpu_executor.close();
    }
    void joinWorkers() noexcept
    {
        for (auto & worker : m_workers.values()) { worker.join(); }
    }
    bool empty() const noexcept
    {
        return m_control_executor.empty() && m_cpu_executor.empty();
    }

private:
    static std::uint32_t validate_info(const ContextInfo & info)
    {
        if (info.getDomainId() == 0 || info.getQueueBindings().empty()) {
            throw std::invalid_argument{"Context requires an unconsumed configuration with routes"};
        }
        return info.getDomainId();
    }

    template <std::destructible Value>
    static constexpr StorageRequirement storage_requirement(std::size_t count) noexcept
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
            std::terminate();
        }
        return {count * sizeof(Value), alignof(Value)};
    }

    std::uint32_t m_domain_id;
    ControlExecutor m_control_executor;
    CpuExecutor m_cpu_executor;
    details::FixedCapacityVector<Worker> m_workers;
    details::FixedCapacityVector<RouteBinding> m_queues;
};

using Context = BasicContext<>;

} // namespace lcf::async
