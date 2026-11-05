#pragma once

#include <async/core/queue/declarations/concepts.h>
#include <async/core/scheduling/SchedulerHandle.h>

#include <cassert>
#include <cstdint>
#include <type_traits>

LCF_ASYNC_EXPORT
namespace lcf::async {

// All schedulers from one SPSC port share its single producer.
template <value_queue_c Queue, typename Notify>
requires std::same_as<typename Queue::Value, DispatchRequest> and std::is_nothrow_invocable_v<Notify &>
class SubmissionPort
{
public:
    SubmissionPort(Queue & queue, std::uint32_t domain_id, std::uint32_t scheduler_count, Notify & notify) noexcept :
        m_queue_p(&queue), m_notify_p(&notify), m_domain_id(domain_id), m_scheduler_count(scheduler_count) {}
    SubmissionPort(const SubmissionPort &) = delete;
    SubmissionPort(SubmissionPort &&) = delete;
    SubmissionPort & operator=(const SubmissionPort &) = delete;
    SubmissionPort & operator=(SubmissionPort &&) = delete;
public:
    auto getSchedulerHandle(SchedulerToken token) noexcept
    {
        assert(token.getDomainId() == m_domain_id and token.getIndex() < m_scheduler_count);
        return SchedulerHandle{*this, token};
    }
    template <dispatch_record_c Record>
    std::error_code tryEnqueue(std::uint32_t target_index, Record & record) noexcept
    {
        auto & request = record.getDispatchRequest();
        request.m_target_index = target_index;
        const auto error = m_queue_p->tryEnqueue(request);
        if (not error) { (*m_notify_p)(); }
        return error;
    }
private:
    Queue * m_queue_p;
    Notify * m_notify_p;
    std::uint32_t m_domain_id;
    std::uint32_t m_scheduler_count;
};

} // namespace lcf::async
