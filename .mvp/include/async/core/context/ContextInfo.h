#pragma once

#include <async/core/scheduling/LogicalQueue.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

namespace lcf::async {

using WorkerCount = std::uint32_t;

enum class QueueTarget
{
    eControl,
    eCpu,
};

struct QueueBinding
{
    QueueTarget m_target;
};

struct StorageRequirement
{
    std::size_t m_bytes;
    std::size_t m_alignment;
};

struct ContextStorageRequirements
{
    StorageRequirement m_workers;
    StorageRequirement m_routes;
};

struct ContextAllocators
{
    using Allocator = std::pmr::polymorphic_allocator<std::byte>;

    ContextAllocators(Allocator workers, Allocator routes) noexcept
        : m_workers(workers), m_routes(routes) {}

    Allocator m_workers;
    Allocator m_routes;
};

class ContextInfo
{
public:
    explicit ContextInfo(WorkerCount worker_count) noexcept
        : m_domain_id(acquire_domain_id()), m_worker_count(worker_count) {}

    ContextInfo(const ContextInfo &) = delete;
    ContextInfo & operator=(const ContextInfo &) = delete;
    ContextInfo(ContextInfo && other) noexcept
        : m_domain_id(std::exchange(other.m_domain_id, 0)),
          m_worker_count(std::exchange(other.m_worker_count, 0)),
          m_queues(std::move(other.m_queues)) {}
    ContextInfo & operator=(ContextInfo &&) = delete;

    LogicalQueueToken addControlQueue() noexcept
    {
        return this->addQueue(QueueTarget::eControl);
    }

    LogicalQueueToken addCpuQueue() noexcept
    {
        if (m_worker_count == 0) {
            std::terminate();
        }
        return this->addQueue(QueueTarget::eCpu);
    }

    WorkerCount getWorkerCount() const noexcept { return m_worker_count; }
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::span<const QueueBinding> getQueueBindings() const noexcept { return m_queues; }

private:
    LogicalQueueToken addQueue(QueueTarget target) noexcept
    {
        if (m_domain_id == 0) {
            std::terminate();
        }
        const auto index = m_queues.size();
        m_queues.push_back(QueueBinding{target});
        return LogicalQueueToken{m_domain_id, index};
    }

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
    std::vector<QueueBinding> m_queues;
};

} // namespace lcf::async
