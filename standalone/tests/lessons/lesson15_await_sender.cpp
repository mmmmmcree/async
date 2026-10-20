#include <NativeApi.h>
#include <gtest/gtest.h>
#include <memory>
#include <string_view>
#include <vector>

/*
 * Lesson 15: co_await adapts a protocol sender into an awaiter.
 * Predict: sender construction, connect, start, adapter receiver completion,
 * then await_resume occur in that order. Inline completion does not enqueue.
 * Read the TEST, consumer(), then ObservedSender.
 * Child path: await_transform(Awaitable) -> TaskAwaiter -> child -> parent.
 * Sender path: await_transform(sender) -> SenderAwaiter -> connect/start ->
 * adapter receiver -> stored result -> await_resume. Values become owned
 * results (one value directly, several as a tuple, none as void).
 * Vary: await a child, an observed sender, void and multiple move-only values.
 * Source map: AwaitablePromise::await_transform and details/sender_awaiter.h.
 */
namespace {
namespace ex = lcf::async::exec;
using Events = std::vector<std::string_view>;
struct ObservedSender
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    template <class R> struct Operation
    {
        void start() noexcept
        {
            m_events.push_back("sender: start");
            m_events.push_back("sender: set_value");
            ex::set_value(std::move(m_receiver), 21);
        }
        R m_receiver; Events & m_events;
    };
    template <class R> auto connect(R receiver) const
    { m_events.push_back("sender: connect"); return Operation<R> {std::move(receiver), m_events}; }
    Events & m_events;
};
lcf::async::Awaitable<int> child(Events & events)
{ events.push_back("child: body"); co_return 1; }
lcf::async::Awaitable<int> consumer(Events & events)
{
    const int first = co_await child(events);
    events.push_back("parent: before sender");
    const int second = co_await ObservedSender {events};
    events.push_back("parent: after sender");
    co_await ex::just();
    auto [owned, extra] = co_await ex::just(std::make_unique<int>(first), 19);
    co_return *owned + second + extra + 1;
}
TEST(CoroutineLessons, Lesson15AwaitingASender)
{
    Events events;
    auto task = consumer(events);
    EXPECT_TRUE(events.empty());
    auto result = ex::sync_wait(std::move(task));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(events, (Events {"child: body", "parent: before sender", "sender: connect",
        "sender: start", "sender: set_value", "parent: after sender"}));
}
}
