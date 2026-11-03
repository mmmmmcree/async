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
private:
    SchedulerToken(std::uint32_t domain_id, std::uint32_t index) noexcept;
public:
    std::uint32_t getDomainId() const noexcept;
    std::uint32_t getIndex() const noexcept;
    bool operator==(const SchedulerToken &) const noexcept = default;
private:
    std::uint32_t m_domain_id;
    std::uint32_t m_index;
};

template <typename Executor>
class SchedulerHandle;

} // namespace lcf::async
