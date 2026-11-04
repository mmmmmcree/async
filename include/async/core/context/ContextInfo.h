#pragma once

#include <async/core/scheduling/SchedulerHandle.h>
#include <async/core/scheduling/BasicExecutor.h>
#include <async/core/worker/WorkerPool.h>

#include <cstddef>
#include <cstdint>
#include <concepts>
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
    StorageRequirement m_executors;
    StorageRequirement m_workers;
};

struct ContextAllocators
{
    using Allocator = std::pmr::polymorphic_allocator<std::byte>;
public:
    ~ContextAllocators() noexcept = default;
    ContextAllocators() = delete;
    ContextAllocators(Allocator executors, Allocator workers) noexcept :
        m_executors(executors), m_workers(workers) {}
    ContextAllocators(const ContextAllocators &) noexcept = default;
    ContextAllocators(ContextAllocators &&) noexcept = default;
    ContextAllocators & operator=(const ContextAllocators &) = delete;
    ContextAllocators & operator=(ContextAllocators &&) = delete;
public:
    Allocator m_executors;
    Allocator m_workers;
};

class ContextInfo
{
    using Self = ContextInfo;
private:
    inline static std::uint32_t s_next_domain_id = 1;
    static std::uint32_t acquire_domain_id() noexcept
    {
        if (s_next_domain_id == std::numeric_limits<std::uint32_t>::max()) { std::terminate(); }
        return s_next_domain_id++;
    }
    template <std::destructible Value>
    static constexpr StorageRequirement storage_requirement(std::size_t count) noexcept
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value)) { std::terminate(); }
        return {count * sizeof(Value), alignof(Value)};
    }
public:
    ~ContextInfo() noexcept = default;
    ContextInfo() noexcept = default;
    explicit ContextInfo(std::uint32_t worker_count) noexcept :
        m_domain_id(acquire_domain_id()), m_worker_count(worker_count) {}
    ContextInfo(const ContextInfo &) = delete;
    ContextInfo & operator=(const ContextInfo &) = delete;
    ContextInfo(ContextInfo && other) noexcept :
        m_domain_id(std::exchange(other.m_domain_id, 0)),
        m_worker_count(std::exchange(other.m_worker_count, 0)),
        m_scheduler_count(std::exchange(other.m_scheduler_count, 0)),
        m_executor_resource_p(other.m_executor_resource_p),
        m_worker_resource_p(other.m_worker_resource_p) {}
    ContextInfo & operator=(ContextInfo &&) = delete;
public:
    SchedulerToken addScheduler() noexcept
    {
        if (m_domain_id == 0 || m_scheduler_count == std::numeric_limits<std::uint32_t>::max()) {
            std::terminate();
        }
        const auto index = static_cast<std::uint32_t>(m_scheduler_count++);
        return SchedulerToken{m_domain_id, index};
    }
    Self & setWorkerCount(std::uint32_t worker_count) noexcept
    {
        m_worker_count = worker_count;
        return *this;
    }
    Self & setAllocators(ContextAllocators allocators) noexcept
    {
        m_executor_resource_p = allocators.m_executors.resource();
        m_worker_resource_p = allocators.m_workers.resource();
        return *this;
    }
    ContextAllocators getAllocators() const noexcept
    {
        return {ContextAllocators::Allocator{m_executor_resource_p}, ContextAllocators::Allocator{m_worker_resource_p}};
    }
    template <multi_worker_executor_c Executor = BasicExecutor<>, join_handle_c ThreadHandle = std::thread>
    ContextStorageRequirements storageRequirements() const noexcept
    {
        using Worker = typename WorkerPool<Executor, ThreadHandle>::Worker;
        return {storage_requirement<Executor>(m_scheduler_count), storage_requirement<Worker>(m_worker_count)};
    }
    std::uint32_t getWorkerCount() const noexcept { return m_worker_count; }
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::size_t getSchedulerCount() const noexcept { return m_scheduler_count; }
    std::size_t getExecutorCount() const noexcept { return m_scheduler_count + 1; }
private:
    std::uint32_t m_domain_id = acquire_domain_id();
    std::uint32_t m_worker_count = 0;
    std::size_t m_scheduler_count = 0;
    std::pmr::memory_resource * m_executor_resource_p = std::pmr::null_memory_resource();
    std::pmr::memory_resource * m_worker_resource_p = std::pmr::null_memory_resource();
};

} // namespace lcf::async
