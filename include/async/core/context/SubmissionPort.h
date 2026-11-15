#pragma once

#include <async/core/context/declarations/tokens.h>
#include <async/core/scheduling/SchedulerHandle.h>
#include <async/core/executable/declarations/concepts.h>
#include <async/core/scheduling/dispatcher/DispatchRequest.h>
#include <async/core/scheduling/declarations/concepts.h>

#include <cassert>
#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <typename Submitter, cancellable_executable_c Executable>
requires record_submitter_c<Submitter, DispatchRequest<Executable>>
class SubmissionPort
{
    using SchedulerHandle = ::lcf::async::SchedulerHandle<Submitter, Executable>;
public:
    ~SubmissionPort() noexcept = default;
    SubmissionPort() = delete;
    SubmissionPort(Submitter & submitter, std::uint32_t domain_id) noexcept :
        m_submitter_p(&submitter), m_domain_id(domain_id) {}
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
    SchedulerHandle getSchedulerHandle(PortToken token) const noexcept { return this->getSchedulerHandle(token.getSchedulerToken()); }
private:
    Submitter * m_submitter_p;
    std::uint32_t m_domain_id;
};

} // namespace lcf::async
