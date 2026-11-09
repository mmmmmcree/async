#pragma once

#include "ContextInfo.h"
#include "SubmissionPort.h"
#include <async/core/context/declarations/poll_structs.h>
#include <async/core/executable/ExecutableNode.h>
#include <async/core/notify/AtomicSequenceSignal.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/context/details/OwnerThreadState.h>
#include <async/core/scheduling/details/dispatch_targets.h>
#include <async/core/scheduling/executor/WorkStealingExecutor.h>
#include <async/core/scheduling/executor/selectors/RoundRobinVictimSelector.h>
#include <async/core/worker/thread/ThreadLauncher.h>

#include <algorithm>
#include <cassert>
#include <chrono>
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

template <join_handle_c ThreadHandle = std::thread, std::uint64_t queue_capacity = 256,
    template <typename, typename, typename, typename> class Executor = WorkStealingExecutor,
    template <typename> class VictimSelector = RoundRobinVictimSelector,
    notify::signal_c Signal = notify::AtomicSequenceSignal,
    typename Notifier = notify::SignalNotifier<Signal>,
    cancellable_executable_c Executable = ExecutableNode>
requires (queue_capacity > 0) and
    std::is_nothrow_default_constructible_v<Signal> and
    notify::signal_notifier_c<Notifier, Signal> and linked_node_c<Executable>
class BasicContext
{
private:
    using SchedulingRuntime = details::ContextSchedulingRuntime<queue_capacity, Executor, VictimSelector, Signal, Notifier, Executable>;
    using WorkerPool = WorkerPool<typename SchedulingRuntime::WorkerProgress, ThreadHandle, Signal>;
    using SubmissionPort = ::lcf::async::SubmissionPort<SchedulingRuntime, Executable>;
    using PortSubmitter = std::remove_reference_t<decltype(std::declval<SchedulingRuntime &>().getPort(0))>;
    using DedicatedSubmissionPort = ::lcf::async::SubmissionPort<PortSubmitter, Executable>;
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
        if (m_domain_id != details::k_unbound_domain_id) { return std::make_error_code(std::errc::operation_in_progress); }
        ContextInfo info {std::move(context_info)};
        if (info.getDomainId() == details::k_unbound_domain_id or info.getDispatcherCount() == 0) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        try {
            const std::uint64_t thread_count = info.getWorkerCount();
            typename WorkerPool::WorkerList workers {thread_count, {&info.getWorkerMemoryResource()}};
            m_scheduling_runtime.create(info.getSchedulerCount(), info.getWorkerCount(),
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
    SubmissionPort getSubmissionPort() noexcept
    {
        return SubmissionPort {m_scheduling_runtime, m_domain_id, m_scheduler_count};
    }
    DedicatedSubmissionPort getSubmissionPort(PortToken token) noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return DedicatedSubmissionPort {m_scheduling_runtime.getPort(token.getIndex()), m_domain_id, m_scheduler_count};
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void run(Launcher launcher = {})
    {
        const details::OwnerThreadGuard guard {m_owner_state};
        try {
            m_worker_pool.start([this](std::uint64_t index) noexcept {
                return m_scheduling_runtime.getWorkerProgress(index);
            }, std::move(launcher));
        } catch (...) {
            this->requestStopProcessing();
            throw;
        }
        Signal & signal = m_scheduling_runtime.getSignal();
        for (;;) {
            const auto observed = signal.observe();
            if (m_worker_pool.isStopRequested()) { break; }
            const PollReport report = m_scheduling_runtime.pollOwner(not m_worker_pool.isRunning());
            if (report.hasProgress()) { continue; }
            if (m_scheduling_runtime.areSubmissionsClosed()) {
                if (m_scheduling_runtime.isIdle()) { break; }
                std::this_thread::yield();
            } else { signal.wait(observed); }
        }
        this->stopAndJoinWorkers();
    }
    PollReport pollOne() noexcept
    {
        const details::OwnerThreadGuard guard {m_owner_state};
        return m_scheduling_runtime.pollOwner(not m_worker_pool.isRunning());
    }
    PollReport poll(PollBudget budget = {}) noexcept
    {
        using Clock = std::chrono::steady_clock;
        const details::OwnerThreadGuard guard {m_owner_state};
        const Clock::time_point started = Clock::now();
        PollReport report;
        while (report.m_records < budget.m_max_records) {
            if (Clock::now() - started >= budget.m_max_duration) { break; }
            const PollReport polled = m_scheduling_runtime.pollOwner(not m_worker_pool.isRunning());
            report.m_records += polled.m_records;
            report.m_result = std::max(report.m_result, polled.getResult());
            if (not polled.hasProgress()) { break; }
        }
        report.m_elapsed = Clock::now() - started;
        report.m_count_limit_reached = report.m_records == budget.m_max_records;
        report.m_time_limit_reached = report.m_elapsed >= budget.m_max_duration;
        return report;
    }
    PollReport pollPortOne(PortToken token) noexcept
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
    std::uint32_t m_domain_id = details::k_unbound_domain_id;
    std::uint32_t m_scheduler_count = 0;
};

using Context = BasicContext<>;

} // namespace lcf::async
