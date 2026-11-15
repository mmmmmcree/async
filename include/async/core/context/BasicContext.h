#pragma once

#include "ContextInfo.h"
#include "SubmissionPort.h"
#include "ExecutionPort.h"
#include <async/core/details/concepts/constructible.h>
#include <async/core/executable/ExecutableNode.h>
#include <async/core/notify/AtomicSequenceSignal.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/context/details/DispatchRequestSink.h>
#include <async/core/context/details/OwnerThreadState.h>
#include <async/core/context/details/WorkerPoller.h>
#include <async/core/context/details/assemble_scheduling_runtime.h>
#include <async/core/scheduling/details/dispatch_targets.h>
#include <async/core/scheduling/policy/RoundRobin.h>
#include <async/core/worker/WorkerPool.h>
#include <async/core/worker/thread/ThreadLauncher.h>

#include <cassert>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

namespace details {

template <typename Queue>
using DefaultExecutableStream = WorkStealingStream<Queue, policy::RoundRobin>;
template <typename Queue>
using DefaultRequestStream = DispatchRequestStream<Queue, policy::RoundRobin, policy::RoundRobin>;
template <typename SharedQueue, typename ThreadQueue, typename RequestQueue>
using DefaultRequestSink = DispatchRequestSink<SharedQueue, ThreadQueue, RequestQueue, policy::RoundRobin>;
template <typename RequestStream, typename RequestSink>
using DefaultDispatcher = BasicDispatcher<RequestStream, RequestSink>;
template <typename Dispatcher, typename SharedExecutor, typename LocalExecutor, typename Signal>
using DefaultSchedulingRuntime = SchedulingRuntime<Dispatcher, SharedExecutor, LocalExecutor, Signal, policy::RoundRobin>;

} // namespace details

template <join_handle_c ThreadHandle = std::thread, std::uint64_t queue_capacity = 256,
    cancellable_executable_c Executable = ExecutableNode,
    notify::signal_c Signal = notify::AtomicSequenceSignal,
    typename Notifier = notify::SignalNotifier<Signal>,
    template <typename> class Executor = BasicExecutor,
    template <typename> class ExecutableStream = details::DefaultExecutableStream,
    template <typename> class RequestStream = details::DefaultRequestStream,
    template <typename, typename, typename> class RequestSink = details::DefaultRequestSink,
    template <typename, typename> class Dispatcher = details::DefaultDispatcher,
    template <typename, typename, typename, typename> class Runtime = details::DefaultSchedulingRuntime>
requires (queue_capacity > 0) and
    details::all_nothrow_default_constructible<Signal> and
    notify::signal_notifier_c<Notifier, Signal> and linked_node_c<Executable>
class BasicContext
{
    friend class ContextInfo;
private:
    using Resources = details::ContextResources<queue_capacity, Executable, Signal, Notifier,
        Executor, ExecutableStream, RequestStream, RequestSink, Dispatcher, Runtime>;
    using SchedulingRuntime = typename Resources::runtime_t;
    using WorkerPoller = details::WorkerPoller<SchedulingRuntime, typename Resources::thread_shared_executor_t>;
    using WorkerPool = WorkerPool<WorkerPoller, ThreadHandle, Signal>;
    using SubmissionPort = ::lcf::async::SubmissionPort<typename Resources::shared_submitter_t, Executable>;
    using DedicatedSubmissionPort = ::lcf::async::SubmissionPort<typename Resources::dedicated_submitter_t, Executable>;
public:
    BasicContext() = default;
    ~BasicContext() noexcept
    {
        const details::OwnerThreadGuard guard {m_owner_state};
        this->closeSubmissions();
        this->stopAndJoinWorkers();
        m_resources.cancelPending();
    }
    BasicContext(const BasicContext &) = delete;
    BasicContext(BasicContext &&) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    BasicContext & operator=(BasicContext &&) = delete;
public:
    [[nodiscard]] std::error_code create(ContextInfo && context_info) noexcept
    {
        if (m_domain_id != details::k_unbound_domain_id) { return std::make_error_code(std::errc::operation_in_progress); }
        ContextInfo info {std::move(context_info)};
        if (info.getDomainId() == details::k_unbound_domain_id or info.getDispatcherCount() == 0) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        try {
            const std::uint64_t thread_count = info.getWorkerCount();
            typename WorkerPool::worker_list_t workers {thread_count, {&info.getWorkerMemoryResource()}};
            details::assemble_scheduling_runtime(m_resources, m_scheduling_runtime, info.getWorkerCount(),
                info.getDispatcherCount(), info.getSubmissionPortCount(), info.getSchedulerKinds(), m_notifier,
                info.getRuntimeMemoryResource());
            std::destroy_at(&m_worker_pool);
            std::construct_at(&m_worker_pool, m_signal, std::move(workers));
        } catch (const std::bad_array_new_length &) {
            return std::make_error_code(std::errc::value_too_large);
        } catch (const std::bad_alloc &) {
            return std::make_error_code(std::errc::not_enough_memory);
        } catch (const std::length_error &) {
            return std::make_error_code(std::errc::value_too_large);
        } catch (const std::system_error & error) {
            return error.code();
        } catch (...) {
            return std::make_error_code(std::errc::io_error);
        }
        m_domain_id = info.getDomainId();
        return {};
    }
    SubmissionPort getSubmissionPort() noexcept
    {
        return SubmissionPort {m_resources.m_shared_submitters.front(), m_domain_id};
    }
    DedicatedSubmissionPort getSubmissionPort(PortToken token) noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return DedicatedSubmissionPort {m_resources.m_dedicated_submitters[token.getIndex()], m_domain_id};
    }
    auto getExecutionPort() noexcept
    {
        return ExecutionPort {[this]() noexcept -> PollResult {
            const details::OwnerThreadGuard guard {m_owner_state};
            return details::require_poll_result(m_scheduling_runtime.pollOwner(not m_worker_pool.isRunning()));
        }};
    }
    auto getExecutionPort(PortToken token) noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return ExecutionPort {[this, executor_p = &m_resources.m_thread_local_executors[token.getIndex()]]() noexcept -> PollResult {
            return m_scheduling_runtime.pollLocal(*executor_p);
        }};
    }
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    void run(Launcher launcher = {})
    {
        const details::OwnerThreadGuard guard {m_owner_state};
        try {
            m_worker_pool.start([this](std::uint64_t index) {
                return WorkerPoller {m_scheduling_runtime, m_resources.m_thread_shared_executors[index]};
            }, std::move(launcher));
        } catch (...) {
            this->requestStopProcessing();
            throw;
        }
        Signal & signal = m_signal;
        while (true) {
            const typename Signal::observation_t observed = signal.observe();
            if (m_worker_pool.isStopRequested()) { break; }
            if (details::require_poll_result(m_scheduling_runtime.pollOwner(not m_worker_pool.isRunning()))) { continue; }
            if (m_scheduling_runtime.areSubmissionsClosed()) {
                if (m_scheduling_runtime.isIdle()) { break; }
                std::this_thread::yield();
            } else { signal.wait(observed); }
        }
        this->stopAndJoinWorkers();
    }
    void closeSubmissions() noexcept
    {
        m_resources.closeSubmissions();
        m_scheduling_runtime.closeSubmissions();
    }
    void requestStopProcessing() noexcept { m_worker_pool.requestStop(); }
    bool isEmpty() const noexcept { return m_scheduling_runtime.isEmpty(); }
private:
    void stopAndJoinWorkers() noexcept
    {
        this->requestStopProcessing();
        m_worker_pool.join();
    }
private:
    Signal m_signal;
    Notifier m_notifier {m_signal};
    Resources m_resources;
    SchedulingRuntime m_scheduling_runtime {m_signal};
    WorkerPool m_worker_pool {m_signal};
    details::OwnerThreadState m_owner_state;
    std::uint32_t m_domain_id = details::k_unbound_domain_id;
};

using Context = BasicContext<>;

template <typename ContextType = Context>
ContextStorageRequirements ContextInfo::getStorageRequirements() const noexcept
{
    using Resources = typename ContextType::Resources;
    using Worker = typename ContextType::WorkerPool::worker_t;
    const typename Resources::storage_requirements_result_t requirements = Resources::calc_storage_requirements(
        m_worker_count, m_dispatcher_count, m_port_count, this->getSchedulerCount());
    if (not requirements or m_worker_count > std::numeric_limits<std::size_t>::max() / sizeof(Worker)) {
        std::terminate();
    }
    const auto [runtime_size, runtime_alignment] = *requirements;
    return {runtime_size, runtime_alignment, m_worker_count * sizeof(Worker), alignof(Worker)};
}

} // namespace lcf::async
