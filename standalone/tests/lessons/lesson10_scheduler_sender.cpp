#include <NativeApi.h>
#include <gtest/gtest.h>

/*
 * Lesson 10: a scheduler manufactures a sender describing a queue boundary.
 * Predict: connect does not enqueue; start enqueues without completing.
 * Read the TEST, Receiver::get_env, then ScheduledOperation::start/invoke.
 * A receiver environment supplies execution properties through query objects.
 * Here get_scheduler identifies the context available to nested work. The
 * schedule sender also advertises its value-completion scheduler.
 * Vary: inspect readiness before and after start and pollOne.
 * Source map: Scheduler.h, details/scheduled_operation.h, protocol queries.
 */
namespace {
namespace ex = lcf::async::exec;
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    auto get_env() const noexcept { return ex::prop {ex::get_scheduler, m_scheduler}; }
    void set_value() noexcept { ++m_values; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    lcf::async::Scheduler m_scheduler;
    int & m_values;
};
TEST(CoroutineLessons, Lesson10SchedulerAsSenderFactory)
{
    lcf::async::Context context;
    const auto scheduler = context.getScheduler();
    int values = 0;
    Receiver receiver {scheduler, values};
    EXPECT_EQ(ex::get_scheduler(ex::get_env(receiver)), scheduler);
    auto description = ex::schedule(scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(description)), scheduler);
    auto operation = ex::connect(description, receiver);
    EXPECT_EQ(context.getReadyCount(), 0u);
    ex::start(operation);
    EXPECT_EQ(values, 0);
    EXPECT_EQ(context.getReadyCount(), 1u);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(values, 1);
    EXPECT_EQ(context.getReadyCount(), 0u);
}
}
