#include <NativeApi.h>
#include <gtest/gtest.h>
#include <string_view>
#include <vector>

/*
 * Lesson 17: environment inheritance and continuation routing are separate.
 * Predict: child and root see the receiver scheduler. Awaiting schedule on
 * another context runs that sender there, then queues the coroutine back.
 * Read the TEST, parent(), child(), InspectEnvironment.
 * TaskAwaiter copies the parent's CoroutineEnvironment and saves its return
 * executor. With no executor change, child final_suspend transfers directly
 * to the parent. Root dispatch instead invokes its terminal receiver.
 * Vary: pump the foreign queue before the home queue and inspect the gap.
 * Source map: CoroutineEnvironment, CoroutineState::finish/dispatch,
 * TaskAwaiter::await_suspend and SenderAwaiter::publish.
 */
namespace {
namespace ex = lcf::async::exec;
using Events = std::vector<std::string_view>;
struct InspectEnvironment
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t()>;
    template <class R> struct Operation
    {
        void start() noexcept
        { m_observed = lcf::async::Executor {ex::get_scheduler(ex::get_env(m_receiver))}; ex::set_value(std::move(m_receiver)); }
        R m_receiver; lcf::async::Executor & m_observed;
    };
    template <class R> auto connect(R receiver) const { return Operation<R> {std::move(receiver), m_observed}; }
    lcf::async::Executor & m_observed;
};
lcf::async::Awaitable<int> child(lcf::async::Scheduler other, lcf::async::Executor & observed, Events & events)
{
    co_await InspectEnvironment {observed};
    events.push_back("child: before foreign sender");
    co_await ex::schedule(other);
    events.push_back("child: resumed at home");
    co_return 21;
}
lcf::async::Awaitable<int> parent(lcf::async::Scheduler other, lcf::async::Executor & observed, Events & events)
{
    events.push_back("parent: entry");
    const auto value = co_await child(other, observed, events);
    events.push_back("parent: child returned");
    co_return value * 2;
}
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    auto get_env() const noexcept { return ex::prop {ex::get_scheduler, m_home}; }
    void set_value(int value) noexcept { m_value = value; m_events.push_back("root: receiver"); }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    lcf::async::Scheduler m_home; int & m_value; Events & m_events;
};
TEST(CoroutineLessons, Lesson17EnvironmentAndContinuationRouting)
{
    lcf::async::Context home, other;
    lcf::async::Executor observed;
    Events events;
    int value = 0;
    auto operation = ex::connect(parent(other.getScheduler(), observed, events), Receiver {home.getScheduler(), value, events});
    ex::start(operation); // An environment alone does not enqueue initial entry.
    EXPECT_EQ(observed, home.getExecutor());
    EXPECT_EQ(events.size(), 2u);
    EXPECT_EQ(other.pollOne(), 1u);
    EXPECT_EQ(value, 0);
    EXPECT_EQ(events.size(), 2u);
    EXPECT_EQ(home.getReadyCount(), 1u);
    EXPECT_EQ(home.pollOne(), 1u);
    EXPECT_EQ(value, 42);
    EXPECT_EQ(events, (Events {"parent: entry", "child: before foreign sender",
        "child: resumed at home", "parent: child returned", "root: receiver"}));
    EXPECT_EQ(home.getReadyCount(), 0u);
}
}
