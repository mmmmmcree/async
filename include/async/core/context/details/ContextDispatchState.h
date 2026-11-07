#pragma once

#include <async/core/context/details/ContextTopologyStates.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/scheduling/dispatcher/BasicDispatcher.h>
#include <async/core/scheduling/dispatcher/selectors/RoundRobinTargetSelector.h>
#include <async/core/scheduling/declarations/dispatch_structs.h>

#include <cstddef>
#include <ranges>
#include <span>
#include <system_error>

namespace lcf::async::details {

template <typename Topology>
class ContextTopologyRouter
{
public:
    ~ContextTopologyRouter() noexcept = default;
    ContextTopologyRouter() = delete;
    ContextTopologyRouter(Topology & topology, std::size_t dispatcher_index) noexcept :
        m_topology_p(&topology), m_dispatcher_index(dispatcher_index), m_selector(dispatcher_index) {}
    std::error_code tryEnqueue(DispatchRequest & request) noexcept;
private:
    Topology * m_topology_p;
    std::size_t m_dispatcher_index;
    RoundRobinTargetSelector m_selector;
};

template <typename Topology, typename PortState>
class DispatchState
{
    using Ingress = queue::intrusive::mpmc::Queue<queue::intrusive::mpmc::AlgorithmTag::eMutex, DispatchRequest>;
    using Router = ContextTopologyRouter<Topology>;
    using PortStateSpan = std::span<PortState>;
    struct Inputs
    {
        ~Inputs() noexcept = default;
        Inputs() = delete;
        Inputs(Ingress & ingress, PortStateSpan ports) noexcept : m_ingress_p(&ingress), m_ports(ports) {}
        DispatchRequest * tryPop() noexcept
        {
            if (auto * request_p = m_ingress_p->tryPop()) { return request_p; }
            for (std::size_t attempt = 0; attempt < m_ports.size(); ++attempt) {
                auto * request_p = m_ports[m_next_index].tryPopSubmission();
                if (++m_next_index == m_ports.size()) { m_next_index = 0; }
                if (request_p) { return request_p; }
            }
            return nullptr;
        }
        bool isEmpty() const noexcept
        {
            return m_ingress_p->isEmpty() and std::ranges::all_of(m_ports, &PortState::areSubmissionsEmpty);
        }
        Ingress * m_ingress_p;
        PortStateSpan m_ports;
        std::size_t m_next_index = 0;
    };
public:
    ~DispatchState() noexcept = default;
    DispatchState() = delete;
    DispatchState(Topology & topology, std::size_t index, PortStateSpan ports) :
        m_dispatcher(Inputs{m_ingress, ports}, Router{topology, index}) {}
    std::error_code tryEnqueue(DispatchRequest & request) noexcept { return m_ingress.tryEnqueue(request); }
    bool dispatch() noexcept { return m_dispatcher.dispatch(); }
    bool isEmpty() const noexcept { return m_dispatcher.isEmpty(); }
private:
    Ingress m_ingress;
    BasicDispatcher<Inputs, Router> m_dispatcher;
};

} // namespace lcf::async::details
