#pragma once

#include <async/core/context/declarations/enums.h>
#include <async/core/context/declarations/tokens.h>
#include <async/core/scheduling/details/dispatch_targets.h>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

LCF_ASYNC_EXPORT
namespace lcf::async {

struct ContextStorageRequirements
{
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
    inline static std::uint32_t s_next_domain_id {details::k_unbound_domain_id + 1};
public:
    ~ContextInfo() noexcept = default;
    ContextInfo() noexcept = default;
    explicit ContextInfo(std::uint32_t worker_count) noexcept : m_worker_count(worker_count) {}
    ContextInfo(const ContextInfo &) = delete;
    ContextInfo & operator=(const ContextInfo &) = delete;
    ContextInfo(ContextInfo && other) noexcept :
        m_domain_id(std::exchange(other.m_domain_id, details::k_unbound_domain_id)), m_worker_count(other.m_worker_count),
        m_dispatcher_count(other.m_dispatcher_count), m_scheduler_kinds(std::move(other.m_scheduler_kinds)),
        m_port_count(other.m_port_count),
        m_runtime_resource_p(other.m_runtime_resource_p), m_worker_resource_p(other.m_worker_resource_p) {}
    ContextInfo & operator=(ContextInfo &&) = delete;
public:
    SchedulerToken addSharedScheduler() { return this->add_scheduler(SchedulingTargetKind::eShared); }
    PortToken addPort()
    {
        const SchedulerToken scheduler_token = this->add_scheduler(SchedulingTargetKind::eLocal);
        return PortToken {m_port_count++, scheduler_token};
    }
    Self & setWorkerCount(std::uint32_t count) noexcept { m_worker_count = count; return *this; }
    Self & setDispatcherCount(std::uint32_t count) noexcept { m_dispatcher_count = count; return *this; }
    Self & setMemoryResources(std::pmr::memory_resource & runtime_resource, std::pmr::memory_resource & worker_resource) noexcept
    {
        m_runtime_resource_p = &runtime_resource;
        m_worker_resource_p = &worker_resource;
        return *this;
    }
    std::pmr::memory_resource & getRuntimeMemoryResource() const noexcept { return *m_runtime_resource_p; }
    std::pmr::memory_resource & getWorkerMemoryResource() const noexcept { return *m_worker_resource_p; }
    template <typename ContextType>
    ContextStorageRequirements getStorageRequirements() const noexcept;
    std::uint32_t getWorkerCount() const noexcept { return m_worker_count; }
    std::uint32_t getDispatcherCount() const noexcept { return m_dispatcher_count; }
    std::uint32_t getSubmissionPortCount() const noexcept { return m_port_count; }
    std::uint32_t getSchedulerCount() const noexcept { return static_cast<std::uint32_t>(m_scheduler_kinds.size()); }
    std::span<const SchedulingTargetKind> getSchedulerKinds() const noexcept { return m_scheduler_kinds; }
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
private:
    SchedulerToken add_scheduler(SchedulingTargetKind kind)
    {
        const std::uint32_t index = this->getSchedulerCount();
        m_scheduler_kinds.push_back(kind);
        return SchedulerToken {m_domain_id, index};
    }
private:
    std::uint32_t m_domain_id = s_next_domain_id++;
    std::uint32_t m_worker_count = 0;
    std::uint32_t m_dispatcher_count = 1;
    std::vector<SchedulingTargetKind> m_scheduler_kinds;
    std::uint32_t m_port_count = 0;
    std::pmr::memory_resource * m_runtime_resource_p = std::pmr::null_memory_resource();
    std::pmr::memory_resource * m_worker_resource_p = std::pmr::null_memory_resource();
};

} // namespace lcf::async
