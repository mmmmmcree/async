#pragma once

#include "ContextInfo.h"
#include <async/core/context/declarations/poll_structs.h>
#include <async/core/notify/AtomicSequenceSignal.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/context/details/OwnerThreadState.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <join_handle_c ThreadHandle = std::thread, std::size_t queue_capacity = 256,
    template <typename, typename, typename, typename> class Executor = WorkStealingExecutor,
    template <typename> class VictimSelector = RoundRobinVictimSelector,
    notify::signal_c Signal = notify::AtomicSequenceSignal,
    typename Notifier = notify::SignalNotifier<Signal>>
requires (queue_capacity > 0) and
    std::is_nothrow_default_constructible_v<Signal> and
    notify::signal_notifier_c<Notifier, Signal>
class BasicContext
{
private:
    using SchedulingRuntime = details::ContextSchedulingRuntime<queue_capacity, Executor, VictimSelector, Signal, Notifier>;
    using WorkerPool = WorkerPool<typename SchedulingRuntime::WorkerProgress, ThreadHandle, Signal>;
    using Scheduler = SchedulerHandle<SchedulingRuntime>;
    using SubmissionPort = std::remove_reference_t<decltype(std::declval<SchedulingRuntime &>().getSubmissionPort(0))>;
public:
    BasicContext() = default;
    ~BasicContext() noexcept
    {
        const details::OwnerThreadGuard guard {m_owner_state};
        this->closeSubmissions();
        this->stopAndJoinWorkers();
        m_scheduling_runtime.cancelPending();
    }
    BasicContext(const BasicContext &) = delete;
    BasicContext(BasicContext &&) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    BasicContext & operator=(BasicContext &&) = delete;
public:
    [[nodiscard]] std::error_code create(ContextInfo && context_info) noexcept
    {
        if (m_domain_id != 0) { return std::make_error_code(std::errc::operation_in_progress); }
        ContextInfo info {std::move(context_info)};
        if (info.getDomainId() == 0 or info.getDispatcherCount() == 0) { return std::make_error_code(std::errc::invalid_argument); }
        try {
            const std::size_t thread_count = info.getWorkerCount();
            typename WorkerPool::WorkerList workers {thread_count, {&info.getWorkerMemoryResource()}};
            m_scheduling_runtime.create(info.getDomainId(), info.getSchedulerCount(), info.getWorkerCount(),
                info.getDispatcherCount(), info.getSubmissionPortCount(), info.getRuntimeMemoryResource());
            std::destroy_at(&m_worker_pool);
            std::construct_at(&m_worker_pool, m_scheduling_runtime.getSignal(), std::move(workers));
        } catch (const std::bad_array_new_length &) {
            return std::make_error_code(std::errc::value_too_large);
        } catch (const std::bad_alloc &) {
            return std::make_error_code(std::errc::not_enough_memory);
        } catch (const std::length_error &) {
            return std::make_error_code(std::errc::value_too_large);
        } catch (const std::system_error & error) {
            return error.code();
        } catch (...) {
            return std::make_error_code(std::errc::io_error);
        }
        m_domain_id = info.getDomainId();
        m_scheduler_count = info.getSchedulerCount();
        return {};
    }
    Scheduler getSchedulerHandle(SchedulerToken token) noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return Scheduler {m_scheduling_runtime, token};
    }
    Scheduler getEventLoopSchedulerHandle() noexcept
    {
        return Scheduler {m_scheduling_runtime, m_domain_id, details::k_owner_scheduler_index};
    }
    Scheduler getPortSchedulerHandle(PortToken token) noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return Scheduler {m_scheduling_runtime, m_domain_id, m_scheduler_count + token.getIndex()};
    }
    SubmissionPort & getSubmissionPort(PortToken token) noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return m_scheduling_runtime.getSubmissionPort(token.getIndex());
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void run(Launcher launcher = {})
    {
        const details::OwnerThreadGuard guard {m_owner_state};
        try {
            m_worker_pool.start([this](std::size_t index) noexcept {
                return m_scheduling_runtime.getWorkerProgress(index);
            }, std::move(launcher));
        } catch (...) {
            this->requestStopProcessing();
            throw;
        }
        auto & signal = m_scheduling_runtime.getSignal();
        for (;;) {
            const auto observed = signal.observe();
            if (m_worker_pool.isStopRequested()) { break; }
            if (m_scheduling_runtime.pollOwner(not m_worker_pool.isRunning())) { continue; }
            if (m_scheduling_runtime.areSubmissionsClosed()) {
                if (m_scheduling_runtime.isIdle()) { break; }
                std::this_thread::yield();
            } else { signal.wait(observed); }
        }
        this->stopAndJoinWorkers();
    }
    PollResult pollOne() noexcept
    {
        const details::OwnerThreadGuard guard {m_owner_state};
        return m_scheduling_runtime.pollOwner(not m_worker_pool.isRunning());
    }
    PollReport poll(PollBudget budget = {}) noexcept
    {
        using Clock = std::chrono::steady_clock;
        const details::OwnerThreadGuard guard {m_owner_state};
        const auto started = Clock::now();
        PollReport report;
        while (report.m_records < budget.m_max_records) {
            if (Clock::now() - started >= budget.m_max_duration) { break; }
            const auto result = m_scheduling_runtime.pollOwner(not m_worker_pool.isRunning());
            if (not result) { break; }
            report.m_result = std::max(report.m_result, result);
            if (result == PollResult::eExecuted) { ++report.m_records; }
        }
        report.m_elapsed = Clock::now() - started;
        report.m_count_limit_reached = report.m_records == budget.m_max_records;
        report.m_time_limit_reached = report.m_elapsed >= budget.m_max_duration;
        return report;
    }
    PollResult pollPortOne(PortToken token) noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return m_scheduling_runtime.pollPortOne(token.getIndex());
    }
    void closeSubmissions() noexcept { m_scheduling_runtime.closeSubmissions(); }
    void requestStopProcessing() noexcept { m_worker_pool.requestStop(); }
    bool isEmpty() const noexcept { return m_scheduling_runtime.isEmpty(); }
private:
    void stopAndJoinWorkers() noexcept
    {
        this->requestStopProcessing();
        m_worker_pool.join();
    }
private:
    SchedulingRuntime m_scheduling_runtime;
    WorkerPool m_worker_pool {m_scheduling_runtime.getSignal()};
    details::OwnerThreadState m_owner_state;
    std::uint32_t m_domain_id = 0;
    std::uint32_t m_scheduler_count = 0;
};

using Context = BasicContext<>;

} // namespace lcf::async
