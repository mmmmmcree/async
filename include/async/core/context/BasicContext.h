#pragma once

#include <async/config/macros.h>
#include <async/core/context/ContextInfo.h>
#include <async/core/context/declarations/concepts.h>
#include <async/core/context/ExecutionPort.h>
#include <async/core/context/SubmissionPort.h>
#include <async/core/context/details/ContextResources.h>
#include <async/core/context/details/ContextCoordinator.h>
#include <async/core/details/ResourceObject.h>
#include <async/core/notify/declarations/concepts.h>
#include <async/core/notify/EventPumpSignal.h>
#include <async/core/notify/SemaphoreEvent.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/service/ServiceHost.h>
#include <async/core/worker/WorkerPool.h>
#include <async/core/executable/AnyExecutableRef.h>
#include <async/core/scheduling/SchedulingRecord.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/dispatcher/BasicDispatcher.h>
#include <async/core/dispatcher/DispatchRequestSink.h>
#include <async/core/executor/BasicExecutor.h>
#include <async/core/executor/DispatchingExecutor.h>
#include <async/core/stream/DispatchRequestStream.h>
#include <async/core/stream/WorkStealingStream.h>
#include <async/core/stream/PriorityStream.h>
#include <async/core/sink/SubmissionSink.h>
#include <async/core/sink/ExecuteExecutableSink.h>
#include <async/core/sink/CancelExecutableSink.h>
#include <async/core/policy/RoundRobin.h>
#include <async/core/policy/WeightedPriority.h>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <thread>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct DefaultContextComponents
{
    using event_pump_t = notify::SemaphoreEvent;
    using thread_handle_t = std::thread;
    using executable_t = AnyExecutableRef;
    using record_t = SchedulingRecord<executable_t>;
    using submission_queue_t = queue::intrusive::mpmc::Queue<record_t>;
    using execution_queue_t = queue::intrusive::mpmc::Queue<record_t>;
    template <typename Dispatcher, typename Executor>
    using dispatching_executor_t = DispatchingExecutor<Dispatcher, Executor, policy::RoundRobin>;
    template <typename Queue, typename Notifier>
    using submission_sink_t = SubmissionSink<Queue, Notifier>;
    template <typename Target>
    using dispatch_sink_t = DispatchRequestSink<Target, policy::RoundRobin>;
    template <typename Record>
    using execute_sink_t = ExecuteExecutableSink<Record>;
    template <typename Record>
    using cancel_sink_t = CancelExecutableSink<Record>;
    template <typename Queue>
    using request_stream_t = DispatchRequestStream<Queue, policy::RoundRobin, policy::RoundRobin>;
    template <typename Queue>
    using execution_stream_t = WorkStealingStream<Queue, policy::RoundRobin>;
    template <typename Stream>
    using priority_stream_t = PriorityStream<Stream, policy::WeightedPriority<4, 2, 1>>;
    template <typename Stream, typename Sink>
    using dispatcher_t = BasicDispatcher<Stream, Sink>;
    template <typename Stream, typename Sink>
    using executor_t = BasicExecutor<Stream, Sink>;
};

template <context_components_c Components = DefaultContextComponents>
class BasicContext
{
    template <typename, bool>
    friend class ExecutionPort;
    using Self = BasicContext;
    using EventPump = typename Components::event_pump_t;
    using ThreadHandle = typename Components::thread_handle_t;
    using Signal = notify::EventPumpSignal<EventPump>;
    using Notifier = notify::SignalNotifier<Signal>;
    using Coordinator = details::ContextCoordinator<Signal>;
    using Resources = details::ContextResources<Components, Coordinator, Notifier>;
    using Executor = typename Components::template dispatching_executor_t<typename Resources::dispatcher_t, typename Resources::execution_t>;
    using Pool = WorkerPool<Executor, Coordinator, ThreadHandle>;
    using SharedSubmissionPort = SubmissionPort<typename Resources::admission_t>;
    using LocalSubmissionPort = SubmissionPort<typename Resources::admission_t, true>;
    using OwnerPort = ExecutionPort<Self, true>;
    using LocalPort = ExecutionPort<Self, false>;
    enum class RunState { eNotStarted, eRunning, eFinished };
public:
    static std::expected<ContextStorageRequirements, std::error_code> get_storage_requirements(const ContextInfo & info) noexcept
    {
        details::StorageLayout runtime_storage_layout, workers_storage_layout;
        Resources resources;
        runtime_storage_layout.template append<Resources>();
        resources.prepareStorage(info, runtime_storage_layout);
        runtime_storage_layout.template append<Executor>();
        runtime_storage_layout.template append<Priority>(info.getPortPriorities().size());
        const auto service_size = ServiceHost<EventPump>::get_storage_requirements(info.getServiceCapacity());
        if (service_size) { runtime_storage_layout.append(*service_size); }
        Pool pool;
        pool.prepareStorage(info.getWorkerCount(), workers_storage_layout);
        const auto runtime_size = runtime_storage_layout.getSize();
        const auto worker_size = workers_storage_layout.getSize();
        if (not service_size) { return std::unexpected {service_size.error()}; }
        if (not runtime_size) { return std::unexpected {runtime_size.error()}; }
        if (not worker_size) { return std::unexpected {worker_size.error()}; }
        return ContextStorageRequirements {runtime_size->m_size_in_bytes, runtime_size->m_alignment,
            worker_size->m_size_in_bytes, worker_size->m_alignment};
    }
public:
    ~BasicContext() noexcept
    {
        (void)this->shutdown();
        if (m_resources and not m_cleaned.load(std::memory_order_acquire)) { std::terminate(); }
    }
    BasicContext() noexcept = default;
    BasicContext(const BasicContext &) = delete;
    BasicContext(BasicContext &&) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    BasicContext & operator=(BasicContext &&) = delete;
public:
    std::error_code create(ContextInfo && info) noexcept
    {
        if (m_resources) { return std::make_error_code(std::errc::operation_in_progress); }
        if (info.getDispatcherCount() == 0) { return std::make_error_code(std::errc::invalid_argument); }
        try {
            m_resources = details::ResourceObject<Resources> {info.getRuntimeMemoryResource()};
            m_resources->create(info, m_coordinator, Notifier {m_signal});
            m_owner_executor = details::ResourceObject<Executor> {info.getRuntimeMemoryResource(), m_resources->getDispatchers(),
                m_resources->getSharedExecutor(0)};
            m_priorities = {info.getPortPriorities().size(), {&info.getRuntimeMemoryResource()}};
            for (Priority priority : info.getPortPriorities()) { m_priorities.emplace_back(priority); }
            m_pool.prepare(info.getWorkerCount(), m_coordinator, info.getWorkerMemoryResource(), [this](std::size_t index) {
                return Executor {m_resources->getDispatchers(), m_resources->getSharedExecutor(index + 1)};
            });
            if (const std::error_code ec = m_services.create(info.getServiceCapacity(), info.getRuntimeMemoryResource())) {
                return this->creationFailed(ec);
            }
            m_domain = info.getDomainId();
            m_shared_priority = info.getSharedPortPriority();
        } catch (const std::bad_alloc &) { return this->creationFailed(std::make_error_code(std::errc::not_enough_memory)); }
        catch (const std::length_error &) { return this->creationFailed(std::make_error_code(std::errc::value_too_large)); }
        catch (const std::system_error & exception) { return this->creationFailed(exception.code()); }
        catch (...) { return this->creationFailed(make_error_code(errc::unexpected_exception)); }
        return {};
    }
    SharedSubmissionPort getSubmissionPort() noexcept
    { return SharedSubmissionPort {m_resources->getSubmissionSink(0), m_domain, m_shared_priority}; }
    LocalSubmissionPort getSubmissionPort(PortToken token) noexcept
    {
        assert(token.getDomainId() == m_domain);
        return LocalSubmissionPort {m_resources->getSubmissionSink(token.getIndex() + 1), token, m_priorities[token.getIndex()]};
    }
    OwnerPort getExecutionPort() noexcept { return OwnerPort {*this}; }
    LocalPort getExecutionPort(PortToken token) noexcept
    {
        assert(token.getDomainId() == m_domain);
        return LocalPort {*this, token.getIndex()};
    }
    EventPump & getEventPump() noexcept { return m_event_pump; }
    template <service_c Service>
    std::error_code registerService(Service & service) noexcept
    {
        if (not m_resources) { return std::make_error_code(std::errc::operation_not_permitted); }
        return m_services.registerService(AnyServiceRef {service});
    }
    std::error_code enableServiceHandling() noexcept
    {
        if (not m_resources) { return std::make_error_code(std::errc::operation_not_permitted); }
        if (this->isClosing()) { return std::make_error_code(std::errc::operation_canceled); }
        if (const std::error_code ec = this->bindDriverThread()) { return ec; }
        const std::error_code ec = m_services.enable();
        if (ec) {
            m_coordinator.reportErrorCode(ec);
            this->driveUntilClosed();
        }
        return ec;
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    std::error_code run(Launcher launcher = {}) noexcept
    {
        if (const std::error_code ec = this->bindDriverThread()) { return ec; }
        {
            const std::lock_guard lock {m_lifecycle_mutex};
            if (m_run_state != RunState::eNotStarted or not m_resources or m_cleaned.load(std::memory_order_acquire)) {
                return std::make_error_code(std::errc::operation_not_permitted);
            }
            m_run_state = RunState::eRunning;
        }
        std::error_code ec;
        if (not this->isClosing()) { ec = m_services.enable(); }
        if (not ec and not m_coordinator.isStopRequested()) { ec = m_pool.start(m_coordinator, launcher); }
        m_coordinator.reportErrorCode(ec);
        this->driveUntilClosed();
        const std::lock_guard lock {m_lifecycle_mutex};
        m_run_state = RunState::eFinished;
        m_lifecycle_condition.notify_all();
        return m_coordinator.getErrorCode();
    }
    void closeSubmissions() noexcept
    {
        m_submission_close_requested.store(true, std::memory_order_release);
        m_signal.notifyAll();
    }
    void requestStopProcessing() noexcept
    { m_coordinator.requestStop(); }
    bool isEmpty() const noexcept { return m_coordinator.isEmpty(); }
    std::error_code shutdown() noexcept
    {
        if (not m_resources) { return {}; }
        this->requestStopProcessing();
        {
            std::unique_lock lock {m_lifecycle_mutex};
            if (m_run_state == RunState::eRunning and m_driver_thread != std::this_thread::get_id()) {
                m_lifecycle_condition.wait(lock, [this] { return m_run_state != RunState::eRunning; });
                return m_coordinator.getErrorCode();
            }
        }
        if (m_cleaned.load(std::memory_order_acquire)) { return m_coordinator.getErrorCode(); }
        if (const std::error_code ec = this->bindDriverThread()) { return ec; }
        this->driveUntilClosed();
        return m_coordinator.getErrorCode();
    }
private:
    std::error_code creationFailed(std::error_code ec) noexcept
    { m_owner_executor.reset(); m_resources.reset(); return ec; }
    std::error_code bindDriverThread() noexcept
    {
        const std::lock_guard lock {m_lifecycle_mutex};
        if (m_driver_thread == std::thread::id {}) { m_driver_thread = std::this_thread::get_id(); }
        return m_driver_thread == std::this_thread::get_id() ? std::error_code {} : std::make_error_code(std::errc::operation_not_permitted);
    }
    void finish() noexcept
    {
        m_coordinator.closeSubmissions(); m_coordinator.requestStop();
        m_pool.join(); m_coordinator.waitForExecutions();
        m_resources->cancelRemaining(m_coordinator);
        m_cleaned.store(true, std::memory_order_release);
        m_signal.notifyAll();
    }
    bool isClosing() const noexcept
    { return m_submission_close_requested.load(std::memory_order_acquire) or m_coordinator.isStopRequested(); }
    std::error_code waitForEvents() noexcept
    {
        const std::error_code ec = m_event_pump.waitUntil(m_services.getWakeTime());
        m_coordinator.reportErrorCode(ec);
        return ec;
    }
    void driveUntilClosed() noexcept
    {
        while (not m_cleaned.load(std::memory_order_acquire)) {
            const ExecutionReport report = this->executeOne();
            if (not report.isSucceeded() and not m_cleaned.load(std::memory_order_acquire)) { (void)this->waitForEvents(); }
        }
    }
    ExecutionReport executeOne(std::size_t port_index) noexcept
    {
        if (not m_resources or m_cleaned.load(std::memory_order_acquire)) { return ExecutionReport::make_from_stopped(); }
        m_services.closeRegistration();
        const details::ExecutionGuard guard {m_coordinator};
        if (not guard) { return ExecutionReport::make_from_stopped(); }
        const ExecutionReport report = m_resources->getLocalExecutor(port_index).executeOne();
        m_coordinator.reportErrorCode(report.getErrorCode());
        return report;
    }
    ExecutionReport executeOne() noexcept
    {
        if (not m_resources or m_cleaned.load(std::memory_order_acquire)) { return ExecutionReport::make_from_stopped(); }
        if (const std::error_code ec = this->bindDriverThread()) { return ExecutionReport::make_from_error_code(ec); }
        m_services.closeRegistration();
        if (this->isClosing()) {
            m_services.requestStop();
            if (m_services.isStopped()) { m_coordinator.closeSubmissions(); }
        }
        const std::error_code event_ec = m_event_pump.driveOne();
        const std::error_code service_ec = m_services.driveOne();
        std::error_code ec = event_ec;
        if (not ec or ec == errc::no_work) { ec = service_ec; }
        if (ec == errc::no_work) { ec.clear(); }
        m_coordinator.reportErrorCode(ec);
        const ExecutionReport execution = m_coordinator.isStopRequested() ? ExecutionReport {} : m_owner_executor->executeOne();
        m_coordinator.reportErrorCode(execution.getErrorCode());
        if (m_services.isStopped() and (m_coordinator.isStopRequested() or m_coordinator.isFinished())) {
            this->finish();
            const std::error_code finish_ec = m_coordinator.getErrorCode();
            return finish_ec ? ExecutionReport::make_from_error_code(finish_ec, execution.getExecutedCount())
                : ExecutionReport::make_from_stopped(execution.getExecutedCount());
        }
        if (ec) { return ExecutionReport::make_from_error_code(ec, execution.getExecutedCount()); }
        if (execution.isFailed() or execution.isStopped()) { return execution; }
        const bool succeeded = not event_ec or not service_ec or execution.isSucceeded();
        return succeeded ? ExecutionReport::make_from_succeeded(execution.getExecutedCount()) : ExecutionReport {};
    }
private:
    EventPump m_event_pump;
    Signal m_signal {m_event_pump};
    Coordinator m_coordinator {m_signal};
    ServiceHost<EventPump> m_services {m_event_pump};
    details::ResourceObject<Resources> m_resources;
    details::ResourceObject<Executor> m_owner_executor;
    Pool m_pool;
    details::FixedCapacityList<Priority> m_priorities;
    std::uint32_t m_domain = 0;
    Priority m_shared_priority = Priority::eNormal;
    std::atomic<bool> m_submission_close_requested {false};
    std::atomic<bool> m_cleaned {false};
    std::mutex m_lifecycle_mutex;
    std::condition_variable m_lifecycle_condition;
    std::thread::id m_driver_thread;
    RunState m_run_state = RunState::eNotStarted;
};

using Context = BasicContext<>;
template <typename ContextType = Context>
ContextStorageRequirements ContextInfo::getStorageRequirements() const noexcept
{
    const auto requirements = ContextType::get_storage_requirements(*this);
    if (not requirements) { std::terminate(); }
    return *requirements;
}

} // namespace lcf::async
