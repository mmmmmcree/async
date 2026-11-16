#pragma once

#include <async/core/context/declarations/tokens.h>
#include <async/core/scheduling/SchedulerHandle.h>
#include <async/core/sink/declarations/concepts.h>

#include <cassert>
#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <rejecting_sink_c Sink, bool has_local_target = false>
class SubmissionPort;

template <rejecting_sink_c Sink>
class SubmissionPort<Sink, false>
{
    using SchedulerHandle = ::lcf::async::SchedulerHandle<Sink>;
public:
    ~SubmissionPort() noexcept = default;
    SubmissionPort() = delete;
    SubmissionPort(Sink & sink, std::uint32_t domain, Priority priority) noexcept :
        m_sink_p(&sink), m_domain(domain), m_priority(priority) {}
    SubmissionPort(const SubmissionPort &) noexcept = default;
    SubmissionPort(SubmissionPort &&) noexcept = default;
    SubmissionPort & operator=(const SubmissionPort &) noexcept = default;
    SubmissionPort & operator=(SubmissionPort &&) noexcept = default;
public:
    SchedulerHandle getSchedulerHandle(SchedulerToken token) const noexcept
    {
        assert(token.getDomainId() == m_domain);
        return SchedulerHandle {*m_sink_p, token, m_priority};
    }
    SchedulerHandle getSchedulerHandle(PortToken token) const noexcept { return this->getSchedulerHandle(token.getSchedulerToken()); }
    Priority getPriority() const noexcept { return m_priority; }
private:
    Sink * m_sink_p;
    std::uint32_t m_domain;
    Priority m_priority;
};

template <rejecting_sink_c Sink>
class SubmissionPort<Sink, true>
{
    using SchedulerHandle = ::lcf::async::SchedulerHandle<Sink>;
public:
    ~SubmissionPort() noexcept = default;
    SubmissionPort() = delete;
    SubmissionPort(Sink & sink, PortToken token, Priority priority) noexcept :
        m_sink_p(&sink), m_domain(token.getDomainId()), m_scheduler_token(token.getSchedulerToken()), m_priority(priority) {}
    SubmissionPort(const SubmissionPort &) noexcept = default;
    SubmissionPort(SubmissionPort &&) noexcept = default;
    SubmissionPort & operator=(const SubmissionPort &) noexcept = default;
    SubmissionPort & operator=(SubmissionPort &&) noexcept = default;
public:
    SchedulerHandle getSchedulerHandle(SchedulerToken token) const noexcept
    {
        assert(token.getDomainId() == m_domain);
        return SchedulerHandle {*m_sink_p, token, m_priority};
    }
    SchedulerHandle getSchedulerHandle(PortToken token) const noexcept { return this->getSchedulerHandle(token.getSchedulerToken()); }
    SchedulerHandle getPortSchedulerHandle() const noexcept { return SchedulerHandle {*m_sink_p, m_scheduler_token, m_priority}; }
    Priority getPriority() const noexcept { return m_priority; }
private:
    Sink * m_sink_p;
    std::uint32_t m_domain;
    SchedulerToken m_scheduler_token;
    Priority m_priority;
};

} // namespace lcf::async
