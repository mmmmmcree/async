#pragma once

#include <async/core/scheduling/declarations/poll_structs.h>
#include <async/core/notify/declarations/concepts.h>
#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/declarations/concepts.h>
#include <async/core/scheduling/policy/declarations/concepts.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <span>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <request_dispatcher_c Dispatcher, work_executor_c SharedExecutor, work_executor_c ThreadExecutor,
    notify::signal_c Signal, index_selection_policy_c DispatchSelectionPolicy>
class SchedulingRuntime
{
public:
    struct DispatcherState
    {
        template <typename... Args>
        explicit DispatcherState(Args &&... args) : m_dispatcher(std::forward<Args>(args)...) {}

        Dispatcher m_dispatcher;
        std::atomic_flag m_driving {};
    };

    class WorkerProgress
    {
    public:
        WorkerProgress(SchedulingRuntime & runtime, SharedExecutor & executor,
            DispatchSelectionPolicy dispatch_selection_policy) :
            m_runtime_p(&runtime), m_executor_p(&executor),
            m_dispatch_selection_policy(std::move(dispatch_selection_policy)) {}
        bool operator()() noexcept
        {
            return m_runtime_p->pollOnce(m_dispatch_selection_policy, m_executor_p).hasProgress();
        }
    private:
        SchedulingRuntime * m_runtime_p;
        SharedExecutor * m_executor_p;
        DispatchSelectionPolicy m_dispatch_selection_policy;
    };

    explicit SchedulingRuntime(Signal & signal, DispatchSelectionPolicy owner_dispatch_selection_policy) :
        m_signal_p(&signal), m_owner_dispatch_selection_policy(std::move(owner_dispatch_selection_policy)) {}

    void attach(std::span<DispatcherState> dispatchers, std::span<SharedExecutor> shared_executors,
        std::span<ThreadExecutor> thread_executors) noexcept
    {
        m_dispatchers = dispatchers;
        m_shared_executors = shared_executors;
        m_thread_executors = thread_executors;
        m_submissions_closed.store(false, std::memory_order_release);
    }
    WorkerProgress getWorkerProgress(std::uint64_t worker_index, DispatchSelectionPolicy dispatch_selection_policy)
    {
        return {*this, m_shared_executors[worker_index], std::move(dispatch_selection_policy)};
    }
    PollReport pollOwner(bool execute_shared_work) noexcept
    {
        SharedExecutor * executor_p = execute_shared_work and not m_shared_executors.empty()
            ? &m_shared_executors.front() : nullptr;
        return this->pollOnce(m_owner_dispatch_selection_policy, executor_p);
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
        return std::ranges::all_of(m_dispatchers, [](const DispatcherState & state) { return state.m_dispatcher.isEmpty(); })
            and std::ranges::all_of(m_shared_executors, &SharedExecutor::isEmpty)
            and std::ranges::all_of(m_thread_executors, &ThreadExecutor::isEmpty);
    }
    bool isIdle() const noexcept
    {
        const typename Signal::Observation observed = m_signal_p->observe();
        return m_active_poll_count.load(std::memory_order_acquire) == 0 and this->isEmpty()
            and m_active_poll_count.load(std::memory_order_acquire) == 0 and m_signal_p->observe() == observed;
    }
private:
    DispatchReport dispatchNext(DispatchSelectionPolicy & dispatch_selection_policy) noexcept
    {
        DispatchReport report;
        if (m_dispatchers.empty()) { return report; }
        const std::uint64_t first_dispatcher_index = dispatch_selection_policy.select(m_dispatchers.size());
        for (std::uint64_t attempt = 0; attempt < m_dispatchers.size(); ++attempt) {
            const std::uint64_t dispatcher_index = (first_dispatcher_index + attempt) % m_dispatchers.size();
            DispatcherState & state = m_dispatchers[dispatcher_index];
            if (state.m_driving.test_and_set(std::memory_order_acquire)) { continue; }
            const DispatchReport dispatched = state.m_dispatcher.dispatchOne();
            state.m_driving.clear(std::memory_order_release);
            if (const std::error_code error_code = dispatched) { std::terminate(); }
            report.m_received += dispatched.m_received;
            report.m_dispatched += dispatched.m_dispatched;
            report.m_attempted += dispatched.m_attempted;
            if (report.hasProgress()) { break; }
        }
        return report;
    }
    PollReport pollOnce(DispatchSelectionPolicy & dispatch_selection_policy, SharedExecutor * executor_p) noexcept
    {
        m_active_poll_count.fetch_add(1, std::memory_order_acq_rel);
        const DispatchReport dispatched = this->dispatchNext(dispatch_selection_policy);
        if (dispatched.hasProgress()) { m_signal_p->notifyAll(); }
        PollReport report;
        report.m_records = executor_p and executor_p->executeOne();
        report.m_result = report.m_records ? PollResult::eExecuted
            : dispatched.hasProgress() ? PollResult::eDispatched : PollResult::eIdle;
        if (report.m_records) { m_signal_p->notifyAll(); }
        m_active_poll_count.fetch_sub(1, std::memory_order_release);
        return report;
    }
private:
    Signal * m_signal_p;
    DispatchSelectionPolicy m_owner_dispatch_selection_policy;
    std::span<DispatcherState> m_dispatchers;
    std::span<SharedExecutor> m_shared_executors;
    std::span<ThreadExecutor> m_thread_executors;
    std::atomic<std::uint64_t> m_active_poll_count {0};
    std::atomic<bool> m_submissions_closed {true};
};

} // namespace lcf::async
