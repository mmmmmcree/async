#pragma once

#include <async/core/context/details/ContextExecutionStates.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/scheduling/dispatcher/BasicDispatcher.h>
#include <async/core/scheduling/dispatcher/selectors/RoundRobinTargetSelector.h>
#include <async/core/scheduling/declarations/dispatch_structs.h>

#include <cstddef>
#include <ranges>
#include <span>
#include <system_error>

namespace lcf::async::details {

template <typename SchedulingRuntime>
class ContextTargetRouter
{
public:
    ~ContextTargetRouter() noexcept = default;
    ContextTargetRouter() = delete;
    ContextTargetRouter(SchedulingRuntime & runtime, std::size_t dispatcher_index) noexcept :
        m_runtime_p(&runtime), m_dispatcher_index(dispatcher_index), m_selector(dispatcher_index) {}
    ContextTargetRouter(const ContextTargetRouter &) noexcept = default;
    ContextTargetRouter(ContextTargetRouter &&) noexcept = default;
    ContextTargetRouter & operator=(const ContextTargetRouter &) noexcept = default;
    ContextTargetRouter & operator=(ContextTargetRouter &&) noexcept = default;
    std::error_code tryEnqueue(DispatchRequest & request) noexcept;
private:
    SchedulingRuntime * m_runtime_p;
    std::size_t m_dispatcher_index;
    RoundRobinTargetSelector m_selector;
};

template <typename SchedulingRuntime, typename PortState>
class DispatchState
{
    using Ingress = queue::intrusive::mpmc::Queue<queue::intrusive::mpmc::AlgorithmTag::eMutex, DispatchRequest>;
    using Router = ContextTargetRouter<SchedulingRuntime>;
    using PortStateSpan = std::span<PortState>;
    struct Inputs
    {
        ~Inputs() noexcept = default;
        Inputs() = delete;
        Inputs(Ingress & ingress, PortStateSpan ports) noexcept : m_ingress_p(&ingress), m_ports(ports) {}
        Inputs(const Inputs &) noexcept = default;
        Inputs(Inputs &&) noexcept = default;
        Inputs & operator=(const Inputs &) noexcept = default;
        Inputs & operator=(Inputs &&) noexcept = default;
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
    DispatchState(SchedulingRuntime & runtime, std::size_t index, PortStateSpan ports) :
        m_dispatcher(Inputs {m_ingress, ports}, Router {runtime, index}) {}
    DispatchState(const DispatchState &) = delete;
    DispatchState(DispatchState &&) = delete;
    DispatchState & operator=(const DispatchState &) = delete;
    DispatchState & operator=(DispatchState &&) = delete;
    std::error_code tryEnqueue(DispatchRequest & request) noexcept { return m_ingress.tryEnqueue(request); }
    bool dispatch() noexcept { return m_dispatcher.dispatch(); }
    void cancelPending() noexcept { m_dispatcher.cancelPending(); }
    bool isEmpty() const noexcept { return m_dispatcher.isEmpty(); }
private:
    Ingress m_ingress;
    BasicDispatcher<Inputs, Router> m_dispatcher;
};

} // namespace lcf::async::details
