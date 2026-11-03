#pragma once

#include "ContextInfo.h"
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/scheduling/poll_structs.h>
#include <async/core/scheduling/BasicExecutor.h>
#include <async/core/scheduling/BasicWorker.h>
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
        if (not executor.executeOne()) { break; }
        ++report.m_records;
    }
    report.m_elapsed = Clock::now() - started;
    report.m_count_limit_reached = report.m_records == budget.m_max_records;
    report.m_time_limit_reached = finite_time && report.m_elapsed >= budget.m_max_duration;
    return report;
}

} // namespace details

template <multi_worker_executor_c Executor = BasicExecutor<>,
    join_handle_c ThreadHandle = std::thread>
class BasicContext
{
public:
    using Worker = BasicWorker<Executor, ThreadHandle>;
    using Queue = LogicalQueue<Executor>;
public:
    ~BasicContext() noexcept { assert(this->empty()); }
    BasicContext() = delete;
    template <typename ExecutorFactory = DefaultContextExecutorFactory<Executor>,
        thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
        requires context_executor_factory_c<ExecutorFactory, Executor>
    explicit BasicContext(ContextInfo context_info, ContextAllocators allocators,
        ExecutorFactory executor_factory = {}, Launcher thread_launcher = {})
        : m_domain_id(validate_info(context_info)),
          m_executor(executor_factory.makeExecutor()),
          m_workers(context_info.getWorkerCount(),
              std::pmr::polymorphic_allocator<Worker>{allocators.m_workers}),
          m_queues(context_info.getQueueCount(),
              std::pmr::polymorphic_allocator<Queue>{allocators.m_routes})
    {
        for (std::size_t index = 0; index < context_info.getQueueCount(); ++index) {
            m_queues.emplaceBack(m_executor);
        }
        try {
            for (WorkerCount index = 0; index < context_info.getWorkerCount(); ++index) {
                m_workers.emplaceBack(m_executor, thread_launcher);
            }
        } catch (...) {
            this->close();
            this->joinWorkers();
            throw;
        }
    }

    BasicContext(const BasicContext &) = delete;
    BasicContext(BasicContext &&) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    BasicContext & operator=(BasicContext &&) = delete;
public:
    static constexpr ContextStorageRequirements storageRequirements(
        WorkerCount worker_count, std::size_t queue_count) noexcept
    {
        return {storage_requirement<Worker>(worker_count),
            storage_requirement<Queue>(queue_count)};
    }

    static ContextStorageRequirements storageRequirements(const ContextInfo & info) noexcept
    {
        return storageRequirements(info.getWorkerCount(), info.getQueueCount());
    }

    SchedulerHandle<Executor> getSchedulerHandle(LogicalQueueToken token) const noexcept
    {
        return SchedulerHandle<Executor>{this->findLogicalQueue(token)};
    }

    const Queue & findLogicalQueue(LogicalQueueToken token) const noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return m_queues[token.getIndex()];
    }

    bool pollOne() noexcept { return m_executor.executeOne(); }
    PollReport poll(PollBudget budget = {}) noexcept
    {
        return details::poll_executor(m_executor, budget);
    }
    void run() noexcept requires stoppable_work_executor_c<Executor>
    {
        while (m_executor.waitAndExecuteOne()) {}
    }
    void requestStop() noexcept requires stoppable_work_executor_c<Executor>
    {
        m_executor.requestStop();
    }
    void close() noexcept
    {
        m_executor.close();
    }
    void joinWorkers() noexcept
    {
        for (auto & worker : m_workers.values()) { worker.join(); }
    }
    bool empty() const noexcept
    {
        return m_executor.empty();
    }

private:
    static std::uint32_t validate_info(const ContextInfo & info)
    {
        if (info.getDomainId() == 0 || info.getQueueCount() == 0) {
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
    Executor m_executor;
    details::FixedCapacityVector<Worker> m_workers;
    details::FixedCapacityVector<Queue> m_queues;
};

using Context = BasicContext<>;

} // namespace lcf::async
