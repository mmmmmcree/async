#include <NativeApi.h>
#include <gtest/gtest.h>
#include <memory>

/*
 * Lesson 28: Signal is a one-shot notification with current and late waiters.
 * Predict: set schedules one subscriber at a time on its executor; late
 * subscribers also observe the stored signal. Repeated set returns false.
 * Read the TEST and Receiver, then Notification::dispatchNext and invoke.
 * Signal has no reset API. A new independent round requires a fresh Signal;
 * do not invent reset semantics from other libraries' similarly named types.
 * Vary: cancellation before set, a late subscriber, then a fresh round.
 * Receiver markers identify when subscriber lifetime ends after completion.
 * Source map: Signal.h, details/notification.h, notification_state.h,
 * CompletionEvent.h (the separate blocking completion primitive).
 */
namespace {
namespace ex = lcf::async::exec;
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value() noexcept { ++m_values; m_marker.reset(); }
    void set_stopped() noexcept { ++m_stopped; m_marker.reset(); }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    int & m_values; int & m_stopped; std::shared_ptr<int> m_marker;
};
TEST(CoroutineLessons, Lesson28SignalsAndNotifications)
{
    lcf::async::Context context;
    lcf::async::Signal signal {context.getExecutor()};
    std::stop_source stop;
    int values = 0, stopped = 0;
    auto marker = std::make_shared<int>(42);
    std::weak_ptr<int> observer = marker;
    auto canceled = ex::connect(ex::with_stop_token(signal.wait(), stop.get_token()), Receiver {values, stopped, {}});
    auto first = ex::connect(signal.wait(), Receiver {values, stopped, std::move(marker)});
    auto second = ex::connect(signal.wait(), Receiver {values, stopped, {}});
    ex::start(canceled); ex::start(first); ex::start(second);
    EXPECT_EQ(context.getReadyCount(), 0u);
    stop.request_stop();
    EXPECT_EQ(stopped, 1);
    EXPECT_FALSE(observer.expired());
    EXPECT_TRUE(signal.set());
    EXPECT_FALSE(signal.set());
    EXPECT_EQ(values, 0);
    EXPECT_EQ(context.getReadyCount(), 1u);
    EXPECT_EQ(context.poll({.m_max_callbacks = 1}).m_callbacks, 1u);
    EXPECT_EQ(values, 1);
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(context.getReadyCount(), 1u);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(values, 2);
    auto late = ex::connect(signal.wait(), Receiver {values, stopped, {}});
    ex::start(late);
    EXPECT_EQ(values, 2);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(values, 3);
    lcf::async::Signal next_round {context.getExecutor()};
    auto fresh = ex::connect(next_round.wait(), Receiver {values, stopped, {}});
    ex::start(fresh);
    EXPECT_EQ(context.pollOne(), 0u);
    EXPECT_TRUE(next_round.set());
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(values, 4);
    EXPECT_EQ(stopped, 1);
}
}
