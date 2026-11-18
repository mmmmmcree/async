#pragma once

#include <async/core/notify/declarations/concepts.h>
#include <async/core/details/concepts/constructible.h>
#include <async/core/queue/declarations/concepts.h>
#include <async/core/scheduling/declarations/enums.h>

#include <array>
#include <utility>

LCF_ASYNC_EXPORT
namespace lcf::async {

template <producer_queue_c Queue, notify::notifier_c Notifier>
class SubmissionSink
{
public:
    using value_t = typename Queue::value_t;
public:
    ~SubmissionSink() noexcept = default;
    SubmissionSink() = delete;
    explicit SubmissionSink(Notifier notifier) noexcept(details::all_nothrow_default_constructible_c<Queue>
        and details::all_nothrow_move_constructible_c<Notifier>) : m_notifier(std::move(notifier)) {}
    SubmissionSink(const SubmissionSink &) = delete;
    SubmissionSink(SubmissionSink &&) = delete;
    SubmissionSink & operator=(const SubmissionSink &) = delete;
    SubmissionSink & operator=(SubmissionSink &&) = delete;
public:
    std::error_code receive(value_t & record) noexcept
    {
        const std::size_t band = static_cast<std::size_t>(record.getPriority());
        if (band >= m_queues.size()) { return std::make_error_code(std::errc::invalid_argument); }
        const auto ec = m_queues[band].enqueue(record);
        if (not ec) { m_notifier.notifyAll(); }
        return ec;
    }
    Queue & getQueue(std::size_t band) noexcept { return m_queues[band]; }
private:
    std::array<Queue, k_priority_count> m_queues;
    Notifier m_notifier;
};

} // namespace lcf::async
