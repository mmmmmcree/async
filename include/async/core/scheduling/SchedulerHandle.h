#pragma once
#include <async/core/context/declarations/tokens.h>
#include <async/core/scheduling/details/ScheduleSender.h>
#include <async/core/sink/declarations/concepts.h>
LCF_ASYNC_EXPORT
namespace lcf::async {
template <rejecting_sink_c Sink>
requires scheduling_record_c<typename Sink::value_t>
class SchedulerHandle
{
    using Sender = details::ScheduleSender<SchedulerHandle>;
public:
    using scheduler_concept = protocols::scheduler_t;
    using record_t = typename Sink::value_t;
    using executable_t = typename record_t::executable_t;
    ~SchedulerHandle() noexcept = default;
    SchedulerHandle() = delete;
    SchedulerHandle(Sink & sink, SchedulerToken token, Priority priority) noexcept :
        m_sink_p(&sink), m_token(token), m_priority(priority) {}
    SchedulerHandle(const SchedulerHandle &) noexcept = default;
    SchedulerHandle(SchedulerHandle &&) noexcept = default;
    SchedulerHandle & operator=(const SchedulerHandle &) noexcept = default;
    SchedulerHandle & operator=(SchedulerHandle &&) noexcept = default;
    bool operator==(const SchedulerHandle &) const noexcept = default;
    Sender schedule() const noexcept { return Sender {*this}; }
    std::error_code submit(record_t & record) const noexcept
    {
        record.setSchedulerIndex(m_token.getIndex());
        record.setPriority(m_priority);
        return m_sink_p->receive(record);
    }
    SchedulerHandle & setPriority(Priority priority) noexcept { m_priority = priority; return *this; }
    Priority getPriority() const noexcept { return m_priority; }
private:
    Sink * m_sink_p;
    SchedulerToken m_token;
    Priority m_priority;
};
}
