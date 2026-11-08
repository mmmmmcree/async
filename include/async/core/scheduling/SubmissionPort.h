#pragma once

#include <async/core/queue/declarations/concepts.h>
#include <async/core/notify/declarations/concepts.h>
#include <async/core/scheduling/SchedulerHandle.h>

#include <cassert>
#include <cstdint>

LCF_ASYNC_EXPORT
namespace lcf::async {

// All schedulers from one SPSC port share its single producer.
template <value_queue_c Queue, notify::notifier_c Notifier>
requires std::same_as<typename Queue::Value, DispatchRequest>
class SubmissionPort
{
private:
    using Scheduler = SchedulerHandle<SubmissionPort>;
public:
    ~SubmissionPort() noexcept = default;
    SubmissionPort() = delete;
    SubmissionPort(Queue & queue, std::uint32_t domain_id, Notifier & notifier) noexcept :
        m_queue_p(&queue), m_notifier_p(&notifier), m_domain_id(domain_id) {}
    SubmissionPort(const SubmissionPort &) = delete;
    SubmissionPort(SubmissionPort &&) = delete;
    SubmissionPort & operator=(const SubmissionPort &) = delete;
    SubmissionPort & operator=(SubmissionPort &&) = delete;
public:
    Scheduler getSchedulerHandle(SchedulerToken token) noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return Scheduler {*this, token};
    }
    template <dispatch_record_c Record>
    std::error_code submit(std::uint32_t target_index, Record & record) noexcept
    {
        auto & request = record.getDispatchRequest();
        request.m_target_index = target_index;
        const auto error = m_queue_p->tryEnqueue(request);
        if (not error) { m_notifier_p->notifyAll(); }
        return error;
    }
private:
    Queue * m_queue_p;
    Notifier * m_notifier_p;
    std::uint32_t m_domain_id;
};

} // namespace lcf::async
