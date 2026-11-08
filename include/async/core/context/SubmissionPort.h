#pragma once

#include <async/core/context/declarations/tokens.h>
#include <async/core/scheduling/SchedulerHandle.h>
#include <async/core/scheduling/DispatchRequest.h>
#include <async/core/scheduling/declarations/concepts.h>

#include <cassert>
#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <record_submitter_c<DispatchRequest> Submitter>
class SubmissionPort
{
    using SchedulerHandle = SchedulerHandle<Submitter>;
public:
    ~SubmissionPort() noexcept = default;
    SubmissionPort() = delete;
    SubmissionPort(Submitter & submitter, std::uint32_t domain_id, std::uint32_t scheduler_count) noexcept :
        m_submitter_p(&submitter), m_domain_id(domain_id), m_scheduler_count(scheduler_count) {}
    SubmissionPort(const SubmissionPort &) noexcept = default;
    SubmissionPort(SubmissionPort &&) noexcept = default;
    SubmissionPort & operator=(const SubmissionPort &) noexcept = default;
    SubmissionPort & operator=(SubmissionPort &&) noexcept = default;
public:
    SchedulerHandle getSchedulerHandle(SchedulerToken token) const noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return SchedulerHandle {*m_submitter_p, m_domain_id, token.getIndex()};
    }
    SchedulerHandle getOwnerSchedulerHandle() const noexcept
    {
        return SchedulerHandle {*m_submitter_p, m_domain_id, details::k_owner_scheduler_index};
    }
    SchedulerHandle getPortSchedulerHandle(PortToken token) const noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return SchedulerHandle {*m_submitter_p, m_domain_id, m_scheduler_count + token.getIndex()};
    }
private:
    Submitter * m_submitter_p;
    std::uint32_t m_domain_id;
    std::uint32_t m_scheduler_count;
};

} // namespace lcf::async
