#pragma once

#include <async/config/macros.h>

#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

class ContextInfo;

class SchedulerToken
{
    friend class ContextInfo;
public:
    ~SchedulerToken() noexcept = default;
    SchedulerToken() = delete;
    SchedulerToken(const SchedulerToken &) noexcept = default;
    SchedulerToken(SchedulerToken &&) noexcept = default;
    SchedulerToken & operator=(const SchedulerToken &) noexcept = default;
    SchedulerToken & operator=(SchedulerToken &&) noexcept = default;
    bool operator==(const SchedulerToken &) const noexcept = default;
private:
    SchedulerToken(std::uint32_t domain_id, std::uint32_t index) noexcept :
        m_domain_id(domain_id), m_index(index) {}
public:
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::uint32_t getIndex() const noexcept { return m_index; }
private:
    std::uint32_t m_domain_id;
    std::uint32_t m_index;
};

class PortToken
{
    friend class ContextInfo;
public:
    ~PortToken() noexcept = default;
    PortToken() = delete;
    PortToken(const PortToken &) noexcept = default;
    PortToken(PortToken &&) noexcept = default;
    PortToken & operator=(const PortToken &) noexcept = default;
    PortToken & operator=(PortToken &&) noexcept = default;
    bool operator==(const PortToken &) const noexcept = default;
private:
    PortToken(std::uint32_t index, SchedulerToken scheduler_token) noexcept :
        m_index(index), m_scheduler_token(scheduler_token) {}
public:
    std::uint32_t getDomainId() const noexcept { return m_scheduler_token.getDomainId(); }
    std::uint32_t getIndex() const noexcept { return m_index; }
    SchedulerToken getSchedulerToken() const noexcept { return m_scheduler_token; }
private:
    std::uint32_t m_index;
    SchedulerToken m_scheduler_token;
};

} // namespace lcf::async
