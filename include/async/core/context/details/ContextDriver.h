#pragma once

#include <async/core/context/declarations/poll_structs.h>
#include <async/core/event_loop/BasicEventLoop.h>
#include <async/core/worker/WorkerPool.h>
#include <async/core/worker/details/OwnerThreadBinding.h>

#include <chrono>

namespace lcf::async::details {

template <multi_worker_executor_c Executor, join_handle_c ThreadHandle, work_executor_c LoopExecutor>
class ContextDriver
{
    using Pool = WorkerPool<Executor, ThreadHandle>;
    using EventLoop = BasicEventLoop<LoopExecutor>;
    using HostDriver = typename Pool::Driver;
public:
    ~ContextDriver() noexcept = default;
    ContextDriver() noexcept = default;
    ContextDriver(const ContextDriver &) = delete;
    ContextDriver(ContextDriver &&) = delete;
    ContextDriver & operator=(const ContextDriver &) = delete;
    ContextDriver & operator=(ContextDriver &&) = delete;
public:
    void create(EventLoop & event_loop, Pool & worker_pool) noexcept
    {
        m_event_loop_p = &event_loop;
        m_worker_pool_p = &worker_pool;
        worker_pool.bindDriver(m_host_driver);
    }
    void run() noexcept
    {
        m_owner_binding.enter();
        for (;;) {
            const auto observed = m_event_loop_p->observe();
            if (m_event_loop_p->isStopRequested()) { break; }
            if (this->executeOne()) { continue; }
            if (m_event_loop_p->isClosed() and m_event_loop_p->isEmpty()
                and (m_worker_pool_p->isRunning() or m_worker_pool_p->isEmpty())) { break; }
            m_event_loop_p->wait(observed);
        }
        m_owner_binding.leave();
    }
    bool pollOne() noexcept
    {
        m_owner_binding.enter();
        const bool executed = this->executeOne();
        m_owner_binding.leave();
        return executed;
    }
    PollReport poll(PollBudget budget) noexcept
    {
        using Clock = std::chrono::steady_clock;
        m_owner_binding.enter();
        const auto started = Clock::now();
        const bool finite_time = budget.m_max_duration != Clock::duration::max();
        PollReport report;
        while (report.m_records < budget.m_max_records) {
            if (finite_time and Clock::now() - started >= budget.m_max_duration) { break; }
            if (not this->executeOne()) { break; }
            ++report.m_records;
        }
        report.m_elapsed = Clock::now() - started;
        report.m_count_limit_reached = report.m_records == budget.m_max_records;
        report.m_time_limit_reached = finite_time and report.m_elapsed >= budget.m_max_duration;
        m_owner_binding.leave();
        return report;
    }
private:
    bool executeOne() noexcept
    {
        const bool include_shared = not m_worker_pool_p->isRunning();
        const bool shared_first = include_shared and m_shared_first;
        m_shared_first = not m_shared_first;
        if (shared_first and m_host_driver.executeOne()) { return true; }
        if (m_event_loop_p->executeOne()) { return true; }
        return include_shared and not shared_first and m_host_driver.executeOne();
    }
private:
    EventLoop * m_event_loop_p = nullptr;
    Pool * m_worker_pool_p = nullptr;
    HostDriver m_host_driver;
    OwnerThreadBinding m_owner_binding;
    bool m_shared_first = false;
};

} // namespace lcf::async::details
