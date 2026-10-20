#include <NativeApi.h>
#include <gtest/gtest.h>
#include <vector>

namespace {

namespace runtime = lcf::async;
namespace stde = lcf::async::exec;

/*
 * Objective: a count budget leaves excess ready callbacks queued.
 * Input: ten callbacks and a budget of three.
 * Procedure: poll twice, inspect ready counts, then drain the remaining callbacks.
 * Expected: 3, 3, and 4 callbacks; FIFO order and a final empty queue.
 * Failure hints: inspect Context::poll and Queue's ready count under its mutex.
 */
void count_budget_limits_admission();
TEST(StdexecPoll, CountBudgetLimitsAdmission) { count_budget_limits_admission(); }

/*
 * Objective: zero count or time budgets execute no callbacks.
 * Input: one queued callback and each zero budget.
 * Procedure: poll under both limits, then call pollOne.
 * Expected: both reports contain zero callbacks; pollOne finally executes the callback.
 * Failure hints: inspect budget checks before dequeue rather than after invocation.
 */
void empty_budgets_keep_callbacks_ready();
TEST(StdexecPoll, EmptyBudgetsKeepCallbacksReady) { empty_budgets_keep_callbacks_ready(); }

/*
 * Objective: time budgets stop admission after an overrun without preempting user code.
 * Input: a 20ms callback, another ready callback, and a 1ms budget.
 * Procedure: poll until the first callback executes; bound the overall wait at five seconds.
 * Expected: the callback finishes; the report exceeds its time budget; the second stays queued.
 * Failure hints: inspect the clock check between callbacks; this is a cooperative budget.
 */
void slow_callback_reports_cooperative_overrun();
TEST(StdexecPoll, SlowCallbackReportsCooperativeOverrun) { slow_callback_reports_cooperative_overrun(); }

/*
 * Objective: fanout notification itself respects the queue's cooperative boundaries.
 * Input: 64 receivers waiting on one signal bound to the control executor.
 * Procedure: signal once, then poll with a one-callback budget 64 times.
 * Expected: signalling queues one step; each poll notifies exactly one receiver.
 * Failure hints: inspect Notification::dispatchNext and the registration's queued ownership.
 */
void fanout_notifications_are_incremental();
TEST(StdexecPoll, FanoutNotificationsAreIncremental) { fanout_notifications_are_incremental(); }

/*
 * Objective: stop halts admission even when callbacks remain queued.
 * Input: a callback that stops its context followed by a second callback.
 * Procedure: run, inspect the pending callback, restart and poll again.
 * Expected: stop returns after the first; restart admits the remaining callback.
 * Failure hints: inspect Queue::pop's stopped check before dequeue.
 */
void stop_preserves_queued_callbacks_until_restart();
TEST(StdexecPoll, StopPreservesQueuedCallbacksUntilRestart) { stop_preserves_queued_callbacks_until_restart(); }

struct CountReceiver
{
    using receiver_concept = stde::receiver_t;
    void set_value() noexcept { ++*m_calls_p; }
    void set_error(std::exception_ptr) noexcept { ++*m_failures_p; }
    void set_stopped() noexcept { ++*m_failures_p; }
    unsigned * m_calls_p;
    unsigned * m_failures_p;
};

void count_budget_limits_admission()
{
    runtime::Context context;
    unsigned next = 0;
    for (unsigned index = 0; index < 10; ++index) {
        context.getExecutor().post([&, index] { EXPECT_EQ(next++, index); });
    }
    EXPECT_EQ(context.getReadyCount(), 10u);
    const auto first = context.poll({.m_max_callbacks = 3});
    EXPECT_EQ(first.m_callbacks, 3u);
    EXPECT_TRUE(first.m_count_limit_reached);
    EXPECT_EQ(context.getReadyCount(), 7u);
    EXPECT_EQ(context.poll({.m_max_callbacks = 3}).m_callbacks, 3u);
    EXPECT_EQ(context.poll().m_callbacks, 4u);
    EXPECT_EQ(context.getReadyCount(), 0u);
    EXPECT_EQ(next, 10u);
}

void empty_budgets_keep_callbacks_ready()
{
    runtime::Context context;
    unsigned calls = 0;
    context.getExecutor().post([&] { ++calls; });
    EXPECT_EQ(context.poll({.m_max_callbacks = 0}).m_callbacks, 0u);
    EXPECT_EQ(context.poll({.m_max_duration = std::chrono::milliseconds {0}}).m_callbacks, 0u);
    EXPECT_EQ(context.getReadyCount(), 1u);
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(calls, 1u);
}

void slow_callback_reports_cooperative_overrun()
{
    runtime::Context context;
    unsigned calls = 0;
    context.getExecutor().post([&] {
        const auto finish = std::chrono::steady_clock::now() + std::chrono::milliseconds {20};
        while (std::chrono::steady_clock::now() < finish) { std::this_thread::yield(); }
        ++calls;
    });
    context.getExecutor().post([&] { ++calls; });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {5};
    runtime::PollReport report;
    do {
        report = context.poll({.m_max_duration = std::chrono::milliseconds {1}});
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
    } while (report.m_callbacks == 0u);
    EXPECT_EQ(report.m_callbacks, 1u);
    EXPECT_EQ(calls, 1u);
    EXPECT_TRUE(report.m_time_limit_reached);
    EXPECT_GE(report.m_elapsed, std::chrono::milliseconds {20});
    EXPECT_EQ(context.getReadyCount(), 1u);
    EXPECT_EQ(context.pollOne(), 1u);
}

void fanout_notifications_are_incremental()
{
    runtime::Context context;
    runtime::Signal signal {context.getExecutor()};
    unsigned calls = 0;
    unsigned failures = 0;
    using Operation = stde::connect_result_t<decltype(signal.wait()), CountReceiver>;
    std::vector<std::unique_ptr<Operation>> operations;
    for (unsigned index = 0; index < 64; ++index) {
        operations.emplace_back(new Operation(stde::connect(signal.wait(), CountReceiver {&calls, &failures})));
        stde::start(*operations.back());
    }
    EXPECT_EQ(context.getReadyCount(), 0u);
    ASSERT_TRUE(signal.set());
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(context.getReadyCount(), 1u);
    for (unsigned index = 0; index < 64; ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(context.poll({.m_max_callbacks = 1}).m_callbacks, 1u);
        EXPECT_EQ(calls, index + 1);
        EXPECT_EQ(context.getReadyCount(), index == 63 ? 0u : 1u);
    }
    EXPECT_EQ(failures, 0u);
}

void stop_preserves_queued_callbacks_until_restart()
{
    runtime::Context context;
    unsigned calls = 0;
    context.getExecutor().post([&] { ++calls; context.stop(); });
    context.getExecutor().post([&] { ++calls; });
    context.run();
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(context.getReadyCount(), 1u);
    EXPECT_EQ(context.pollOne(), 0u);
    context.restart();
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(calls, 2u);
}

}
