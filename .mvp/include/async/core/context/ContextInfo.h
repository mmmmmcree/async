#pragma once

#include <async/core/scheduling/SchedulerHandle.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory_resource>
#include <utility>

namespace lcf::async {

using WorkerCount = std::uint32_t;

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
public:
    ~ContextInfo() noexcept = default;
    ContextInfo() = delete;
    explicit ContextInfo(WorkerCount worker_count) noexcept
        : m_domain_id(acquire_domain_id()), m_worker_count(worker_count) {}

    ContextInfo(const ContextInfo &) = delete;
    ContextInfo & operator=(const ContextInfo &) = delete;
    ContextInfo(ContextInfo && other) noexcept
        : m_domain_id(std::exchange(other.m_domain_id, 0)),
          m_worker_count(std::exchange(other.m_worker_count, 0)),
          m_scheduler_count(std::exchange(other.m_scheduler_count, 0)) {}
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

    WorkerCount getWorkerCount() const noexcept { return m_worker_count; }
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::size_t getSchedulerCount() const noexcept { return m_scheduler_count; }
    std::size_t getExecutorCount() const noexcept { return m_scheduler_count + 1; }

private:
    static std::uint32_t acquire_domain_id() noexcept
    {
        auto next = s_next_domain_id.load(std::memory_order_relaxed);
        for (;;) {
            if (next == std::numeric_limits<std::uint32_t>::max()) {
                std::terminate();
            }
            if (s_next_domain_id.compare_exchange_weak(
                    next, next + 1, std::memory_order_relaxed)) {
                return next;
            }
        }
    }

    inline static std::atomic<std::uint32_t> s_next_domain_id{1};
    std::uint32_t m_domain_id;
    WorkerCount m_worker_count;
    std::size_t m_scheduler_count = 0;
};

} // namespace lcf::async
