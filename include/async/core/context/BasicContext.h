#pragma once

#include <async/config/macros.h>
#include <async/core/context/ContextInfo.h>
#include <async/core/context/DefaultContextComponents.h>
#include <async/core/context/ExecutionPort.h>
#include <async/core/context/SubmissionPort.h>
#include <async/core/context/details/ContextResources.h>
#include <async/core/context/details/ContextCoordinator.h>
#include <async/core/details/ResourceObject.h>
#include <async/core/driver/WorkerExecutionPath.h>
#include <async/core/event/EventBackend.h>
#include <async/core/notify/BackendSignal.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/service/ServiceHost.h>
#include <async/core/worker/WorkerPool.h>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <thread>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Components = DefaultContextComponents, event_backend_c Backend = EventBackend, join_handle_c ThreadHandle = std::thread>
class BasicContext
{
    template <typename, bool>
    friend class ExecutionPort;
    using Self = BasicContext;
    using Signal = notify::BackendSignal<Backend>;
    using Notifier = notify::SignalNotifier<Signal>;
    using Coordinator = details::ContextCoordinator<Signal>;
    using Resources = details::ContextResources<Components, Coordinator, Notifier>;
    using Path = typename Components::template execution_path_t<typename Resources::dispatcher_t,
        typename Resources::execution_t, Coordinator>;
    using Pool = WorkerPool<Path, Coordinator, ThreadHandle>;
    using SharedSubmissionPort = SubmissionPort<typename Resources::admission_t>;
    using LocalSubmissionPort = SubmissionPort<typename Resources::admission_t, true>;
    using OwnerPort = ExecutionPort<Self, true>;
    using LocalPort = ExecutionPort<Self, false>;
    enum class CloseMode { eOpen, eGraceful, eStop };
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
    static std::expected<ContextStorageRequirements, std::error_code> getStorageRequirements(const ContextInfo & info) noexcept
    {
        details::StorageLayout runtime, workers;
        Resources resources;
        runtime.template append<Resources>();
        resources.prepareStorage(info, runtime);
        runtime.template append<Path>();
        runtime.template append<Priority>(info.getPortPriorities().size());
        ServiceHost<Backend>::appendStorage(info.getServiceCapacity(), runtime);
        Pool pool;
        pool.prepareStorage(info.getWorkerCount(), workers);
        const auto runtime_size = runtime.getSize();
        const auto worker_size = workers.getSize();
        if (not runtime_size) { return std::unexpected {runtime_size.error()}; }
        if (not worker_size) { return std::unexpected {worker_size.error()}; }
        return ContextStorageRequirements {runtime_size->m_size_in_bytes, runtime_size->m_alignment,
            worker_size->m_size_in_bytes, worker_size->m_alignment};
    }
    std::error_code create(ContextInfo && info) noexcept
    {
        if (m_resources) { return std::make_error_code(std::errc::operation_in_progress); }
        if (info.getDispatcherCount() == 0) { return std::make_error_code(std::errc::invalid_argument); }
        try {
            m_resources = details::ResourceObject<Resources> {info.getRuntimeMemoryResource()};
            m_resources->create(info, m_coordinator, Notifier {m_signal});
            m_shared_count = std::max<std::size_t>(info.getWorkerCount(), 1);
            m_owner_path = details::ResourceObject<Path> {info.getRuntimeMemoryResource(), m_resources->getDispatchers(),
                m_resources->getSharedExecutor(0), m_coordinator};
            m_priorities = {info.getPortPriorities().size(), {&info.getRuntimeMemoryResource()}};
            for (Priority priority : info.getPortPriorities()) { m_priorities.emplace_back(priority); }
            details::StorageAllocator storage {info.getRuntimeMemoryResource()};
            m_services.prepareStorage(info.getServiceCapacity(), storage);
            m_pool.prepare(info.getWorkerCount(), m_coordinator, info.getWorkerMemoryResource(), [this](std::size_t index) {
                return Path {m_resources->getDispatchers(), m_resources->getSharedExecutor(index + 1), m_coordinator};
            });
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
    Backend & getEventBackend() noexcept { return m_backend; }
    template <service_c Service>
    std::error_code registerService(Service & service) noexcept
    {
        if (not m_resources) { return std::make_error_code(std::errc::operation_not_permitted); }
        return m_services.registerService(AnyServiceRef {service});
    }
    std::error_code enableServiceHandling() noexcept
    {
        if (not m_resources) { return std::make_error_code(std::errc::operation_not_permitted); }
        if (m_close_mode.load(std::memory_order_acquire) != CloseMode::eOpen) { return std::make_error_code(std::errc::operation_canceled); }
        if (const std::error_code ec = this->bindDriverThread()) { return ec; }
        const std::error_code ec = m_services.enable();
        if (ec) {
            this->requestStopProcessing();
            m_services.requestStop();
            while (not m_services.isStopped()) {
                const DriveReport backend_report = m_backend.driveOne();
                const DriveReport service_report = m_services.driveOne();
                if (backend_report.m_error_code) { m_coordinator.reportErrorCode(backend_report.m_error_code); }
                if (service_report.m_error_code) { m_coordinator.reportErrorCode(service_report.m_error_code); }
                if (not m_services.isStopped()) {
                    if (const std::error_code wait_ec = m_backend.waitUntil(m_services.getWakeTime())) {
                        m_coordinator.reportErrorCode(wait_ec);
                    }
                }
            }
        }
        return ec;
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    std::error_code run(Launcher launcher = {}) noexcept
    {
        if (const std::error_code ec = this->bindDriverThread()) { return ec; }
        {
            const std::lock_guard lock {m_lifecycle_mutex};
            if (m_run_started or not m_resources) { return std::make_error_code(std::errc::operation_not_permitted); }
            m_run_started = true; m_running = true;
        }
        std::error_code ec;
        if (m_close_mode.load(std::memory_order_acquire) == CloseMode::eOpen) { ec = m_services.enable(); }
        if (not ec) {
            try { m_pool.start(m_coordinator, std::move(launcher)); }
            catch (const std::system_error & exception) { ec = exception.code(); }
            catch (...) { ec = errc::unexpected_exception; }
        }
        if (ec) { m_coordinator.reportErrorCode(ec); this->requestStopProcessing(); }
        while (not m_cleaned.load(std::memory_order_acquire)) {
            const ExecutionReport report = this->executeOne<true>(0);
            if (m_cleaned.load(std::memory_order_acquire)) { break; }
            if (not report.hasProgress()) {
                const std::error_code wait_ec = m_backend.waitUntil(m_services.getWakeTime());
                if (wait_ec) { m_coordinator.reportErrorCode(wait_ec); this->requestStopProcessing(); }
            }
        }
        const std::lock_guard lock {m_lifecycle_mutex};
        m_running = false;
        m_lifecycle_condition.notify_all();
        return m_coordinator.getErrorCode();
    }
    void closeSubmissions() noexcept
    {
        CloseMode expected = CloseMode::eOpen;
        m_close_mode.compare_exchange_strong(expected, CloseMode::eGraceful, std::memory_order_acq_rel);
        m_signal.notifyAll();
    }
    void requestStopProcessing() noexcept
    { m_close_mode.store(CloseMode::eStop, std::memory_order_release); m_coordinator.requestStop(); m_signal.notifyAll(); }
    bool isEmpty() const noexcept { return m_coordinator.isEmpty(); }
    std::error_code shutdown() noexcept
    {
        if (not m_resources) { return {}; }
        this->requestStopProcessing();
        {
            std::unique_lock lock {m_lifecycle_mutex};
            if (m_running and m_driver_thread != std::this_thread::get_id()) {
                m_lifecycle_condition.wait(lock, [this] { return not m_running; });
                return m_coordinator.getErrorCode();
            }
        }
        if (m_cleaned.load(std::memory_order_acquire)) { return m_coordinator.getErrorCode(); }
        if (const std::error_code ec = this->bindDriverThread()) { return ec; }
        while (not m_cleaned.load(std::memory_order_acquire)) {
            const ExecutionReport report = this->executeOne<true>(0);
            if (not report.hasProgress() and not report.isStopped()) {
                if (const std::error_code ec = m_backend.waitUntil(m_services.getWakeTime())) { m_coordinator.reportErrorCode(ec); }
            }
        }
        return m_coordinator.getErrorCode();
    }
private:
    std::error_code creationFailed(std::error_code ec) noexcept
    { m_owner_path.reset(); m_resources.reset(); return ec; }
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
    template <bool owner>
    ExecutionReport executeOne(std::size_t index) noexcept
    {
        if (not m_resources or m_cleaned.load(std::memory_order_acquire)) { return {.m_result = ExecutionResult::eStopped}; }
        m_services.closeRegistration();
        if constexpr (not owner) {
            const details::ExecutionGuard guard {m_coordinator};
            if (not guard) { return {.m_result = ExecutionResult::eStopped}; }
            const ExecutionReport report = m_resources->getLocalExecutor(m_shared_count + 1, index).executeOne();
            if (report.m_error_code) { m_coordinator.reportErrorCode(report.m_error_code); }
            return report;
        } else {
            if (const std::error_code ec = this->bindDriverThread()) { return {.m_result = ExecutionResult::eStopped, .m_error_code = ec}; }
            m_services.closeRegistration();
            CloseMode mode = m_close_mode.load(std::memory_order_acquire);
            if (m_coordinator.isStopRequested()) { mode = CloseMode::eStop; m_close_mode.store(mode, std::memory_order_release); }
            if (mode != CloseMode::eOpen) {
                m_services.requestStop();
                if (m_services.isStopped()) { m_coordinator.closeSubmissions(); }
            }
            const DriveReport backend_report = m_backend.driveOne();
            const DriveReport service_report = m_services.driveOne();
            const std::error_code ec = backend_report.m_error_code ? backend_report.m_error_code : service_report.m_error_code;
            if (ec) { m_coordinator.reportErrorCode(ec); this->requestStopProcessing(); mode = CloseMode::eStop; }
            ExecutionReport execution;
            {
                const details::ExecutionGuard guard {m_coordinator};
                if (guard) { execution = m_owner_path->executeOne(); }
            }
            const bool finished = m_services.isStopped() and (mode == CloseMode::eStop
                or (m_coordinator.areSubmissionsClosed() and m_coordinator.isEmpty()));
            if (finished) {
                this->finish();
                return {.m_result = ExecutionResult::eStopped, .m_executed = execution.m_executed,
                    .m_error_code = m_coordinator.getErrorCode()};
            }
            const bool progress = backend_report.hasProgress() or service_report.hasProgress() or execution.hasProgress();
            return {.m_result = ec or execution.m_error_code ? ExecutionResult::eStopped
                    : progress ? ExecutionResult::eProgress : ExecutionResult::eIdle,
                .m_executed = execution.m_executed, .m_error_code = ec ? ec : execution.m_error_code};
        }
    }
    template <bool owner>
    ExecutionReport waitAndExecuteOne(std::size_t index, std::stop_token stop) noexcept
    {
        const std::stop_callback wake {stop, [this] { m_signal.notifyAll(); }};
        while (true) {
            const auto observed = m_signal.observe();
            if (stop.stop_requested()) { return {.m_result = ExecutionResult::eStopped}; }
            const ExecutionReport report = this->executeOne<owner>(index);
            if (report.m_executed or report.isStopped() or report.m_error_code) { return report; }
            if (report.hasProgress()) { continue; }
            if constexpr (owner) {
                if (const std::error_code ec = m_backend.waitUntil(m_services.getWakeTime())) {
                    m_coordinator.reportErrorCode(ec);
                    return {.m_result = ExecutionResult::eStopped, .m_error_code = ec};
                }
            } else { m_signal.wait(observed); }
        }
    }
private:
    Backend m_backend;
    Signal m_signal {m_backend};
    Coordinator m_coordinator {m_signal};
    ServiceHost<Backend> m_services {m_backend};
    details::ResourceObject<Resources> m_resources;
    details::ResourceObject<Path> m_owner_path;
    Pool m_pool;
    details::FixedCapacityVector<Priority> m_priorities;
    std::size_t m_shared_count = 1;
    std::uint32_t m_domain = 0;
    Priority m_shared_priority = Priority::eNormal;
    std::atomic<CloseMode> m_close_mode {CloseMode::eOpen};
    std::atomic<bool> m_cleaned {false};
    std::mutex m_lifecycle_mutex;
    std::condition_variable m_lifecycle_condition;
    std::thread::id m_driver_thread;
    bool m_running = false, m_run_started = false;
};

using Context = BasicContext<>;
template <typename ContextType = Context>
ContextStorageRequirements ContextInfo::getStorageRequirements() const noexcept
{
    const auto requirements = ContextType::getStorageRequirements(*this);
    if (not requirements) { std::terminate(); }
    return *requirements;
}

} // namespace lcf::async
