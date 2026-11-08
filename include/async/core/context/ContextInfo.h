#pragma once

#include <async/core/context/details/ContextSchedulingRuntime.h>
#include <async/core/notify/AtomicSequenceSignal.h>
#include <async/core/notify/SignalNotifier.h>
#include <async/core/scheduling/SchedulerHandle.h>
#include <async/core/worker/WorkerPool.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory_resource>
#include <thread>
#include <type_traits>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct ContextStorageRequirements
{
    ~ContextStorageRequirements() noexcept = default;
    const std::size_t & getRuntimeSizeInBytes() const noexcept { return m_runtime_size_in_bytes; }
    const std::size_t & getRuntimeAlignment() const noexcept { return m_runtime_alignment; }
    const std::size_t & getWorkersSizeInBytes() const noexcept { return m_workers_size_in_bytes; }
    const std::size_t & getWorkersAlignment() const noexcept { return m_workers_alignment; }

    std::size_t m_runtime_size_in_bytes;
    std::size_t m_runtime_alignment;
    std::size_t m_workers_size_in_bytes;
    std::size_t m_workers_alignment;
};

class ContextInfo
{
    using Self = ContextInfo;
    inline static std::uint32_t s_next_domain_id{1};
    static std::uint32_t acquire_domain_id() noexcept { return s_next_domain_id++; }
public:
    ContextInfo() noexcept = default;
    ~ContextInfo() noexcept = default;
    explicit ContextInfo(std::uint32_t worker_count) noexcept : m_worker_count(worker_count) {}
    ContextInfo(const ContextInfo &) = delete;
    ContextInfo & operator=(const ContextInfo &) = delete;
    ContextInfo(ContextInfo && other) noexcept :
        m_domain_id(std::exchange(other.m_domain_id, 0)), m_worker_count(other.m_worker_count),
        m_dispatcher_count(other.m_dispatcher_count), m_scheduler_count(other.m_scheduler_count),
        m_port_count(other.m_port_count), m_runtime_resource_p(other.m_runtime_resource_p),
        m_worker_resource_p(other.m_worker_resource_p) {}
    ContextInfo & operator=(ContextInfo &&) = delete;
public:
    SchedulerToken addScheduler() noexcept { return SchedulerToken{m_domain_id, m_scheduler_count++}; }
    PortToken addSubmissionPort() noexcept { return PortToken{m_domain_id, m_port_count++}; }
    Self & setWorkerCount(std::uint32_t count) noexcept { m_worker_count = count; return *this; }
    Self & setDispatcherCount(std::uint32_t count) noexcept { m_dispatcher_count = count; return *this; }
    Self & setMemoryResources(
        std::pmr::memory_resource & runtime_resource,
        std::pmr::memory_resource & worker_resource) noexcept
    {
        m_runtime_resource_p = &runtime_resource;
        m_worker_resource_p = &worker_resource;
        return *this;
    }
    std::pmr::memory_resource & getRuntimeMemoryResource() const noexcept { return *m_runtime_resource_p; }
    std::pmr::memory_resource & getWorkerMemoryResource() const noexcept { return *m_worker_resource_p; }
    template <join_handle_c ThreadHandle = std::thread, std::size_t queue_capacity = 256,
        template <typename, typename, typename, typename> class CpuExecutor = WorkStealingExecutor,
        template <typename> class VictimSelector = RoundRobinVictimSelector,
        notify::signal_c Signal = notify::AtomicSequenceSignal,
        typename Notifier = notify::SignalNotifier<Signal>>
    requires (queue_capacity > 0) and std::is_nothrow_default_constructible_v<Signal>
        and notify::signal_notifier_c<Notifier, Signal>
    ContextStorageRequirements getStorageRequirements() const noexcept
    {
        using SchedulingRuntime = details::ContextSchedulingRuntime<queue_capacity, CpuExecutor, VictimSelector, Signal, Notifier>;
        using Worker = typename WorkerPool<typename SchedulingRuntime::Progress, ThreadHandle, Signal>::Worker;
        const auto max_size = std::numeric_limits<std::size_t>::max();
        const auto expected_runtime_requirements = SchedulingRuntime::calc_runtime_storage_requirements(m_worker_count, m_dispatcher_count, m_port_count);
        const bool is_worker_size_overflow = m_worker_count > max_size / sizeof(Worker);
        const bool is_runtime_size_overflow = not expected_runtime_requirements;
        if (is_runtime_size_overflow or is_worker_size_overflow) { std::terminate(); }
        const auto [runtime_size_in_bytes, runtime_alignment] = *expected_runtime_requirements;
        return {runtime_size_in_bytes, runtime_alignment, m_worker_count * sizeof(Worker), alignof(Worker)};
    }
    std::uint32_t getWorkerCount() const noexcept { return m_worker_count; }
    std::uint32_t getDispatcherCount() const noexcept { return m_dispatcher_count; }
    std::uint32_t getSubmissionPortCount() const noexcept { return m_port_count; }
    std::uint32_t getSchedulerCount() const noexcept { return m_scheduler_count; }
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
private:
    std::uint32_t m_domain_id = acquire_domain_id();
    std::uint32_t m_worker_count = 0;
    std::uint32_t m_dispatcher_count = 1;
    std::uint32_t m_scheduler_count = 0;
    std::uint32_t m_port_count = 0;
    std::pmr::memory_resource * m_runtime_resource_p = std::pmr::null_memory_resource();
    std::pmr::memory_resource * m_worker_resource_p = std::pmr::null_memory_resource();
};

} // namespace lcf::async
