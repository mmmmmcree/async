#pragma once

#include <async/core/sink/declarations/concepts.h>
#include <utility>

namespace lcf::async::details {

template <rejecting_sink_c Sink, typename Coordinator>
class SubmissionGate
{
public:
    using value_t = typename Sink::value_t;

public:
    ~SubmissionGate() noexcept = default;
    SubmissionGate() = delete;
    SubmissionGate(Sink & sink, Coordinator & coordinator) noexcept :
        m_sink_p(&sink), m_coordinator_p(&coordinator) {}
    SubmissionGate(const SubmissionGate &) noexcept = default;
    SubmissionGate(SubmissionGate &&) noexcept = default;
    SubmissionGate & operator=(const SubmissionGate &) noexcept = default;
    SubmissionGate & operator=(SubmissionGate &&) noexcept = default;

    std::error_code receive(value_t & record) noexcept
    {
        if (not m_coordinator_p->beginSubmission()) { return std::make_error_code(std::errc::operation_canceled); }
        const std::error_code ec = m_sink_p->receive(record);
        if (ec) { m_coordinator_p->complete(); }
        m_coordinator_p->endSubmission();
        return ec;
    }

private:
    Sink * m_sink_p;
    Coordinator * m_coordinator_p;
};

template <sink_c Sink, typename Coordinator>
requires (not rejecting_sink_c<Sink>)
class CompletionTracker
{
public:
    using value_t = typename Sink::value_t;

public:
    ~CompletionTracker() noexcept = default;
    CompletionTracker() = delete;
    CompletionTracker(Sink sink, Coordinator & coordinator) noexcept :
        m_sink(std::move(sink)), m_coordinator_p(&coordinator) {}
    CompletionTracker(const CompletionTracker &) = default;
    CompletionTracker(CompletionTracker &&) = default;
    CompletionTracker & operator=(const CompletionTracker &) = default;
    CompletionTracker & operator=(CompletionTracker &&) = default;

    void receive(value_t & record) noexcept
    {
        m_sink.receive(record);
        m_coordinator_p->complete();
    }

private:
    Sink m_sink;
    Coordinator * m_coordinator_p;
};

} // namespace lcf::async::details
