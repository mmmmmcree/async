#pragma once

#include <async/core/notify/declarations/concepts.h>
#include <async/core/queue/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <queue_producer_c SubmissionQueue, notify::notifier_c Notifier>
class QueueSubmitter
{
public:
    QueueSubmitter(SubmissionQueue & submission_queue, Notifier & notifier) noexcept :
        m_submission_queue_p(&submission_queue), m_notifier_p(&notifier) {}

    std::error_code submit(typename SubmissionQueue::Value & request) noexcept
    {
        const std::error_code error_code = m_submission_queue_p->tryEnqueue(request);
        if (not error_code) { m_notifier_p->notifyAll(); }
        return error_code;
    }
private:
    SubmissionQueue * m_submission_queue_p;
    Notifier * m_notifier_p;
};

} // namespace lcf::async
