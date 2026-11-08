#pragma once

#include <async/core/scheduling/details/exec_schedule_adapters.h>
#include <async/core/scheduling/details/dispatch_targets.h>
#include <async/core/scheduling/declarations/concepts.h>

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
    PortToken(std::uint32_t domain_id, std::uint32_t index) noexcept :
        m_domain_id(domain_id), m_index(index) {}
public:
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::uint32_t getIndex() const noexcept { return m_index; }
private:
    std::uint32_t m_domain_id;
    std::uint32_t m_index;
};

template <typename Submitter>
class SchedulerHandle
{
public:
    using scheduler_concept = protocols::scheduler_t;
public:
    ~SchedulerHandle() noexcept = default;
    SchedulerHandle() = delete;
    explicit SchedulerHandle(Submitter & submitter) noexcept :
        m_submitter_p(&submitter), m_domain_id(0), m_scheduler_index(details::k_owner_scheduler_index) {}
    SchedulerHandle(Submitter & submitter, SchedulerToken token) noexcept :
        m_submitter_p(&submitter), m_domain_id(token.getDomainId()),
        m_scheduler_index(token.getIndex()) {}
    SchedulerHandle(Submitter & submitter, std::uint32_t domain_id, std::uint32_t target_index) noexcept :
        m_submitter_p(&submitter), m_domain_id(domain_id), m_scheduler_index(target_index) {}
    SchedulerHandle(const SchedulerHandle &) noexcept = default;
    SchedulerHandle(SchedulerHandle &&) noexcept = default;
    SchedulerHandle & operator=(const SchedulerHandle &) noexcept = default;
    SchedulerHandle & operator=(SchedulerHandle &&) noexcept = default;
    bool operator==(const SchedulerHandle &) const noexcept = default;
public:
    auto schedule() const noexcept;
    template <typename Record>
    requires record_submitter_c<Submitter, Record> or indexed_record_submitter_c<Submitter, Record>
    std::error_code tryEnqueue(Record & record) const noexcept
    {
        if constexpr (indexed_record_submitter_c<Submitter, Record>) {
            return m_submitter_p->tryEnqueue(m_scheduler_index, record);
        } else {
            return m_submitter_p->tryEnqueue(record);
        }
    }
private:
    Submitter * m_submitter_p;
    std::uint32_t m_domain_id;
    std::uint32_t m_scheduler_index;
};

template <typename Submitter>
auto SchedulerHandle<Submitter>::schedule() const noexcept
{
    return details::ScheduleSender<SchedulerHandle>{*this};
}

} // namespace lcf::async
