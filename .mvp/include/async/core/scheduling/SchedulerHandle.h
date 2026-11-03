#pragma once

#include <async/core/scheduling/details/exec_schedule_adapters.h>
#include <async/core/concepts.h>

#include <cstdint>

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

template <typename Executor>
class SchedulerHandle
{
public:
    using scheduler_concept = protocols::scheduler_t;
public:
    ~SchedulerHandle() noexcept = default;
    SchedulerHandle() = delete;
    explicit SchedulerHandle(Executor & executor) noexcept :
        m_executor_p(&executor), m_domain_id(0), m_scheduler_index(0) {}
    SchedulerHandle(Executor & executor, SchedulerToken token) noexcept :
        m_executor_p(&executor), m_domain_id(token.getDomainId()),
        m_scheduler_index(token.getIndex()) {}
    SchedulerHandle(const SchedulerHandle &) noexcept = default;
    SchedulerHandle(SchedulerHandle &&) noexcept = default;
    SchedulerHandle & operator=(const SchedulerHandle &) noexcept = default;
    SchedulerHandle & operator=(SchedulerHandle &&) noexcept = default;
    bool operator==(const SchedulerHandle &) const noexcept = default;
public:
    auto schedule() const noexcept;
    template <typename Record>
    requires record_submitter_c<Executor, Record>
    EnqueueResult tryEnqueue(Record & record) const noexcept { return m_executor_p->tryEnqueue(record); }
private:
    Executor * m_executor_p;
    std::uint32_t m_domain_id;
    std::uint32_t m_scheduler_index;
};

template <typename Executor>
auto SchedulerHandle<Executor>::schedule() const noexcept
{
    return details::ScheduleSender<SchedulerHandle>{*this};
}

} // namespace lcf::async
