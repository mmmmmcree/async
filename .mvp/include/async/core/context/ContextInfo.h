#pragma once

#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/scheduling/LogicalQueue.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace lcf::async {

using WorkerIndex = std::uint32_t;

struct QueueBinding
{
    bool m_on_worker;
    WorkerIndex m_worker_index;
};

struct StorageRequirement
{
    std::size_t m_bytes;
    std::size_t m_alignment;
};

struct ContextStorageRequirements
{
    StorageRequirement m_worker_loops;
    StorageRequirement m_workers;
    StorageRequirement m_routes;
};

struct ContextAllocators
{
    using Allocator = std::pmr::polymorphic_allocator<std::byte>;

    ContextAllocators(Allocator worker_loops, Allocator workers, Allocator routes) noexcept
        : m_worker_loops(worker_loops), m_workers(workers), m_routes(routes) {}

    Allocator m_worker_loops;
    Allocator m_workers;
    Allocator m_routes;
};

class ContextInfo
{
public:
    ContextInfo(WorkerIndex worker_count, std::size_t queue_capacity,
        std::pmr::polymorphic_allocator<QueueBinding> allocator)
        : m_domain_id(acquire_domain_id()), m_worker_count(worker_count),
          m_queues(checked_queue_capacity(queue_capacity), allocator) {}

    ContextInfo(const ContextInfo &) = delete;
    ContextInfo & operator=(const ContextInfo &) = delete;
    ContextInfo(ContextInfo && other) noexcept
        : m_domain_id(std::exchange(other.m_domain_id, 0)),
          m_worker_count(std::exchange(other.m_worker_count, 0)),
          m_queues(std::move(other.m_queues)) {}
    ContextInfo & operator=(ContextInfo &&) = delete;

    std::optional<LogicalQueueToken> addControlQueue() noexcept
    {
        return this->addQueue({false, 0});
    }

    std::optional<LogicalQueueToken> addWorkerQueue(WorkerIndex worker_index) noexcept
    {
        if (worker_index >= m_worker_count) {
            return std::nullopt;
        }
        return this->addQueue({true, worker_index});
    }

    WorkerIndex getWorkerCount() const noexcept { return m_worker_count; }
    std::uint64_t getDomainId() const noexcept { return m_domain_id; }
    std::span<const QueueBinding> getQueueBindings() const noexcept { return m_queues.values(); }

    static constexpr StorageRequirement storageRequirement(std::size_t queue_capacity)
    {
        if (queue_capacity > std::numeric_limits<std::uint32_t>::max()
            || queue_capacity > std::numeric_limits<std::size_t>::max() / sizeof(QueueBinding)) {
            throw std::length_error{"ContextInfo capacity overflow"};
        }
        return {queue_capacity * sizeof(QueueBinding), alignof(QueueBinding)};
    }

private:
    std::optional<LogicalQueueToken> addQueue(QueueBinding binding) noexcept
    {
        if (m_domain_id == 0 || m_queues.size() == m_queues.capacity()) {
            return std::nullopt;
        }
        const auto index = static_cast<std::uint32_t>(m_queues.size());
        m_queues.emplaceBack(binding);
        return LogicalQueueToken{m_domain_id, index};
    }

    static std::size_t checked_queue_capacity(std::size_t capacity)
    {
        storageRequirement(capacity);
        return capacity;
    }

    static std::uint64_t acquire_domain_id()
    {
        auto next = s_next_domain_id.load(std::memory_order_relaxed);
        for (;;) {
            if (next == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error{"Context domain IDs exhausted"};
            }
            if (s_next_domain_id.compare_exchange_weak(
                    next, next + 1, std::memory_order_relaxed)) {
                return next;
            }
        }
    }

    inline static std::atomic<std::uint64_t> s_next_domain_id{1};
    std::uint64_t m_domain_id;
    WorkerIndex m_worker_count;
    details::FixedCapacityVector<QueueBinding> m_queues;
};

} // namespace lcf::async
