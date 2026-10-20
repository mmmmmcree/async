#include <NativeApi.h>
#include <gtest/gtest.h>
#include <thread>

/*
 * Lesson 27: a timer service makes deadlines ready; a context delivers them.
 * Predict: constructing and connecting do not start the deadline. Completion
 * is queued to the selected scheduler, so readiness alone is not delivery.
 * Read the TEST, Receiver, then TimerRegistration::start/finish/invoke.
 * This backend has a timer-service thread; the context queue itself holds
 * ready callbacks, while TimerService owns pending timer entries. This is why
 * polling an empty context does not mean a pending timer has disappeared.
 * Vary: an expiring short wait and a one-hour wait canceled before expiry.
 * Clock waits are bounded; no exact elapsed duration is asserted.
 * Source map: timer/operations.h, senders.h, details/timer.h and timer_wait.h.
 */
namespace {
namespace ex = lcf::async::exec;
struct Outcome { int m_values = 0, m_stopped = 0, m_errors = 0; std::thread::id m_thread; };
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value() noexcept { ++m_out.m_values; m_out.m_thread = std::this_thread::get_id(); }
    void set_stopped() noexcept { ++m_out.m_stopped; m_out.m_thread = std::this_thread::get_id(); }
    void set_error(std::exception_ptr) noexcept { ++m_out.m_errors; }
    Outcome & m_out;
};
TEST(CoroutineLessons, Lesson27Timers)
{
    using namespace std::chrono_literals;
    lcf::async::Context context;
    const auto caller = std::this_thread::get_id();
    for (bool cancel : {false, true}) {
        SCOPED_TRACE(cancel);
        Outcome outcome;
        std::stop_source stop;
        auto sender = ex::with_stop_token(lcf::async::schedule_after(context.getScheduler(), cancel ? 1h : 1ms), stop.get_token());
        auto operation = ex::connect(std::move(sender), Receiver {outcome});
        struct Cleanup
        {
            std::stop_source & m_stop; lcf::async::Context & m_context;
            ~Cleanup() { m_stop.request_stop(); m_context.poll(); }
        } cleanup {stop, context}; // Release a pending wait even if a bounded assertion fails.
        EXPECT_EQ(context.getReadyCount(), 0u);
        ex::start(operation);
        EXPECT_EQ(outcome.m_values + outcome.m_stopped, 0);
        if (cancel) { EXPECT_TRUE(stop.request_stop()); }
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (context.getReadyCount() == 0 && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
        ASSERT_EQ(context.getReadyCount(), 1u);
        EXPECT_EQ(outcome.m_values + outcome.m_stopped, 0);
        EXPECT_EQ(context.pollOne(), 1u);
        EXPECT_EQ(outcome.m_values, cancel ? 0 : 1);
        EXPECT_EQ(outcome.m_stopped, cancel ? 1 : 0);
        EXPECT_EQ(outcome.m_errors, 0);
        EXPECT_EQ(outcome.m_thread, caller);
        stop.request_stop();
        EXPECT_EQ(context.pollOne(), 0u);
        EXPECT_EQ(outcome.m_values + outcome.m_stopped, 1);
    }
}
}
