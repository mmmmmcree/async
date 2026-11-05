#pragma once

#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/queue/non_intrusive/spsc/Queue.h>
#include <async/core/queue/non_intrusive/spmc/Deque.h>
#include <async/core/scheduling/SubmissionPort.h>
#include <async/core/scheduling/dispatcher/BasicDispatcher.h>
#include <async/core/scheduling/dispatcher/selectors/RoundRobinTargetSelector.h>
#include <async/core/scheduling/executor/BasicExecutor.h>
#include <async/core/scheduling/executor/ExecutorView.h>
#include <async/core/scheduling/executor/WorkStealingExecutor.h>
#include <async/core/scheduling/details/dispatch_targets.h>
#include <async/core/worker/details/WorkSignal.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <utility>

namespace lcf::async::details {

template <std::size_t queue_capacity>
requires (queue_capacity > 0)
class ContextTopology
{
    struct Notifier
    {
        WorkSignal * m_signal_p;
        void operator()() const noexcept { m_signal_p->notifyAll(); }
    };
    using Node = queue::intrusive::ExecutionNode;
    using Inbox = queue::intrusive::mpmc::Queue<>;
    using ReadyQueue = queue::non_intrusive::spmc::Deque<Node, queue_capacity>;
    using Ingress = queue::intrusive::mpmc::Queue<queue::intrusive::mpmc::AlgorithmTag::eMutex, DispatchRequest>;
    using SubmitQueue = queue::non_intrusive::spsc::Queue<DispatchRequest, queue_capacity>;
    using ReturnQueue = queue::non_intrusive::spsc::Queue<Node, queue_capacity>;
    using Port = SubmissionPort<SubmitQueue, Notifier>;
    struct CpuState
    {
        using Value = Node;
        Inbox m_inbox;
        ReadyQueue m_ready;
        Node * trySteal() noexcept
        {
            if (auto * executable_p = m_ready.trySteal()) { return executable_p; }
            return m_inbox.tryPop();
        }
        WorkStealingExecutor<Inbox, ReadyQueue, RoundRobinVictimSelector<CpuState>, Notifier> m_executor;
        CpuState(std::span<CpuState> cpus, std::size_t index, Notifier notify) :
            m_executor(m_inbox, m_ready, RoundRobinVictimSelector<CpuState>{cpus, index}, notify) {}
        bool operator()() noexcept { return m_executor.executeOne(); }
    };
    struct PortState
    {
        SubmitQueue m_submissions;
        ReturnQueue m_returns;
        Port m_port;
        BasicExecutor<ReturnQueue> m_executor;
        PortState(std::uint32_t domain_id, std::uint32_t scheduler_count, Notifier & notify) noexcept :
            m_port(m_submissions, domain_id, scheduler_count, notify), m_executor(m_returns) {}
    };
    struct Inputs
    {
        Ingress * m_ingress_p;
        std::span<PortState> m_ports;
        std::size_t m_next_index = 0;
        DispatchRequest * tryPop() noexcept
        {
            const auto count = m_ports.size() + 1;
            for (std::size_t attempt = 0; attempt < count; ++attempt) {
                const auto index = m_next_index++ % count;
                auto * request_p = index == 0 ? m_ingress_p->tryPop() : m_ports[index - 1].m_submissions.tryPop();
                if (request_p) { return request_p; }
            }
            return nullptr;
        }
        bool isEmpty() const noexcept
        {
            if (not m_ingress_p->isEmpty()) { return false; }
            for (const auto & port : m_ports) {
                if (not port.m_submissions.isEmpty()) { return false; }
            }
            return true;
        }
    };
    struct Router
    {
        ContextTopology * m_topology_p;
        std::size_t m_dispatcher_index;
        RoundRobinTargetSelector m_selector;
        std::error_code tryEnqueue(DispatchRequest & request) noexcept
        {
            auto & topology = *m_topology_p;
            const auto target = request.m_target_index;
            if (target == k_owner_scheduler_index) {
                return topology.m_owner_queue.tryEnqueue(*request.m_executable_p);
            }
            if (target < topology.m_scheduler_count) {
                return topology.m_cpus[m_selector.select(topology.m_cpus.size())].m_inbox.tryEnqueue(
                    *request.m_executable_p);
            }
            const auto port_index = target - topology.m_scheduler_count;
            const auto home = port_index / topology.m_ports_per_dispatcher;
            return home == m_dispatcher_index
                ? topology.m_ports[port_index].m_returns.tryEnqueue(*request.m_executable_p)
                : topology.m_dispatchers[home].m_ingress.tryEnqueue(request);
        }
    };
    struct DispatchState
    {
        Ingress m_ingress;
        BasicDispatcher<Inputs, Router> m_dispatcher;
        DispatchState(ContextTopology & topology, std::size_t index, std::span<PortState> ports) :
            m_dispatcher(Inputs{&m_ingress, ports}, Router{&topology, index, RoundRobinTargetSelector{index}}) {}
        bool operator()() noexcept { return m_dispatcher.dispatch(); }
    };
public:
    struct Progress
    {
        template <typename Actor>
        Progress(ContextTopology & topology, Actor & actor) noexcept :
            m_topology_p(&topology), m_actor_p(&actor),
            m_progress_f([](void * actor_p) noexcept { return (*static_cast<Actor *>(actor_p))(); }) {}
        bool operator()() noexcept
        {
            m_topology_p->m_active.fetch_add(1, std::memory_order_acq_rel);
            return m_topology_p->finishTurn(m_progress_f(m_actor_p));
        }
        ContextTopology * m_topology_p;
        void * m_actor_p;
        bool (*m_progress_f)(void *) noexcept;
    };
    struct OwnerTurn
    {
        bool m_progressed;
        bool m_executed;
    };
    void create(std::uint32_t domain_id, std::uint32_t scheduler_count, std::size_t worker_count,
        std::size_t dispatcher_count, std::size_t port_count, std::pmr::memory_resource * resource_p)
    {
        FixedCapacityVector<CpuState> cpus{std::max<std::size_t>(worker_count, 1), {resource_p}};
        FixedCapacityVector<PortState> ports{port_count, {resource_p}};
        FixedCapacityVector<DispatchState> dispatchers{dispatcher_count, {resource_p}};
        for (std::size_t index = 0; index < cpus.capacity(); ++index) {
            cpus.emplace_back(std::span{cpus.data(), cpus.capacity()}, index, m_notifier);
        }
        for (std::size_t index = 0; index < port_count; ++index) {
            ports.emplace_back(domain_id, scheduler_count, m_notifier);
        }
        const auto ports_per_dispatcher = std::max<std::size_t>((port_count + dispatcher_count - 1) / dispatcher_count, 1);
        for (std::size_t index = 0; index < dispatcher_count; ++index) {
            const auto first = std::min(index * ports_per_dispatcher, port_count);
            const auto count = std::min(ports_per_dispatcher, port_count - first);
            dispatchers.emplace_back(*this, index, std::span{ports}.subspan(first, count));
        }
        m_cpus = std::move(cpus);
        m_ports = std::move(ports);
        m_dispatchers = std::move(dispatchers);
        m_scheduler_count = scheduler_count;
        m_ports_per_dispatcher = ports_per_dispatcher;
        m_accepting.store(true, std::memory_order_release);
    }
    template <typename Append>
    static void append_storage(std::size_t worker_count, std::size_t dispatcher_count,
        std::size_t port_count, Append append)
    {
        append(std::max<std::size_t>(worker_count, 1), sizeof(CpuState), alignof(CpuState));
        append(port_count, sizeof(PortState), alignof(PortState));
        append(dispatcher_count, sizeof(DispatchState), alignof(DispatchState));
    }
    std::error_code tryEnqueue(std::uint32_t target, DispatchRequest & request) noexcept
    {
        if (not m_accepting.load(std::memory_order_acquire)) { return std::make_error_code(std::errc::operation_canceled); }
        request.m_target_index = target;
        const auto index = m_next_ingress.fetch_add(1, std::memory_order_relaxed) % m_dispatchers.size();
        const auto error = m_dispatchers[index].m_ingress.tryEnqueue(request);
        if (not error) { m_signal.notifyAll(); }
        return error;
    }
    Progress getWorkerProgress(std::size_t index) noexcept
    {
        if (index < m_dispatchers.size()) { return {*this, m_dispatchers[index]}; }
        return {*this, m_cpus[index - m_dispatchers.size()]};
    }
    OwnerTurn pollOwner(bool drive_workers) noexcept
    {
        m_active.fetch_add(1, std::memory_order_acq_rel);
        bool dispatched = false;
        if (drive_workers) {
            for (auto & state : m_dispatchers) { dispatched = state() or dispatched; }
        }
        const bool executed = this->executeNext(drive_workers ? m_cpus.size() + 1 : 1);
        return {this->finishTurn(dispatched or executed), executed};
    }
    bool pollReturn(std::size_t index) noexcept
    {
        m_active.fetch_add(1, std::memory_order_acq_rel);
        return this->finishTurn(m_ports[index].m_executor.executeOne());
    }
    Port & getSubmissionPort(std::size_t index) noexcept
    {
        return m_ports[index].m_port;
    }
    void close() noexcept
    {
        m_accepting.store(false, std::memory_order_release);
        for (auto & port : m_ports) { port.m_submissions.close(); }
        m_signal.notifyAll();
    }
    bool isEmpty() const noexcept
    {
        if (not m_owner_queue.isEmpty()) { return false; }
        for (const auto & cpu : m_cpus) {
            if (not cpu.m_inbox.isEmpty() or not cpu.m_ready.isEmpty()) { return false; }
        }
        for (const auto & port : m_ports) { if (not port.m_returns.isEmpty()) { return false; } }
        for (const auto & state : m_dispatchers) { if (not state.m_dispatcher.isEmpty()) { return false; } }
        return true;
    }
    bool isIdle() const noexcept
    {
        // Reject a scan crossed by an active turn or a completed queue transfer.
        const auto observed = m_signal.observe();
        return m_active.load(std::memory_order_acquire) == 0 and this->isEmpty()
            and m_active.load(std::memory_order_acquire) == 0 and m_signal.observe() == observed;
    }
    WorkSignal & getSignal() noexcept { return m_signal; }
private:
    bool executeNext(std::size_t executor_count) noexcept
    {
        for (std::size_t attempt = 0; attempt < executor_count; ++attempt) {
            const auto index = m_next_executor++ % executor_count;
            auto executor = index == 0 ? ExecutorView{m_owner_executor} : ExecutorView{m_cpus[index - 1].m_executor};
            if (executor.executeOne()) { return true; }
        }
        return false;
    }
    bool finishTurn(bool progressed) noexcept
    {
        if (progressed) { m_signal.notifyAll(); }
        m_active.fetch_sub(1, std::memory_order_release);
        return progressed;
    }
    WorkSignal m_signal;
    Notifier m_notifier{&m_signal};
    Inbox m_owner_queue;
    BasicExecutor<Inbox> m_owner_executor{m_owner_queue};
    FixedCapacityVector<CpuState> m_cpus;
    FixedCapacityVector<PortState> m_ports;
    FixedCapacityVector<DispatchState> m_dispatchers;
    std::atomic<std::size_t> m_next_ingress{0};
    std::atomic<std::size_t> m_active{0};
    std::atomic<bool> m_accepting{false};
    std::size_t m_scheduler_count = 0;
    std::size_t m_ports_per_dispatcher = 1;
    std::size_t m_next_executor = 0;
};

} // namespace lcf::async::details
