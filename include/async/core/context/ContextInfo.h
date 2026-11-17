#pragma once

#include <async/config/macros.h>
#include <async/core/context/declarations/tokens.h>
#include <async/core/context/declarations/structs.h>
#include <async/core/scheduling/declarations/enums.h>

#include <exception>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

LCF_ASYNC_EXPORT
namespace lcf::async {

class ContextInfo
{
    inline static constexpr std::uint32_t k_unbound_domain_id = 0;
    inline static std::uint32_t s_next_domain_id {k_unbound_domain_id + 1};
public:
    ~ContextInfo() noexcept = default;
    ContextInfo() = delete;
    explicit ContextInfo(std::pmr::memory_resource & configuration_resource) noexcept :
        m_targets(&configuration_resource), m_priorities(&configuration_resource) {}
    ContextInfo(const ContextInfo &) = delete;
    ContextInfo(ContextInfo && other) noexcept :
        m_domain_id(std::exchange(other.m_domain_id, k_unbound_domain_id)),
        m_worker_count(other.m_worker_count), m_dispatcher_count(other.m_dispatcher_count),
        m_service_capacity(other.m_service_capacity), m_targets(std::move(other.m_targets)),
        m_priorities(std::move(other.m_priorities)), m_shared_priority(other.m_shared_priority),
        m_runtime_resource_p(other.m_runtime_resource_p), m_worker_resource_p(other.m_worker_resource_p) {}
    ContextInfo & operator=(const ContextInfo &) = delete;
    ContextInfo & operator=(ContextInfo &&) = delete;
public:
    SchedulerToken addSharedScheduler()
    {
        const auto index = static_cast<std::uint32_t>(m_targets.size());
        m_targets.push_back({.m_is_shared = true, .m_port_index = 0});
        return SchedulerToken {m_domain_id, index};
    }
    PortToken addPort(Priority priority = Priority::eNormal)
    {
        const auto index = static_cast<std::uint32_t>(m_targets.size());
        const auto port = static_cast<std::uint32_t>(m_priorities.size());
        m_priorities.push_back(priority);
        try { m_targets.push_back({.m_is_shared = false, .m_port_index = port}); }
        catch (...) { m_priorities.pop_back(); throw; }
        return PortToken {port, SchedulerToken {m_domain_id, index}};
    }
    ContextInfo & setWorkerCount(std::uint32_t count) noexcept { m_worker_count = count; return *this; }
    ContextInfo & setDispatcherCount(std::uint32_t count) noexcept { m_dispatcher_count = count; return *this; }
    ContextInfo & setServiceCapacity(std::uint32_t count) noexcept { m_service_capacity = count; return *this; }
    ContextInfo & setSharedPortPriority(Priority priority) noexcept { m_shared_priority = priority; return *this; }
    ContextInfo & setMemoryResources(std::pmr::memory_resource & runtime, std::pmr::memory_resource & workers) noexcept
    { m_runtime_resource_p = &runtime; m_worker_resource_p = &workers; return *this; }
    template <typename ContextType>
    ContextStorageRequirements getStorageRequirements() const noexcept;
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::uint32_t getWorkerCount() const noexcept { return m_worker_count; }
    std::uint32_t getDispatcherCount() const noexcept { return m_dispatcher_count; }
    std::uint32_t getServiceCapacity() const noexcept { return m_service_capacity; }
    std::span<const SchedulerTargetBinding> getTargets() const noexcept { return m_targets; }
    std::span<const Priority> getPortPriorities() const noexcept { return m_priorities; }
    Priority getSharedPortPriority() const noexcept { return m_shared_priority; }
    std::pmr::memory_resource & getRuntimeMemoryResource() const noexcept { return *m_runtime_resource_p; }
    std::pmr::memory_resource & getWorkerMemoryResource() const noexcept { return *m_worker_resource_p; }
private:
    std::uint32_t m_domain_id = s_next_domain_id++;
    std::uint32_t m_worker_count = 0;
    std::uint32_t m_dispatcher_count = 1;
    std::uint32_t m_service_capacity = 0;
    std::pmr::vector<SchedulerTargetBinding> m_targets;
    std::pmr::vector<Priority> m_priorities;
    Priority m_shared_priority = Priority::eNormal;
    std::pmr::memory_resource * m_runtime_resource_p = std::pmr::null_memory_resource();
    std::pmr::memory_resource * m_worker_resource_p = std::pmr::null_memory_resource();
};

} // namespace lcf::async
