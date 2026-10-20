#include <Observation.h>
#include <gtest/gtest.h>
#include <expected>
#include <memory_resource>
#include <functional>
#include <barrier>

namespace {

namespace async = lcf::async;
namespace ex = async::exec;
namespace test = async::testing;
using namespace std::chrono_literals;

/*
 * Objective: core post is lazy and executes on the polling thread.
 * Input: the graph and controlled resources in post_defers_callback.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect core completion forwarding and operation ownership.
 */
void post_defers_callback();
TEST(Core, PostDefersCallback) { post_defers_callback(); }

/*
 * Objective: core coroutine yield separates user stages.
 * Input: the graph and controlled resources in yield_keeps_explicit_boundary.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect core completion forwarding and operation ownership.
 */
void yield_keeps_explicit_boundary();
TEST(Core, YieldKeepsExplicitBoundary) { yield_keeps_explicit_boundary(); }

/*
 * Objective: the coroutine bridge does not require a default constructed result.
 * Input: the graph and controlled resources in coroutine_non_default_value.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect core completion forwarding and operation ownership.
 */
void coroutine_non_default_value();
TEST(Core, CoroutineNonDefaultValue) { coroutine_non_default_value(); }

/*
 * Objective: coroutine exceptions preserve their error type.
 * Input: the graph and controlled resources in coroutine_error_is_preserved.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect core completion forwarding and operation ownership.
 */
void coroutine_error_is_preserved();
TEST(Core, CoroutineErrorIsPreserved) { coroutine_error_is_preserved(); }

/*
 * Objective: pre-cancellation prevents a long timer from waiting.
 * Input: the graph and controlled resources in timer_pre_stop_skips_work.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect core completion forwarding and operation ownership.
 */
void timer_pre_stop_skips_work();
TEST(Core, TimerPreStopSkipsWork) { timer_pre_stop_skips_work(); }

/*
 * Objective: timer cancellation registration chooses one terminal completion.
 * Input: the graph and controlled resources in timer_start_races_stop.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect core completion forwarding and operation ownership.
 */
void timer_start_races_stop();
TEST(Core, TimerStartRacesStop) { timer_start_races_stop(); }

/*
 * Objective: signal broadcasts and caches its ready state.
 * Input: the graph and controlled resources in signal_current_and_late_waiters.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect core completion forwarding and operation ownership.
 */
void signal_current_and_late_waiters();
TEST(Core, SignalCurrentAndLateWaiters) { signal_current_and_late_waiters(); }

/*
 * Objective: canceling one subscriber leaves other signal waiters usable.
 * Input: the graph and controlled resources in cancel_one_signal_waiter.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect core completion forwarding and operation ownership.
 */
void cancel_one_signal_waiter();
TEST(Core, CancelOneSignalWaiter) { cancel_one_signal_waiter(); }

/*
 * Objective: concurrent set cannot lose a notification.
 * Input: the graph and controlled resources in signal_set_races_registration.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect core completion forwarding and operation ownership.
 */
void signal_set_races_registration();
TEST(Core, SignalSetRacesRegistration) { signal_set_races_registration(); }

/*
 * Objective: retaining a connected timer does not consume its requested delay.
 * Input: a 40ms timer held for 60ms before start.
 * Procedure: connect, wait past the duration, start and observe its completion time.
 * Expected: completion occurs at least 40ms after start.
 * Failure hints: inspect when TimerRegistration assigns its deadline.
 */
void timer_duration_begins_at_start();
TEST(Core, TimerDurationBeginsAtStart) { timer_duration_begins_at_start(); }

async::Awaitable<void> two_stages(unsigned & stages)
{
    ++stages;
    co_await async::yield();
    ++stages;
}
struct NonDefault
{
    explicit NonDefault(int value) noexcept : m_value(value) {}
    int m_value;
};
async::Awaitable<NonDefault> non_default() { co_return NonDefault {42}; }
async::Awaitable<int> throwing_coroutine()
{
    throw std::system_error(std::make_error_code(std::errc::permission_denied));
    co_return 0;
}

void post_defers_callback()
{
    test::Context context;
    unsigned calls = 0;
    auto thread = std::this_thread::get_id();
    async::post(context.get_executor(), [&] { ++calls; EXPECT_EQ(std::this_thread::get_id(), thread); });
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(context.poll_one(), 1u);
    EXPECT_EQ(calls, 1u);
}

void yield_keeps_explicit_boundary()
{
    test::Context context;
    unsigned stages = 0;
    test::Observation observation;
    auto operation = ex::connect(async::as_sender(context.getScheduler(), two_stages(stages)), test::Receiver {&observation});
    ex::start(operation);
    EXPECT_EQ(stages, 0u);
    while (not observation.m_done.load()) {
        unsigned before = stages;
        context.poll_one();
        ASSERT_LE(stages - before, 1u);
    }
    EXPECT_EQ(stages, 2u);
    EXPECT_EQ(observation.m_value_calls, 1u);
}

void coroutine_non_default_value()
{
    test::Worker worker;
    auto result = ex::sync_wait(async::as_sender(worker.getScheduler(), non_default()));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result).m_value, 42);
}

void coroutine_error_is_preserved()
{
    test::Worker worker;
    try {
        ex::sync_wait(async::as_sender(worker.getScheduler(), throwing_coroutine()));
        FAIL() << "Expected system_error";
    } catch (const std::system_error & error) { EXPECT_EQ(error.code(), std::errc::permission_denied); }
}

void timer_pre_stop_skips_work()
{
    test::Context context;
    std::stop_source stop;
    stop.request_stop();
    test::Observation observation;
    test::run(context, ex::with_stop_token(async::schedule_after(context.getScheduler(), 1h), stop.get_token()), observation);
    EXPECT_EQ(observation.m_stopped_calls, 1u);
    EXPECT_EQ(observation.m_error_calls, 0u);
}

void timer_start_races_stop()
{
    for (unsigned index = 0; index < 64; ++index) {
        SCOPED_TRACE(index);
        test::Context context;
        std::stop_source stop;
        test::Observation observation;
        auto operation = ex::connect(ex::with_stop_token(async::schedule_after(context.getScheduler(), 1h), stop.get_token()), test::Receiver {&observation});
        std::jthread stopper {[&] { stop.request_stop(); }};
        ex::start(operation);
        stopper.join();
        test::pump_until(context, [&] { return observation.m_done.load(); });
        context.poll();
        EXPECT_EQ(observation.m_stopped_calls, 1u);
        EXPECT_EQ(observation.m_value_calls + observation.m_error_calls, 0u);
    }
}

void signal_current_and_late_waiters()
{
    test::Context context;
    async::Signal signal {context.get_executor()};
    test::Observation first;
    test::Observation second;
    auto one = ex::connect(async::as_sender(context.getScheduler(), signal.wait()), test::Receiver {&first});
    auto two = ex::connect(async::as_sender(context.getScheduler(), signal.wait()), test::Receiver {&second});
    ex::start(one); ex::start(two); context.poll();
    EXPECT_FALSE(first.m_done.load()); EXPECT_FALSE(second.m_done.load());
    EXPECT_TRUE(signal.set()); EXPECT_FALSE(signal.set());
    test::pump_until(context, [&] { return first.m_done.load() and second.m_done.load(); });
    test::Observation late;
    test::run(context, async::as_sender(context.getScheduler(), signal.wait()), late);
    EXPECT_EQ(first.m_value_calls, 1u); EXPECT_EQ(second.m_value_calls, 1u); EXPECT_EQ(late.m_value_calls, 1u);
}

void cancel_one_signal_waiter()
{
    test::Context context;
    async::Signal signal;
    std::stop_source stop;
    test::Observation canceled;
    test::Observation active;
    auto one = ex::connect(ex::with_stop_token(async::as_sender(context.getScheduler(), signal.wait()), stop.get_token()), test::Receiver {&canceled});
    auto two = ex::connect(async::as_sender(context.getScheduler(), signal.wait()), test::Receiver {&active});
    ex::start(one); ex::start(two); context.poll();
    stop.request_stop();
    test::pump_until(context, [&] { return canceled.m_done.load(); });
    EXPECT_EQ(canceled.m_stopped_calls, 1u);
    EXPECT_FALSE(active.m_done.load());
    signal.set();
    test::pump_until(context, [&] { return active.m_done.load(); });
    EXPECT_EQ(active.m_value_calls, 1u);
}

void signal_set_races_registration()
{
    for (unsigned index = 0; index < 64; ++index) {
        SCOPED_TRACE(index);
        test::Context context;
        async::Signal signal;
        test::Observation observation;
        auto operation = ex::connect(async::as_sender(context.getScheduler(), signal.wait()), test::Receiver {&observation});
        std::jthread producer {[&] { signal.set(); }};
        ex::start(operation);
        producer.join();
        test::pump_until(context, [&] { return observation.m_done.load(); });
        EXPECT_EQ(observation.m_value_calls, 1u);
    }
}

void timer_duration_begins_at_start()
{
    test::Context context;
    test::Observation observation;
    auto operation = ex::connect(async::schedule_after(context.getScheduler(), 40ms), test::Receiver {&observation});
    std::this_thread::sleep_for(60ms);
    auto started = std::chrono::steady_clock::now();
    ex::start(operation);
    test::pump_until(context, [&] { return observation.m_done.load(); });
    EXPECT_GE(std::chrono::steady_clock::now() - started, 40ms);
    EXPECT_EQ(observation.m_value_calls, 1u);
}

}
