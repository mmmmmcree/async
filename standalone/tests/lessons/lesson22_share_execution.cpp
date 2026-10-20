#include <NativeApi.h>
#include <gtest/gtest.h>
#include <memory>

/*
 * Lesson 22: two subscriptions can share one execution and one stored result.
 * Predict: split is lazy; both branches see the same move-only object. A late
 * subscriber reads cached completion without invoking the producer again.
 * Read the TEST. Branches receive const references into shared storage; map
 * to an owned integer before leaving its lifetime. Do not move out of a cache.
 * Vary: early subscribers versus a subscriber attached after completion.
 * Source map: execution/split.h and CompletionStorage::replay.
 */
namespace {
namespace ex = lcf::async::exec;
struct Payload { int m_value; std::shared_ptr<int> m_marker; };
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int first, int second) noexcept { m_sum = first + second; ++m_calls; }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    int & m_sum; int & m_calls;
};
TEST(CoroutineLessons, Lesson22ShareOneExecution)
{
    lcf::async::Context context;
    int producer_calls = 0, sum = 0, completions = 0;
    auto marker = std::make_shared<int>(21);
    std::weak_ptr<int> observer = marker;
    {
        auto shared = ex::split(ex::starts_on(context.getScheduler(),
            ex::just(std::make_unique<Payload>(Payload {21, std::move(marker)}))
            | ex::then([&](auto owned) { ++producer_calls; owned->m_value *= 2; return owned; })));
        const Payload * first_address = nullptr, * second_address = nullptr;
        auto first = shared | ex::then([&](const auto & owned) { first_address = owned.get(); return owned->m_value; });
        auto second = shared | ex::then([&](const auto & owned) { second_address = owned.get(); return owned->m_value; });
        auto operation = ex::connect(ex::when_all(std::move(first), std::move(second)), Receiver {sum, completions});
        EXPECT_EQ(producer_calls, 0);
        ex::start(operation);
        EXPECT_EQ(completions, 0);
        EXPECT_EQ(context.pollOne(), 1u);
        EXPECT_EQ(producer_calls, 1);
        EXPECT_EQ(completions, 1);
        EXPECT_EQ(sum, 84);
        EXPECT_NE(first_address, nullptr);
        EXPECT_EQ(first_address, second_address);
        EXPECT_FALSE(observer.expired()); // The cached result itself owns this marker.
        auto late = ex::sync_wait(shared | ex::then([&](const auto & owned) {
            EXPECT_EQ(owned.get(), first_address); return owned->m_value + 1;
        }));
        ASSERT_TRUE(late);
        EXPECT_EQ(std::get<0>(*late), 43);
        EXPECT_EQ(producer_calls, 1);
    }
    EXPECT_TRUE(observer.expired());
}
}
