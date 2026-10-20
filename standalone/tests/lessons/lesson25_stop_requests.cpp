#include <NativeApi.h>
#include <gtest/gtest.h>

/*
 * Lesson 25: a stop request asks pending work to complete on the stopped edge.
 * Predict: a pre-stopped root never enters its body; a pending signal wait
 * skips code after co_await when stopped. Cancellation is cooperative, so a
 * request does not interrupt arbitrary currently executing C++ statements.
 * Read the TEST, pending(), Receiver; then compare two split subscriptions.
 * One canceled subscriber leaves its sibling active. A retained shared sender
 * is also a consumer: drop it before expecting the last subscription's stop
 * to propagate upstream. A stop request is not a second completion.
 * Vary: before start, while pending, one subscription, and all consumer handles.
 * Source map: cancellation/operations.h, execution/environment.h, split.h,
 * CoroutineState::stop and when_all.h's sibling stop source.
 */
namespace {
namespace ex = lcf::async::exec;
struct Outcome { int m_values = 0, m_errors = 0, m_stopped = 0; };
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    template <class... T> void set_value(T &&...) noexcept { ++m_out.m_values; }
    void set_error(std::exception_ptr) noexcept { ++m_out.m_errors; }
    void set_stopped() noexcept { ++m_out.m_stopped; }
    Outcome & m_out;
};
lcf::async::Awaitable<void> pending(lcf::async::Signal & signal, int & stages)
{ ++stages; co_await signal.wait(); ++stages; }
TEST(CoroutineLessons, Lesson25StopRequestsAndStoppedCompletion)
{
    for (bool pre_stopped : {true, false}) {
        SCOPED_TRACE(pre_stopped);
        lcf::async::Signal signal;
        std::stop_source stop;
        if (pre_stopped) { EXPECT_TRUE(stop.request_stop()); }
        int stages = 0;
        Outcome outcome;
        auto operation = ex::connect(ex::with_stop_token(pending(signal, stages), stop.get_token()), Receiver {outcome});
        ex::start(operation);
        EXPECT_EQ(stages, pre_stopped ? 0 : 1);
        if (!pre_stopped) { EXPECT_EQ(outcome.m_stopped, 0); EXPECT_TRUE(stop.request_stop()); }
        EXPECT_EQ(outcome.m_stopped, 1);
        EXPECT_EQ(outcome.m_values + outcome.m_errors, 0);
        EXPECT_FALSE(stop.request_stop());
        EXPECT_TRUE(signal.set());
        EXPECT_EQ(stages, pre_stopped ? 0 : 1);
        EXPECT_EQ(outcome.m_stopped, 1);
    }
    for (int scenario = 0; scenario < 3; ++scenario) {
        SCOPED_TRACE(scenario);
        const bool cancel_everyone = scenario != 0;
        const bool keep_sender = scenario == 2;
        lcf::async::Signal signal;
        std::stop_source first_stop, second_stop;
        int producers = 0;
        auto shared = std::optional {ex::split(signal.wait() | ex::then([&] { ++producers; return 42; }))};
        Outcome first, second;
        auto one = ex::connect(ex::with_stop_token(*shared, first_stop.get_token()), Receiver {first});
        auto two = ex::connect(ex::with_stop_token(*shared, second_stop.get_token()), Receiver {second});
        ex::start(one); ex::start(two);
        if (!keep_sender) { shared.reset(); }
        first_stop.request_stop();
        EXPECT_EQ(first.m_stopped, 1);
        EXPECT_EQ(second.m_values + second.m_stopped, 0);
        if (cancel_everyone) { second_stop.request_stop(); }
        EXPECT_TRUE(signal.set());
        EXPECT_EQ(second.m_stopped, cancel_everyone ? 1 : 0);
        EXPECT_EQ(second.m_values, cancel_everyone ? 0 : 1);
        // Even with both subscriptions canceled, a reusable sender handle
        // still counts as interest in the upstream result.
        EXPECT_EQ(producers, cancel_everyone && !keep_sender ? 0 : 1);
        EXPECT_EQ(first.m_errors + second.m_errors, 0);
    }
}
}
