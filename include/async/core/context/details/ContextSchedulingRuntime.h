#pragma once

#include <async/core/context/details/ContextDispatcher.h>
#include <async/core/context/details/ContextExecutor.h>
#include <async/core/context/details/ContextPort.h>
#include <async/core/context/declarations/enums.h>
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/scheduling/details/dispatch_targets.h>
#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/scheduling/executor/BasicExecutor.h>
#include <async/core/scheduling/executor/ExecutorView.h>
#include <async/core/notify/AtomicSequenceSignal.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/notify/declarations/concepts.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory_resource>
#include <ranges>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>

namespace lcf::async::details {

template <std::size_t queue_capacity,
    template <typename, typename, typename, typename> class ExecutorPolicy = WorkStealingExecutor,
    template <typename> class VictimSelector = RoundRobinVictimSelector,
    notify::signal_c Signal = notify::AtomicSequenceSignal,
    typename Notifier = notify::SignalNotifier<Signal>>
requires (queue_capacity > 0) and std::is_nothrow_default_constructible_v<Signal>
    and notify::signal_notifier_c<Notifier, Signal>
class ContextSchedulingRuntime
{
    using Executor = ContextExecutor<queue_capacity, ExecutorPolicy, VictimSelector, Notifier>;
    using Port = ContextPort<queue_capacity, Notifier>;
    using Dispatcher = ContextDispatcher<ContextSchedulingRuntime, Port>;
    using OwnerQueue = queue::intrusive::mpmc::Queue<>;
public:
    class WorkerProgress
    {
        friend class ContextSchedulingRuntime;
    public:
        ~WorkerProgress() noexcept = default;
        WorkerProgress() = delete;
        WorkerProgress(const WorkerProgress &) noexcept = default;
        WorkerProgress(WorkerProgress &&) noexcept = default;
        WorkerProgress & operator=(const WorkerProgress &) noexcept = default;
        WorkerProgress & operator=(WorkerProgress &&) noexcept = default;
        bool operator()() noexcept { return m_runtime_p->pollWorker(*m_executor_p, m_dispatchers); }
    private:
        WorkerProgress(ContextSchedulingRuntime & runtime, Executor & executor, std::span<Dispatcher> dispatchers) noexcept :
            m_runtime_p(&runtime), m_executor_p(&executor), m_dispatchers(dispatchers) {}
        ContextSchedulingRuntime * m_runtime_p;
        Executor * m_executor_p;
        std::span<Dispatcher> m_dispatchers;
    };
public:
    ContextSchedulingRuntime() noexcept = default;
    ~ContextSchedulingRuntime() noexcept = default;
    ContextSchedulingRuntime(const ContextSchedulingRuntime &) = delete;
    ContextSchedulingRuntime(ContextSchedulingRuntime &&) = delete;
    ContextSchedulingRuntime & operator=(const ContextSchedulingRuntime &) = delete;
    ContextSchedulingRuntime & operator=(ContextSchedulingRuntime &&) = delete;
public:
    void create(std::uint32_t domain_id, std::uint32_t scheduler_count, std::size_t worker_count,
        std::size_t dispatcher_count, std::size_t port_count, std::pmr::memory_resource & resource)
    {
        const auto executor_count = std::max<std::size_t>(worker_count, 1);
        FixedCapacityVector<Executor> worker_executors {executor_count, {&resource}};
        FixedCapacityVector<Port> ports {port_count, {&resource}};
        FixedCapacityVector<Dispatcher> dispatchers {dispatcher_count, {&resource}};
        FixedCapacityVector<ExecutorView> executor_views {executor_count + 1, {&resource}};
        const auto executor_span = std::span {worker_executors.data(), executor_count};
        for (std::size_t executor_index = 0; executor_index < executor_count; ++executor_index) {
            worker_executors.emplace_back(executor_span, executor_index, m_notifier);
        }
        for (std::size_t port_index = 0; port_index < port_count; ++port_index) {
            ports.emplace_back(domain_id, m_notifier);
        }
        const auto ports_per_dispatcher = std::max<std::size_t>(
            port_count / dispatcher_count + (port_count % dispatcher_count != 0), 1);
        auto remaining_ports = std::span {ports};
        for (std::size_t dispatcher_index = 0; dispatcher_index < dispatcher_count; ++dispatcher_index) {
            const auto assigned_port_count = std::min(ports_per_dispatcher, remaining_ports.size());
            dispatchers.emplace_back(*this, dispatcher_index, remaining_ports.first(assigned_port_count));
            remaining_ports = remaining_ports.subspan(assigned_port_count);
        }
        executor_views.emplace_back(m_owner_executor);
        for (auto & executor : worker_executors) { executor_views.emplace_back(executor); }
        m_worker_executors = std::move(worker_executors);
        m_ports = std::move(ports);
        m_dispatchers = std::move(dispatchers);
        m_executor_views = std::move(executor_views);
        m_scheduler_count = scheduler_count;
        m_ports_per_dispatcher = ports_per_dispatcher;
        m_submissions_closed.store(false, std::memory_order_release);
    }
    static std::expected<std::pair<std::size_t, std::size_t>, std::error_code> calc_storage_requirements(
        std::size_t worker_count, std::size_t dispatcher_count, std::size_t port_count) noexcept
    {
        struct StorageSegment
        {
            std::size_t m_count;
            std::size_t m_element_size;
            std::size_t m_alignment;
        };
        const auto max_size = std::numeric_limits<std::size_t>::max();
        const auto executor_count = std::max<std::size_t>(worker_count, 1);
        const auto executor_view_count = executor_count + 1;
        bool size_overflow = executor_count == max_size;
        const std::array<StorageSegment, 4> segments {{
            {executor_count, sizeof(Executor), alignof(Executor)},
            {port_count, sizeof(Port), alignof(Port)},
            {dispatcher_count, sizeof(Dispatcher), alignof(Dispatcher)},
            {executor_view_count, sizeof(ExecutorView), alignof(ExecutorView)}
        }};
        std::size_t size_in_bytes = 0;
        std::size_t storage_alignment = 1;
        for (const auto & [count, element_size, alignment] : segments) {
            const auto padding = (alignment - size_in_bytes % alignment) % alignment;
            const auto padded_size = size_in_bytes + padding;
            const bool padding_overflow = size_in_bytes > max_size - padding;
            const bool segment_overflow = count > (max_size - padded_size) / element_size;
            size_overflow |= padding_overflow or segment_overflow;
            size_in_bytes = padded_size + count * element_size;
            storage_alignment = std::max(storage_alignment, alignment);
        }
        if (size_overflow) { return std::unexpected {std::make_error_code(std::errc::value_too_large)}; }
        return std::pair {size_in_bytes, storage_alignment};
    }
    template <dispatch_record_c Record>
    std::error_code tryEnqueue(std::uint32_t target_index, Record & record) noexcept
    {
        if (this->areSubmissionsClosed()) { return std::make_error_code(std::errc::operation_canceled); }
        auto & request = record.getDispatchRequest();
        request.m_target_index = target_index;
        const auto dispatcher_index = m_next_dispatcher_index.fetch_add(1, std::memory_order_relaxed) % m_dispatchers.size();
        const auto error = m_dispatchers[dispatcher_index].tryEnqueue(request);
        if (not error) { m_signal.notifyAll(); }
        return error;
    }
    WorkerProgress getWorkerProgress(std::size_t worker_index) noexcept
    {
        const auto executor_count = m_worker_executors.size();
        const auto dispatchers_per_worker = m_dispatchers.size() / executor_count;
        const auto extra_dispatchers = m_dispatchers.size() % executor_count;
        const auto first_dispatcher_index = worker_index * dispatchers_per_worker + std::min(worker_index, extra_dispatchers);
        const auto assigned_dispatcher_count = dispatchers_per_worker + (worker_index < extra_dispatchers);
        return {*this, m_worker_executors[worker_index],
            std::span {m_dispatchers}.subspan(first_dispatcher_index, assigned_dispatcher_count)};
    }
    PollResult pollOwner(bool include_worker_work) noexcept
    {
        m_active_poll_count.fetch_add(1, std::memory_order_acq_rel);
        const bool dispatched = include_worker_work and dispatch_requests(m_dispatchers);
        const bool executed = include_worker_work ? this->executeNext() : m_owner_executor.executeOne();
        this->finishPoll(dispatched or executed);
        if (executed) { return PollResult::eExecuted; }
        return dispatched ? PollResult::eDispatched : PollResult::eIdle;
    }
    PollResult pollPortOne(std::size_t port_index) noexcept
    {
        m_active_poll_count.fetch_add(1, std::memory_order_acq_rel);
        const bool executed = m_ports[port_index].executeOne();
        this->finishPoll(executed);
        return executed ? PollResult::eExecuted : PollResult::eIdle;
    }
    auto & getSubmissionPort(std::size_t port_index) noexcept { return m_ports[port_index].getSubmissionPort(); }
    void closeSubmissions() noexcept
    {
        m_submissions_closed.store(true, std::memory_order_release);
        std::ranges::for_each(m_ports, &Port::closeSubmissions);
        m_signal.notifyAll();
    }
    bool areSubmissionsClosed() const noexcept { return m_submissions_closed.load(std::memory_order_acquire); }
    // All drivers must have stopped; completion may destroy the borrowed operation.
    void cancelPending() noexcept
    {
        std::ranges::for_each(m_dispatchers, &Dispatcher::cancelPending);
        while (auto * executable_p = m_owner_queue.tryPop()) { executable_p->cancel(); }
        std::ranges::for_each(m_worker_executors, &Executor::cancelPending);
        std::ranges::for_each(m_ports, &Port::cancelPendingExecutions);
    }
    bool isEmpty() const noexcept
    {
        return m_owner_queue.isEmpty() and
            std::ranges::all_of(m_worker_executors, &Executor::isEmpty) and
            std::ranges::all_of(m_ports, &Port::areExecutionsEmpty) and
            std::ranges::all_of(m_dispatchers, &Dispatcher::isEmpty);
    }
    bool isIdle() const noexcept
    {
        const auto observed = m_signal.observe();
        return m_active_poll_count.load(std::memory_order_acquire) == 0 and this->isEmpty()
            and m_active_poll_count.load(std::memory_order_acquire) == 0 and m_signal.observe() == observed;
    }
    Signal & getSignal() noexcept { return m_signal; }
private:
    template <typename> friend class ContextTargetRouter;

    std::error_code tryRoute(std::size_t dispatcher_index, RoundRobinTargetSelector & executor_selector,
        DispatchRequest & request) noexcept
    {
        const auto target_index = request.m_target_index;
        if (target_index == k_owner_scheduler_index) {
            return m_owner_queue.tryEnqueue(*request.m_executable_p);
        }
        if (target_index < m_scheduler_count) {
            const auto executor_index = executor_selector.select(m_worker_executors.size());
            return m_worker_executors[executor_index].tryEnqueue(*request.m_executable_p);
        }
        const auto port_index = target_index - m_scheduler_count;
        const auto port_dispatcher_index = port_index / m_ports_per_dispatcher;
        if (port_dispatcher_index == dispatcher_index) {
            return m_ports[port_index].tryEnqueueExecution(*request.m_executable_p);
        }
        return m_dispatchers[port_dispatcher_index].tryEnqueue(request);
    }
    static bool dispatch_requests(std::span<Dispatcher> dispatchers) noexcept
    {
        bool dispatched = false;
        for (auto & dispatcher : dispatchers) { dispatched |= dispatcher.dispatch(); }
        return dispatched;
    }
    bool pollWorker(Executor & executor, std::span<Dispatcher> dispatchers) noexcept
    {
        m_active_poll_count.fetch_add(1, std::memory_order_acq_rel);
        const bool dispatched = dispatch_requests(dispatchers);
        const bool executed = executor.executeOne();
        const bool progressed = dispatched or executed;
        this->finishPoll(progressed);
        return progressed;
    }
    bool executeNext() noexcept
    {
        for (std::size_t attempt = 0; attempt < m_executor_views.size(); ++attempt) {
            auto & executor = m_executor_views[m_next_executor_index];
            if (++m_next_executor_index == m_executor_views.size()) { m_next_executor_index = 0; }
            if (executor.executeOne()) { return true; }
        }
        return false;
    }
    void finishPoll(bool progressed) noexcept
    {
        if (progressed) { m_signal.notifyAll(); }
        m_active_poll_count.fetch_sub(1, std::memory_order_release);
    }
private:
    Signal m_signal;
    Notifier m_notifier {m_signal};
    OwnerQueue m_owner_queue;
    BasicExecutor<OwnerQueue> m_owner_executor {m_owner_queue};
    FixedCapacityVector<Executor> m_worker_executors;
    FixedCapacityVector<Port> m_ports;
    FixedCapacityVector<Dispatcher> m_dispatchers;
    FixedCapacityVector<ExecutorView> m_executor_views;
    std::atomic<std::size_t> m_next_dispatcher_index {0};
    std::atomic<std::size_t> m_active_poll_count {0};
    std::atomic<bool> m_submissions_closed {true};
    std::size_t m_scheduler_count = 0;
    std::size_t m_ports_per_dispatcher = 1;
    std::size_t m_next_executor_index = 0;
};

template <typename SchedulingRuntime>
std::error_code ContextTargetRouter<SchedulingRuntime>::tryEnqueue(DispatchRequest & request) noexcept
{
    return m_runtime_p->tryRoute(m_dispatcher_index, m_executor_selector, request);
}

} // namespace lcf::async::details
