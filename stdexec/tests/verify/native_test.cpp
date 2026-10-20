#include <stdexec/execution.hpp>
#include <NativeApi.h>
#include <gtest/gtest.h>

namespace {
namespace runtime = lcf::async;
namespace stde = lcf::async::stde;

/*
 * Objective: cancellation before registration cannot leave a timer pending.
 * Input: an already stopped token and a one-hour timer.
 * Procedure: connect and start without running a worker context.
 * Expected: immediate single stopped completion.
 * Failure hints: inspect TimerService::cancel/submit and TimerRegistration::start.
 */
void pre_stopped_timer_completes();
TEST(StdexecNative, PreStoppedTimerCompletes) { pre_stopped_timer_completes(); }

/*
 * Objective: timer start and stop races have exactly one terminal event.
 * Input: one-hour timers and a stop request on another thread, repeated 64 times.
 * Procedure: race start against request_stop and join the requesting thread.
 * Expected: one cancelled completion for every iteration, without waiting an hour.
 * Failure hints: inspect the timer service's locked terminal decision and unlinking.
 */
void timer_start_races_stop();
TEST(StdexecNative, TimerStartRacesStop) { timer_start_races_stop(); }

/*
 * Objective: signal completion and cancellation safely choose one outcome.
 * Input: bound and unbound signals carrying an inplace stop token, repeated 64 times.
 * Procedure: race Signal::set against request_stop and join both threads.
 * Expected: exactly one value or cancellation, including cancellation after enqueue.
 * Failure hints: inspect NotificationRegistration::complete and subscription cleanup.
 */
void notification_races_stop();
TEST(StdexecNative, NotificationRacesStop) { notification_races_stop(); }

/*
 * Objective: an Awaitable coroutine can be consumed directly as a stdexec sender.
 * Input: a coroutine returning a unique_ptr containing 42, without a task chain.
 * Procedure: pass the returned object to sync_wait, inspect the value, then destroy it.
 * Expected: one value 42 and normal return without recursive coroutine destruction.
 * Failure hints: inspect the coroutine return object, promise, and direct sender connection.
 */
void awaitable_works_with_sync_wait();
TEST(StdexecNative, AwaitableWorksWithSyncWait) { awaitable_works_with_sync_wait(); }

struct Observation
{
    std::atomic_uint m_count = 0;
    std::exception_ptr m_exception;
    bool m_stopped = false;
};

struct Receiver
{
    using receiver_concept = stde::receiver_t;
    struct Environment
    {
        stde::inplace_stop_token m_token;
        stde::inplace_stop_token query(stde::get_stop_token_t) const noexcept { return m_token; }
    };
    Environment get_env() const noexcept { return {m_token}; }
    void set_value() noexcept { ++m_observation_p->m_count; }
    void set_error(std::exception_ptr exception) noexcept
    {
        m_observation_p->m_exception = std::move(exception);
        ++m_observation_p->m_count;
    }
    void set_stopped() noexcept { m_observation_p->m_stopped = true; ++m_observation_p->m_count; }
    stde::inplace_stop_token m_token;
    Observation * m_observation_p;
};

void expect_cancelled(const Observation & observation)
{
    ASSERT_EQ(observation.m_count, 1u);
    EXPECT_TRUE(observation.m_stopped);
    EXPECT_FALSE(observation.m_exception);
}

void pre_stopped_timer_completes()
{
    stde::inplace_stop_source stop_source;
    stop_source.request_stop();
    Observation observation;
    auto operation = stde::connect(runtime::delay(std::chrono::hours {1}), Receiver {stop_source.get_token(), &observation});
    stde::start(operation);
    expect_cancelled(observation);
}

void timer_start_races_stop()
{
    for (unsigned index = 0; index < 64; ++index) {
        SCOPED_TRACE(index);
        stde::inplace_stop_source stop_source;
        Observation observation;
        auto operation = stde::connect(runtime::delay(std::chrono::hours {1}), Receiver {stop_source.get_token(), &observation});
        std::jthread stopper {[&] { stop_source.request_stop(); }};
        stde::start(operation);
        stopper.join();
        expect_cancelled(observation);
    }
}

void notification_races_stop()
{
    for (unsigned index = 0; index < 64; ++index) {
        SCOPED_TRACE(index);
        runtime::Context context;
        runtime::Signal direct_signal;
        runtime::Signal queued_signal {context.getExecutor()};
        auto & signal = index % 2u == 0u ? direct_signal : queued_signal;
        stde::inplace_stop_source stop_source;
        Observation observation;
        auto operation = stde::connect(signal.wait(), Receiver {stop_source.get_token(), &observation});
        stde::start(operation);
        std::jthread producer {[&] { signal.set(); }};
        std::jthread stopper {[&] { stop_source.request_stop(); }};
        producer.join();
        stopper.join();
        context.poll();
        ASSERT_EQ(observation.m_count, 1u);
        if (observation.m_stopped) { expect_cancelled(observation); }
    }
}

void awaitable_works_with_sync_wait()
{
    const auto make_value = []() -> runtime::Awaitable<std::unique_ptr<int>> {
        co_return std::make_unique<int>(42);
    };
    auto result = stde::sync_wait(make_value());
    ASSERT_TRUE(result);
    const auto & value_up = std::get<0>(*result);
    ASSERT_TRUE(value_up);
    EXPECT_EQ(*value_up, 42);
}

}
