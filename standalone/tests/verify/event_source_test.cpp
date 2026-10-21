#include <async/core/scheduling/details/queue.h>
#include <gtest/gtest.h>
#include <array>

namespace {

namespace scheduling = lcf::async::details;

/* Objective: the callback queue works without a timer or any event source.
 * Input: one borrowed callback and a source-free queue.
 * Procedure: enqueue, inspect readiness and pop the callback.
 * Expected: one invocation and no dependency on timer/execution headers.
 * Failure hints: inspect queue.h's includes and direct ready path. */
void source_free_queue();
TEST(EventSource, SourceFreeQueue) { source_free_queue(); }

/* Objective: a different event source can participate in the same pump.
 * Input: two synthetic events and an ordinary callback behind the marker.
 * Procedure: admit one event, the ordinary callback, and the remaining event.
 * Expected: FIFO fairness and exactly one admitted operation per pop.
 * Failure hints: inspect EventPoll and Queue::popSlow, not timer storage. */
void synthetic_events_share_the_pump();
TEST(EventSource, SyntheticEventsShareThePump) { synthetic_events_share_the_pump(); }

/* Objective: the context owns its source and closes it outside the queue mutex.
 * Input: a synthetic source whose shutdown updates queue work accounting.
 * Procedure: close twice, then destroy the queue.
 * Expected: one shutdown, no lock recursion, and source destruction with its owner.
 * Failure hints: inspect Queue::shutdown and EventSource's unique ownership. */
void source_lifetime_and_shutdown();
TEST(EventSource, LifetimeAndShutdown) { source_lifetime_and_shutdown(); }

struct Counters
{
    unsigned m_calls = 0;
    unsigned m_polls = 0;
    unsigned m_closed = 0;
    unsigned m_destroyed = 0;
    std::array<unsigned, 3> m_order {};
};

struct Record
{
    using Self = Record;
    Record(Counters & counters, unsigned tag) noexcept :
        m_counters(counters), m_tag(tag), m_callback(scheduling::Callback::make<&Self::invoke>(this)) {}
    static void invoke(scheduling::Callback * callback_p) noexcept
    {
        auto & record = *static_cast<Self *>(callback_p->m_owner_p);
        record.m_counters.m_order[record.m_counters.m_calls++] = record.m_tag;
    }
    Counters & m_counters;
    unsigned m_tag;
    scheduling::Callback m_callback;
};

class SyntheticSource
{
public:
    ~SyntheticSource() noexcept { ++m_counters.m_destroyed; }
    SyntheticSource(scheduling::Queue & queue, Counters & counters) noexcept :
        m_queue(queue), m_counters(counters), m_records {Record {counters, 1}, Record {counters, 2}} {}
public:
    scheduling::EventPoll poll() noexcept
    {
        ++m_counters.m_polls;
        if (m_next == m_records.size()) { return {}; }
        auto * callback_p = &m_records[m_next++].m_callback;
        const auto deadline = m_next == m_records.size() ? std::chrono::steady_clock::time_point::max() : std::chrono::steady_clock::time_point::min();
        return {callback_p, deadline};
    }
    std::size_t readyCount(std::chrono::steady_clock::time_point) const noexcept { return m_records.size() - m_next; }
    void shutdown() noexcept
    {
        ++m_counters.m_closed;
        m_next = m_records.size();
        m_queue.workStarted();
        m_queue.workFinished();
    }
private:
    scheduling::Queue & m_queue;
    Counters & m_counters;
    std::array<Record, 2> m_records;
    std::size_t m_next = 0;
};

void source_free_queue()
{
    Counters counters;
    Record record {counters, 9};
    scheduling::Queue queue;
    EXPECT_FALSE(static_cast<bool>(queue.getEventSource()));
    queue.push(&record.m_callback);
    EXPECT_EQ(queue.readyCount(), 1u);
    auto * callback_p = queue.pop(false);
    ASSERT_EQ(callback_p, &record.m_callback);
    callback_p->invoke();
    EXPECT_EQ(counters.m_calls, 1u);
    EXPECT_EQ(counters.m_order[0], 9u);
    EXPECT_EQ(queue.pop(false), nullptr);
}

void synthetic_events_share_the_pump()
{
    Counters counters;
    Record ordinary {counters, 9};
    scheduling::Queue queue {[&](scheduling::Queue & owner) {
        return scheduling::EventSource {std::make_unique<SyntheticSource>(owner, counters)};
    }};
    ASSERT_NE(queue.getEventSource().target<SyntheticSource>(), nullptr);
    queue.wakeEvents();
    queue.push(&ordinary.m_callback);
    EXPECT_EQ(queue.readyCount(), 3u);
    for (unsigned index = 0; index < 3; ++index) {
        auto * callback_p = queue.pop(false);
        ASSERT_NE(callback_p, nullptr) << index;
        callback_p->invoke();
        EXPECT_EQ(counters.m_calls, index + 1);
    }
    EXPECT_EQ(counters.m_order, (std::array<unsigned, 3> {1, 9, 2}));
    EXPECT_EQ(counters.m_polls, 2u);
    EXPECT_EQ(queue.readyCount(), 0u);
    EXPECT_EQ(queue.pop(false), nullptr);
}

void source_lifetime_and_shutdown()
{
    static_assert(not std::copy_constructible<scheduling::EventSource>);
    static_assert(std::move_constructible<scheduling::EventSource>);
    Counters counters;
    {
        scheduling::Queue queue {[&](scheduling::Queue & owner) {
            return scheduling::EventSource {std::make_unique<SyntheticSource>(owner, counters)};
        }};
        queue.shutdown();
        queue.shutdown();
        EXPECT_EQ(counters.m_closed, 1u);
        EXPECT_EQ(counters.m_destroyed, 0u);
        EXPECT_EQ(queue.pop(false), nullptr);
    }
    EXPECT_EQ(counters.m_closed, 1u);
    EXPECT_EQ(counters.m_destroyed, 1u);
}

}
