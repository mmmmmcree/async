#pragma once

#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/scheduling/dispatcher/BasicDispatcher.h>
#include <async/core/scheduling/dispatcher/selectors/RoundRobinTargetSelector.h>
#include <async/core/scheduling/declarations/dispatch_structs.h>

#include <algorithm>
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
        m_runtime_p(&runtime), m_dispatcher_index(dispatcher_index), m_executor_selector(dispatcher_index) {}
    ContextTargetRouter(const ContextTargetRouter &) noexcept = default;
    ContextTargetRouter(ContextTargetRouter &&) noexcept = default;
    ContextTargetRouter & operator=(const ContextTargetRouter &) noexcept = default;
    ContextTargetRouter & operator=(ContextTargetRouter &&) noexcept = default;
    std::error_code tryEnqueue(DispatchRequest & request) noexcept;
private:
    SchedulingRuntime * m_runtime_p;
    std::size_t m_dispatcher_index;
    RoundRobinTargetSelector m_executor_selector;
};

template <typename SchedulingRuntime, typename Port>
class ContextDispatcher
{
    using Ingress = queue::intrusive::mpmc::Queue<queue::intrusive::mpmc::AlgorithmTag::eMutex, DispatchRequest>;
    using Router = ContextTargetRouter<SchedulingRuntime>;
    using PortSpan = std::span<Port>;
    class InputSource
    {
    public:
        ~InputSource() noexcept = default;
        InputSource() = delete;
        InputSource(Ingress & ingress, PortSpan ports) noexcept : m_ingress_p(&ingress), m_ports(ports) {}
        InputSource(const InputSource &) noexcept = default;
        InputSource(InputSource &&) noexcept = default;
        InputSource & operator=(const InputSource &) noexcept = default;
        InputSource & operator=(InputSource &&) noexcept = default;
    public:
        DispatchRequest * tryPop() noexcept
        {
            if (auto * request_p = m_ingress_p->tryPop()) { return request_p; }
            for (std::size_t attempt = 0; attempt < m_ports.size(); ++attempt) {
                auto * request_p = m_ports[m_next_port_index].tryPopSubmission();
                if (++m_next_port_index == m_ports.size()) { m_next_port_index = 0; }
                if (request_p) { return request_p; }
            }
            return nullptr;
        }
        bool isEmpty() const noexcept
        {
            return m_ingress_p->isEmpty() and std::ranges::all_of(m_ports, &Port::areSubmissionsEmpty);
        }
    private:
        Ingress * m_ingress_p;
        PortSpan m_ports;
        std::size_t m_next_port_index = 0;
    };
public:
    ~ContextDispatcher() noexcept = default;
    ContextDispatcher() = delete;
    ContextDispatcher(SchedulingRuntime & runtime, std::size_t dispatcher_index, PortSpan ports) :
        m_dispatcher(InputSource {m_ingress, ports}, Router {runtime, dispatcher_index}) {}
    ContextDispatcher(const ContextDispatcher &) = delete;
    ContextDispatcher(ContextDispatcher &&) = delete;
    ContextDispatcher & operator=(const ContextDispatcher &) = delete;
    ContextDispatcher & operator=(ContextDispatcher &&) = delete;
    std::error_code tryEnqueue(DispatchRequest & request) noexcept { return m_ingress.tryEnqueue(request); }
    bool dispatch() noexcept { return m_dispatcher.dispatch(); }
    void cancelPending() noexcept { m_dispatcher.cancelPending(); }
    bool isEmpty() const noexcept { return m_dispatcher.isEmpty(); }
private:
    Ingress m_ingress;
    BasicDispatcher<InputSource, Router> m_dispatcher;
};

} // namespace lcf::async::details
