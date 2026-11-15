#pragma once

#include <async/core/details/concepts/constructible.h>
#include <async/core/scheduling/declarations/enums.h>
#include <async/core/notify/declarations/concepts.h>
#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/DispatcherGroup.h>
#include <async/core/scheduling/dispatcher/declarations/concepts.h>
#include <async/core/scheduling/policy/declarations/concepts.h>

#include <algorithm>
#include <atomic>
#include <expected>
#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <request_dispatcher_c Dispatcher,
    work_executor_c ThreadSharedExecutor,
    work_executor_c ThreadLocalExecutor,
    notify::signal_c Signal,
    selection_policy_c<std::span<Dispatcher>> DispatchSelectionPolicy>
class SchedulingRuntime
{
    using DispatcherSpan = std::span<Dispatcher>;
    using ThreadSharedExecutorSpan = std::span<ThreadSharedExecutor>;
    using ThreadLocalExecutorSpan = std::span<ThreadLocalExecutor>;
public:
    using worker_dispatcher_group_t = DispatcherGroup<Dispatcher, DispatchSelectionPolicy>;
public:
    ~SchedulingRuntime() noexcept = default;
    SchedulingRuntime() = delete;
    explicit SchedulingRuntime(Signal & signal, DispatchSelectionPolicy owner_dispatch_selection_policy = {}) :
        m_signal_p(&signal), m_owner_dispatcher_group(std::move(owner_dispatch_selection_policy)) {}
    SchedulingRuntime(const SchedulingRuntime &) = delete;
    SchedulingRuntime(SchedulingRuntime &&) = delete;
    SchedulingRuntime & operator=(const SchedulingRuntime &) = delete;
    SchedulingRuntime & operator=(SchedulingRuntime &&) = delete;
public:
    worker_dispatcher_group_t makeWorkerDispatcherGroup() const noexcept(
        details::all_nothrow_default_constructible<DispatchSelectionPolicy>)
    {
        return m_owner_dispatcher_group.makeIndependent();
    }
    void attach(DispatcherSpan dispatchers,
        ThreadSharedExecutorSpan thread_shared_executors,
        ThreadLocalExecutorSpan thread_local_executors) noexcept
    {
        m_owner_dispatcher_group.attach(dispatchers);
        m_thread_shared_executors = thread_shared_executors;
        m_thread_local_executors = thread_local_executors;
        m_submissions_closed.store(false, std::memory_order_release);
    }
    std::expected<PollResult, std::error_code> pollOwner(bool execute_shared_work) noexcept
    {
        if (execute_shared_work and not m_thread_shared_executors.empty()) {
            return this->pollWorker(m_owner_dispatcher_group, m_thread_shared_executors.front());
        }
        m_active_poll_count.fetch_add(1, std::memory_order_acq_rel);
        const DispatchReport dispatched = m_owner_dispatcher_group.dispatchOne();
        if (dispatched.hasProgress()) { m_signal_p->notifyAll(); }
        m_active_poll_count.fetch_sub(1, std::memory_order_release);
        if (dispatched.m_error_code) { return std::unexpected {dispatched.m_error_code}; }
        return dispatched.hasProgress() ? PollResult::eDispatched : PollResult::eIdle;
    }
    std::expected<PollResult, std::error_code> pollWorker(
        worker_dispatcher_group_t & dispatcher_group, ThreadSharedExecutor & executor) noexcept
    {
        m_active_poll_count.fetch_add(1, std::memory_order_acq_rel);
        const DispatchReport dispatched = dispatcher_group.dispatchOne();
        if (dispatched.hasProgress()) { m_signal_p->notifyAll(); }
        const bool executed = not dispatched.m_error_code and executor.executeOne();
        if (executed) { m_signal_p->notifyAll(); }
        m_active_poll_count.fetch_sub(1, std::memory_order_release);
        if (dispatched.m_error_code) { return std::unexpected {dispatched.m_error_code}; }
        return executed ? PollResult::eExecuted : dispatched.hasProgress() ? PollResult::eDispatched : PollResult::eIdle;
    }
    PollResult pollLocal(ThreadLocalExecutor & executor) noexcept
    {
        m_active_poll_count.fetch_add(1, std::memory_order_acq_rel);
        const bool executed = executor.executeOne();
        if (executed) { m_signal_p->notifyAll(); }
        m_active_poll_count.fetch_sub(1, std::memory_order_release);
        return executed ? PollResult::eExecuted : PollResult::eIdle;
    }
    void closeSubmissions() noexcept
    {
        m_submissions_closed.store(true, std::memory_order_release);
        m_signal_p->notifyAll();
    }
    bool areSubmissionsClosed() const noexcept { return m_submissions_closed.load(std::memory_order_acquire); }
    std::atomic<std::uint64_t> & getActivePollCount() noexcept { return m_active_poll_count; }
    bool isEmpty() const noexcept
    {
        return m_owner_dispatcher_group.isEmpty()
            and std::ranges::all_of(m_thread_shared_executors, &ThreadSharedExecutor::isEmpty)
            and std::ranges::all_of(m_thread_local_executors, &ThreadLocalExecutor::isEmpty);
    }
    bool isIdle() const noexcept
    {
        const auto observed = m_signal_p->observe();
        return m_active_poll_count.load(std::memory_order_acquire) == 0 and this->isEmpty()
            and m_active_poll_count.load(std::memory_order_acquire) == 0 and m_signal_p->observe() == observed;
    }
private:
    Signal * m_signal_p;
    worker_dispatcher_group_t m_owner_dispatcher_group;
    ThreadSharedExecutorSpan m_thread_shared_executors;
    ThreadLocalExecutorSpan m_thread_local_executors;
    std::atomic<std::uint64_t> m_active_poll_count {0};
    std::atomic<bool> m_submissions_closed {true};
};

} // namespace lcf::async
