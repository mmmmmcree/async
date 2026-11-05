#pragma once

#include <async/core/context/details/ContextTopology.h>
#include <async/core/scheduling/SchedulerHandle.h>
#include <async/core/worker/WorkerPool.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory_resource>
#include <thread>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct StorageRequirement
{
    std::size_t m_bytes;
    std::size_t m_alignment;
};

struct ContextStorageRequirements
{
    StorageRequirement m_runtime;
    StorageRequirement m_workers;
};

struct ContextAllocators
{
    using Allocator = std::pmr::polymorphic_allocator<std::byte>;
    ContextAllocators() = delete;
    ContextAllocators(Allocator runtime, Allocator workers) noexcept : m_runtime(runtime), m_workers(workers) {}
    Allocator m_runtime;
    Allocator m_workers;
};

class ContextInfo
{
    using Self = ContextInfo;
    inline static std::atomic<std::uint32_t> s_next_domain_id{1};
    static std::uint32_t acquire_domain_id() noexcept
    {
        const auto id = s_next_domain_id.fetch_add(1, std::memory_order_relaxed);
        if (id == 0 or id == std::numeric_limits<std::uint32_t>::max()) { std::terminate(); }
        return id;
    }
public:
    ContextInfo() noexcept = default;
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
    SchedulerToken addScheduler() noexcept
    {
        if (m_domain_id == 0 or std::uint64_t{m_scheduler_count} + m_port_count >= details::k_owner_scheduler_index) {
            std::terminate();
        }
        return SchedulerToken{m_domain_id, m_scheduler_count++};
    }
    std::uint32_t addSubmissionPort() noexcept
    {
        if (m_domain_id == 0 or std::uint64_t{m_scheduler_count} + m_port_count >= details::k_owner_scheduler_index) {
            std::terminate();
        }
        return m_port_count++;
    }
    Self & setWorkerCount(std::uint32_t count) noexcept { m_worker_count = count; return *this; }
    Self & setDispatcherCount(std::uint32_t count) noexcept { m_dispatcher_count = count; return *this; }
    Self & setAllocators(ContextAllocators allocators) noexcept
    {
        m_runtime_resource_p = allocators.m_runtime.resource();
        m_worker_resource_p = allocators.m_workers.resource();
        return *this;
    }
    ContextAllocators getAllocators() const noexcept
    {
        return {{m_runtime_resource_p}, {m_worker_resource_p}};
    }
    template <join_handle_c ThreadHandle = std::thread, std::size_t queue_capacity = 256>
    requires (queue_capacity > 0)
    ContextStorageRequirements storageRequirements() const noexcept
    {
        using Topology = details::ContextTopology<queue_capacity>;
        using Worker = typename WorkerPool<typename Topology::Progress, ThreadHandle>::Worker;
        ContextStorageRequirements storage{{0, 1}, {0, alignof(Worker)}};
        auto append = [&](std::size_t count, std::size_t size, std::size_t alignment) noexcept {
            if (count == 0) { return; }
            const auto padding = (alignment - storage.m_runtime.m_bytes % alignment) % alignment;
            const auto max = std::numeric_limits<std::size_t>::max();
            if (storage.m_runtime.m_bytes > max - padding) { std::terminate(); }
            storage.m_runtime.m_bytes += padding;
            if (count > (max - storage.m_runtime.m_bytes) / size) { std::terminate(); }
            storage.m_runtime.m_bytes += count * size;
            storage.m_runtime.m_alignment = std::max(storage.m_runtime.m_alignment, alignment);
        };
        Topology::append_storage(m_worker_count, m_dispatcher_count, m_port_count, append);
        const std::size_t threads = m_worker_count == 0 ? 0 : std::size_t{m_worker_count} + m_dispatcher_count;
        if (threads > std::numeric_limits<std::size_t>::max() / sizeof(Worker)) { std::terminate(); }
        storage.m_workers.m_bytes = threads * sizeof(Worker);
        return storage;
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
