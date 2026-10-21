#include <NativeApi.h>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

/*
 * Lesson 27: the clock makes deadlines eligible; a context selects and delivers them.
 * Predict: constructing and connecting do not start the deadline. An active
 * context delivers timers only when pumped; readiness alone is not delivery.
 * Read the TEST, Receiver, then TimerRegistration::start/invoke/complete.
 * The context owns a callback pump and a timer service through EventSource.
 * TimerService owns the pending min-heap and uses the context transaction.
 * There is no timer-service thread. getReadyCount observes expired entries
 * without delivering them; pollOne selects at most one timer or callback.
 * runUntilIdle can instead sleep until the next deadline and deliver it.
 * Thus pollOne returning zero does not mean a future timer has disappeared.
 * Four experiments: context isolation, a budgeted expired backlog, cancellation
 * across stop/restart, then permanent shutdown and receiver ownership release.
 * Zero-duration waits make expiry deterministic; one-hour waits stay pending.
 * No sleep or elapsed-time assertion is needed. Shutdown abandons pending
 * receivers without a terminal call; starting on a closed context instead
 * reports stopped immediately. Neither behavior means "pause until restart".
 * Heap allocation belongs to Context; Lesson 29 separates that resource from
 * a delay registration's resource and a coroutine frame's resource.
 * Source map: timer/operations.h, senders.h, details/TimerService.h,
 * details/timer.h, timer_wait.h, and scheduling/details/EventSource.h.
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
    std::shared_ptr<int> m_marker_sp;
};
TEST(CoroutineLessons, Lesson27Timers)
{
    using namespace std::chrono_literals;
    lcf::async::Context context;
    lcf::async::Context other_context;
    const auto caller = std::this_thread::get_id();

    // 1. Only the selected context can deliver this already-eligible deadline.
    {
        Outcome outcome;
        auto operation = ex::connect(lcf::async::schedule_after(context.getScheduler(), 0ns), Receiver {outcome});
        EXPECT_EQ(context.getReadyCount(), 0u);
        ex::start(operation);
        EXPECT_EQ(context.getReadyCount(), 1u);
        EXPECT_EQ(other_context.getReadyCount(), 0u);
        EXPECT_EQ(other_context.pollOne(), 0u);
        EXPECT_EQ(outcome.m_values, 0);
        EXPECT_EQ(context.pollOne(), 1u);
        EXPECT_EQ(outcome.m_values, 1);
        EXPECT_EQ(outcome.m_stopped + outcome.m_errors, 0);
        EXPECT_EQ(outcome.m_thread, caller);
        EXPECT_EQ(context.pollOne(), 0u);
    }

    // 2. Two expired deadlines remain two admissions, even if both are ready.
    {
        Outcome outcome;
        auto first = ex::connect(lcf::async::schedule_after(context.getScheduler(), 0ns), Receiver {outcome});
        auto second = ex::connect(lcf::async::schedule_after(context.getScheduler(), 0ns), Receiver {outcome});
        ex::start(first);
        ex::start(second);
        EXPECT_EQ(context.getReadyCount(), 2u);
        EXPECT_EQ(context.poll({.m_max_callbacks = 1}).m_callbacks, 1u);
        EXPECT_EQ(outcome.m_values, 1);
        EXPECT_EQ(context.getReadyCount(), 1u);
        EXPECT_EQ(context.pollOne(), 1u);
        EXPECT_EQ(outcome.m_values, 2);
        EXPECT_EQ(outcome.m_stopped + outcome.m_errors, 0);
    }

    // 3. An empty poll can still have outstanding work. Stop pauses delivery.
    {
        Outcome outcome;
        std::stop_source stop;
        auto sender = ex::with_stop_token(lcf::async::schedule_after(context.getScheduler(), 1h), stop.get_token());
        auto operation = ex::connect(std::move(sender), Receiver {outcome});
        ex::start(operation);
        EXPECT_EQ(context.getReadyCount(), 0u);
        EXPECT_EQ(context.pollOne(), 0u);
        context.stop();
        EXPECT_TRUE(stop.request_stop());
        EXPECT_EQ(context.getReadyCount(), 1u);
        EXPECT_EQ(context.pollOne(), 0u);
        EXPECT_EQ(outcome.m_stopped, 0);
        context.restart();
        EXPECT_EQ(context.pollOne(), 1u);
        EXPECT_EQ(outcome.m_stopped, 1);
        EXPECT_EQ(outcome.m_values + outcome.m_errors, 0);
        EXPECT_EQ(outcome.m_thread, caller);
        EXPECT_FALSE(stop.request_stop());
        EXPECT_EQ(context.pollOne(), 0u);
    }

    // 4. Shutdown releases pending ownership and permanently closes the context.
    {
        Outcome abandoned;
        auto marker_sp = std::make_shared<int>(42);
        const std::weak_ptr<int> observer_wp = marker_sp;
        const auto scheduler = context.getScheduler();
        auto operation = ex::connect(lcf::async::schedule_after(scheduler, 1h), Receiver {abandoned, std::move(marker_sp)});
        ex::start(operation);
        EXPECT_FALSE(observer_wp.expired());
        context.shutdown();
        EXPECT_TRUE(observer_wp.expired());
        EXPECT_EQ(abandoned.m_values + abandoned.m_stopped + abandoned.m_errors, 0);
        context.restart();
        EXPECT_EQ(context.pollOne(), 0u);

        Outcome rejected;
        auto new_operation = ex::connect(lcf::async::schedule_after(scheduler, 0ns), Receiver {rejected});
        ex::start(new_operation);
        EXPECT_EQ(rejected.m_stopped, 1);
        EXPECT_EQ(rejected.m_values + rejected.m_errors, 0);
        EXPECT_EQ(context.pollOne(), 0u);
    }
}
}
