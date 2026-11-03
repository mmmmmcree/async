#pragma once

#include "ContextInfo.h"
#include "poll_structs.h"
#include <async/core/context/details/ContextEventLoop.h>
#include <async/core/context/details/ExecutorSet.h>
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/scheduling/BasicExecutor.h>
#include <async/core/scheduling/BasicWorker.h>
#include <async/core/scheduling/SchedulerHandle.h>

#include <cassert>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory_resource>
#include <thread>
#include <utility>

namespace lcf::async {

template <multi_worker_executor_c Executor = BasicExecutor<>,
    join_handle_c ThreadHandle = std::thread, work_executor_c LoopExecutor = BasicExecutor<>>
class BasicContext
{
    using SharedExecutor = details::NotifiedExecutor<Executor>;
    using SharedExecutors = details::ExecutorSet<Executor>;
    using EventLoop = details::ContextEventLoop<LoopExecutor, SharedExecutors>;
    using Worker = BasicWorker<SharedExecutors, ThreadHandle>;
    using ExecutorList = details::FixedCapacityVector<SharedExecutor>;
    using WorkerList = details::FixedCapacityVector<Worker>;
public:
    ~BasicContext() noexcept
    {
        assert(not m_workers_running and "Context workers must be joined before destruction");
        assert(this->isEmpty() and "Context must be drained before destruction");
    }
    BasicContext() = delete;
    template <typename ExecutorFactory = DefaultContextExecutorFactory<Executor>,
        typename LoopExecutorFactory = DefaultContextExecutorFactory<LoopExecutor>>
        requires context_executor_factory_c<ExecutorFactory, Executor>
            and context_executor_factory_c<LoopExecutorFactory, LoopExecutor>
    explicit BasicContext(ContextInfo context_info, ContextAllocators allocators,
        ExecutorFactory executor_factory = {}, LoopExecutorFactory loop_executor_factory = {}) :
        m_domain_id(validate_info(context_info)), m_worker_count(context_info.getWorkerCount()),
        m_executors(make_executors(context_info.getSchedulerCount(), allocators.m_executors,
            executor_factory, m_worker_signal, m_loop_signal)),
        m_shared_executors(m_executors.values(), m_worker_signal),
        m_event_loop(loop_executor_factory, m_shared_executors, m_loop_signal),
        m_workers(m_worker_count, std::pmr::polymorphic_allocator<Worker>{allocators.m_workers}) {}
    BasicContext(const BasicContext &) = delete;
    BasicContext(BasicContext &&) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    BasicContext & operator=(BasicContext &&) = delete;
public:
    static constexpr ContextStorageRequirements storageRequirements(
        WorkerCount worker_count, std::size_t scheduler_count) noexcept
    {
        return {storage_requirement<SharedExecutor>(scheduler_count), storage_requirement<Worker>(worker_count)};
    }
    static ContextStorageRequirements storageRequirements(const ContextInfo & info) noexcept
    {
        return storageRequirements(info.getWorkerCount(), info.getSchedulerCount());
    }
public:
    SchedulerHandle<SharedExecutor> getSchedulerHandle(SchedulerToken token) const noexcept
    {
        assert(token.getDomainId() == m_domain_id and "Scheduler token belongs to a different Context");
        return SchedulerHandle<SharedExecutor>{m_executors[token.getIndex()], token};
    }
    auto getEventLoopSchedulerHandle() const noexcept
    {
        return SchedulerHandle{m_event_loop.getExecutor()};
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void startWorkers(Launcher thread_launcher = {})
    {
        if (m_workers_started) { return; }
        m_workers_started = true;
        try {
            for (WorkerCount index = 0; index < m_worker_count; ++index) {
                m_workers.emplaceBack(m_shared_executors, thread_launcher);
            }
        } catch (...) {
            this->requestStop();
            this->joinWorkers();
            throw;
        }
        m_workers_running = m_worker_count != 0;
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void run(Launcher thread_launcher = {})
    {
        this->startWorkers(std::move(thread_launcher));
        m_event_loop.enter();
        while (m_event_loop.waitAndExecuteOne(not m_workers_running)) {}
        m_event_loop.leave();
    }
    bool pollOne() noexcept
    {
        m_event_loop.enter();
        const bool executed = m_event_loop.executeOne(not m_workers_running);
        m_event_loop.leave();
        return executed;
    }
    PollReport poll(PollBudget budget = {}) noexcept
    {
        using Clock = std::chrono::steady_clock;
        m_event_loop.enter();
        const auto started = Clock::now();
        const bool finite_time = budget.m_max_duration != Clock::duration::max();
        PollReport report;
        while (report.m_records < budget.m_max_records) {
            if (finite_time and Clock::now() - started >= budget.m_max_duration) { break; }
            if (not m_event_loop.executeOne(not m_workers_running)) { break; }
            ++report.m_records;
        }
        report.m_elapsed = Clock::now() - started;
        report.m_count_limit_reached = report.m_records == budget.m_max_records;
        report.m_time_limit_reached = finite_time and report.m_elapsed >= budget.m_max_duration;
        m_event_loop.leave();
        return report;
    }
    void requestStop() noexcept
    {
        m_shared_executors.requestStop();
        m_event_loop.requestStop();
    }
    void close() noexcept
    {
        m_shared_executors.close();
        m_event_loop.close();
    }
    void joinWorkers() noexcept
    {
        for (auto & worker : m_workers.values()) { worker.join(); }
        m_workers_running = false;
    }
    bool isEmpty() const noexcept { return m_event_loop.isEmpty() and m_shared_executors.isEmpty(); }
private:
    static std::uint32_t validate_info(const ContextInfo & info) noexcept
    {
        assert(info.getDomainId() != 0 and "ContextInfo was already consumed");
        return info.getDomainId();
    }
    template <context_executor_factory_c<Executor> Factory>
    static ExecutorList make_executors(std::size_t count, ContextAllocators::Allocator allocator,
        Factory & factory, details::WorkSignal & worker_signal, details::WorkSignal & loop_signal)
    {
        ExecutorList executors{count, std::pmr::polymorphic_allocator<SharedExecutor>{allocator}};
        for (std::size_t index = 0; index < count; ++index) {
            executors.emplaceBack(factory, worker_signal, loop_signal);
        }
        return executors;
    }
    template <std::destructible Value>
    static constexpr StorageRequirement storage_requirement(std::size_t count) noexcept
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value)) { std::terminate(); }
        return {count * sizeof(Value), alignof(Value)};
    }
private:
    std::uint32_t m_domain_id;
    WorkerCount m_worker_count;
    details::WorkSignal m_worker_signal;
    details::WorkSignal m_loop_signal;
    mutable ExecutorList m_executors;
    SharedExecutors m_shared_executors;
    mutable EventLoop m_event_loop;
    WorkerList m_workers;
    bool m_workers_started = false;
    bool m_workers_running = false;
};

using Context = BasicContext<>;

} // namespace lcf::async
