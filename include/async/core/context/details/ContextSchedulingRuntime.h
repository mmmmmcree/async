#pragma once

#include <async/core/context/details/ContextDispatchState.h>
#include <async/core/context/declarations/enums.h>
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/scheduling/details/dispatch_targets.h>
#include <async/core/scheduling/declarations/concepts.h>
#include <async/core/scheduling/executor/ExecutorView.h>
#include <async/core/notify/AtomicSequenceSignal.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/notify/declarations/concepts.h>

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
    template <typename, typename, typename, typename> class CpuExecutor = WorkStealingExecutor,
    template <typename> class VictimSelector = RoundRobinVictimSelector,
    notify::signal_c Signal = notify::AtomicSequenceSignal,
    typename Notifier = notify::SignalNotifier<Signal>>
requires (queue_capacity > 0) and std::is_nothrow_default_constructible_v<Signal>
    and notify::signal_notifier_c<Notifier, Signal>
class ContextSchedulingRuntime
{
    using CpuState = ContextCpuState<queue_capacity, CpuExecutor, VictimSelector, Notifier>;
    using PortState = ContextPortState<queue_capacity, Notifier>;
    using DispatchState = ::lcf::async::details::DispatchState<ContextSchedulingRuntime, PortState>;
    using Inbox = ContextOwnerQueue;
public:
    ContextSchedulingRuntime() noexcept = default;
    ~ContextSchedulingRuntime() noexcept = default;
    ContextSchedulingRuntime(const ContextSchedulingRuntime &) = delete;
    ContextSchedulingRuntime(ContextSchedulingRuntime &&) = delete;
    ContextSchedulingRuntime & operator=(const ContextSchedulingRuntime &) = delete;
    ContextSchedulingRuntime & operator=(ContextSchedulingRuntime &&) = delete;
    struct Progress
    {
        ~Progress() noexcept = default;
        Progress() = delete;
        Progress(ContextSchedulingRuntime & runtime, std::size_t worker_index) noexcept :
            m_runtime_p(&runtime), m_worker_index(worker_index) {}
        Progress(const Progress &) noexcept = default;
        Progress(Progress &&) noexcept = default;
        Progress & operator=(const Progress &) noexcept = default;
        Progress & operator=(Progress &&) noexcept = default;
        bool operator()() noexcept
        {
            m_runtime_p->m_active.fetch_add(1, std::memory_order_acq_rel);
            return m_runtime_p->finishTurn(m_runtime_p->progressWorker(m_worker_index));
        }
        ContextSchedulingRuntime * m_runtime_p;
        std::size_t m_worker_index;
    };
    void create(std::uint32_t domain_id, std::uint32_t scheduler_count, std::size_t worker_count,
        std::size_t dispatcher_count, std::size_t port_count, std::pmr::memory_resource & resource)
    {
        FixedCapacityVector<CpuState> cpus {std::max<std::size_t>(worker_count, 1), {&resource}};
        FixedCapacityVector<PortState> ports {port_count, {&resource}};
        FixedCapacityVector<DispatchState> dispatchers {dispatcher_count, {&resource}};
        FixedCapacityVector<ExecutorView> executor_views {cpus.capacity() + 1, {&resource}};
        for (std::size_t index = 0; index < cpus.capacity(); ++index) {
            cpus.emplace_back(std::span {cpus.data(), cpus.capacity()}, index, m_notifier);
        }
        for (std::size_t index = 0; index < port_count; ++index) {
            ports.emplace_back(domain_id, m_notifier);
        }
        const auto ports_per_dispatcher = std::max<std::size_t>((port_count + dispatcher_count - 1) / dispatcher_count, 1);
        for (std::size_t index = 0; index < dispatcher_count; ++index) {
            const auto first = std::min(index * ports_per_dispatcher, port_count);
            const auto count = std::min(ports_per_dispatcher, port_count - first);
            dispatchers.emplace_back(*this, index, std::span {ports}.subspan(first, count));
        }
        executor_views.emplace_back(m_owner_executor);
        for (auto & cpu : cpus) { executor_views.emplace_back(cpu); }
        m_cpus = std::move(cpus);
        m_ports = std::move(ports);
        m_dispatchers = std::move(dispatchers);
        m_executors = std::move(executor_views);
        m_scheduler_count = scheduler_count;
        m_ports_per_dispatcher = ports_per_dispatcher;
        m_accepting.store(true, std::memory_order_release);
    }
    static std::expected<std::pair<std::size_t, std::size_t>, std::error_code> calc_runtime_storage_requirements(
        std::size_t worker_count,
        std::size_t dispatcher_count,
        std::size_t port_count) noexcept
    {
        struct StorageSegment
        {
            std::size_t m_count;
            std::size_t m_element_size;
            std::size_t m_alignment;
        };
        const auto max_size = std::numeric_limits<std::size_t>::max();
        const auto cpu_count = std::max<std::size_t>(worker_count, 1);
        const auto executor_view_count = cpu_count + 1;
        bool size_overflow = cpu_count == max_size;
        const std::array<StorageSegment, 4> segments {{
            {cpu_count, sizeof(CpuState), alignof(CpuState)},
            {port_count, sizeof(PortState), alignof(PortState)},
            {dispatcher_count, sizeof(DispatchState), alignof(DispatchState)},
            {executor_view_count, sizeof(ExecutorView), alignof(ExecutorView)}
        }};
        std::size_t runtime_size_in_bytes = 0;
        std::size_t runtime_alignment = 1;
        for (const auto & [count, element_size, alignment] : segments) {
            const auto padding = (alignment - runtime_size_in_bytes % alignment) % alignment;
            const auto padded_size = runtime_size_in_bytes + padding;
            const bool padding_overflow = runtime_size_in_bytes > max_size - padding;
            const bool segment_overflow = count > (max_size - padded_size) / element_size;
            size_overflow |= padding_overflow | segment_overflow;
            runtime_size_in_bytes = padded_size + count * element_size;
            runtime_alignment = std::max(runtime_alignment, alignment);
        }
        if (size_overflow) { return std::unexpected {std::make_error_code(std::errc::value_too_large)}; }
        return std::pair {runtime_size_in_bytes, runtime_alignment};
    }
    template <dispatch_record_c Record>
    std::error_code tryEnqueue(std::uint32_t target, Record & record) noexcept
    {
        if (not m_accepting.load(std::memory_order_acquire)) { return std::make_error_code(std::errc::operation_canceled); }
        auto & request = record.getDispatchRequest();
        request.m_target_index = target;
        const auto index = m_next_ingress.fetch_add(1, std::memory_order_relaxed) % m_dispatchers.size();
        const auto error = m_dispatchers[index].tryEnqueue(request);
        if (not error) { m_signal.notifyAll(); }
        return error;
    }
    Progress getWorkerProgress(std::size_t index) noexcept { return {*this, index}; }
    PollResult pollOwner(bool drive_workers) noexcept
    {
        m_active.fetch_add(1, std::memory_order_acq_rel);
        bool dispatched = false;
        if (drive_workers) {
            for (auto & state : m_dispatchers) { dispatched |= state.dispatch(); }
        }
        const bool executed = this->executeNext(drive_workers);
        this->finishTurn(dispatched or executed);
        if (executed) { return PollResult::eExecuted; }
        return dispatched ? PollResult::eDispatched : PollResult::eIdle;
    }
    PollResult pollPortOne(std::size_t index) noexcept
    {
        m_active.fetch_add(1, std::memory_order_acq_rel);
        return this->finishTurn(m_ports[index].executeOne()) ? PollResult::eExecuted : PollResult::eIdle;
    }
    auto & getSubmissionPort(std::size_t index) noexcept { return m_ports[index].getPort(); }
    void closeSubmissions() noexcept
    {
        m_accepting.store(false, std::memory_order_release);
        std::ranges::for_each(m_ports, &PortState::closeSubmissions);
        m_signal.notifyAll();
    }
    bool areSubmissionsClosed() const noexcept { return not m_accepting.load(std::memory_order_acquire); }
    bool isEmpty() const noexcept
    {
        return m_owner_queue.isEmpty() and
            std::ranges::all_of(m_cpus, &CpuState::isEmpty) and
            std::ranges::all_of(m_ports, &PortState::isExecutionEmpty) and
            std::ranges::all_of(m_dispatchers, &DispatchState::isEmpty);
    }
    bool isIdle() const noexcept
    {
        const auto observed = m_signal.observe();
        return m_active.load(std::memory_order_acquire) == 0 and this->isEmpty()
            and m_active.load(std::memory_order_acquire) == 0 and m_signal.observe() == observed;
    }
    Signal & getSignal() noexcept { return m_signal; }
private:
    template <typename> friend class ContextTargetRouter;

    std::error_code tryRoute(std::size_t dispatcher_index, RoundRobinTargetSelector & selector,
        DispatchRequest & request) noexcept
    {
        const auto target = request.m_target_index;
        if (target == k_owner_scheduler_index) {
            return m_owner_queue.tryEnqueue(*request.m_executable_p);
        }
        if (target < m_scheduler_count) {
            return m_cpus[selector.select(m_cpus.size())].tryEnqueue(*request.m_executable_p);
        }
        const auto port_index = target - m_scheduler_count;
        const auto home = port_index / m_ports_per_dispatcher;
        return home == dispatcher_index
            ? m_ports[port_index].tryEnqueueExecution(*request.m_executable_p)
            : m_dispatchers[home].tryEnqueue(request);
    }
    bool progressWorker(std::size_t worker_index) noexcept
    {
        const auto background_worker_count = m_cpus.size();
        const auto dispatcher_count = m_dispatchers.size();
        const auto dispatchers_per_worker = dispatcher_count / background_worker_count;
        const auto extra_dispatchers = dispatcher_count % background_worker_count;
        const auto first_dispatcher = worker_index * dispatchers_per_worker + std::min(worker_index, extra_dispatchers);
        const auto assigned_dispatcher_count = dispatchers_per_worker + (worker_index < extra_dispatchers);

        bool progressed = false;
        for (std::size_t offset = 0; offset < assigned_dispatcher_count; ++offset) {
            progressed |= m_dispatchers[first_dispatcher + offset].dispatch();
        }
        return m_cpus[worker_index].executeOne() or progressed;
    }
    bool executeNext(bool include_cpu_executors) noexcept
    {
        if (m_executors.empty()) { return m_owner_executor.executeOne(); }
        const auto executor_count = include_cpu_executors ? m_executors.size() : 1;
        if (m_next_executor >= executor_count) { m_next_executor = 0; }
        for (std::size_t attempt = 0; attempt < executor_count; ++attempt) {
            const auto index = m_next_executor;
            if (++m_next_executor == executor_count) { m_next_executor = 0; }
            if (m_executors[index].executeOne()) { return true; }
        }
        return false;
    }
    bool finishTurn(bool progressed) noexcept
    {
        if (progressed) { m_signal.notifyAll(); }
        m_active.fetch_sub(1, std::memory_order_release);
        return progressed;
    }
private:
    Signal m_signal;
    Notifier m_notifier {m_signal};
    Inbox m_owner_queue;
    BasicExecutor<Inbox> m_owner_executor {m_owner_queue};
    FixedCapacityVector<CpuState> m_cpus;
    FixedCapacityVector<PortState> m_ports;
    FixedCapacityVector<DispatchState> m_dispatchers;
    FixedCapacityVector<ExecutorView> m_executors;
    std::atomic<std::size_t> m_next_ingress {0};
    std::atomic<std::size_t> m_active {0};
    std::atomic<bool> m_accepting {false};
    std::size_t m_scheduler_count = 0;
    std::size_t m_ports_per_dispatcher = 1;
    std::size_t m_next_executor = 0;
};

template <typename SchedulingRuntime>
std::error_code ContextTargetRouter<SchedulingRuntime>::tryEnqueue(DispatchRequest & request) noexcept
{
    return m_runtime_p->tryRoute(m_dispatcher_index, m_selector, request);
}

} // namespace lcf::async::details
