#include <NativeApi.h>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

/*
 * Lesson 12: a cooperative budget counts callbacks, not user statements.
 * Predict: a count budget leaves ready callbacks queued, but one admitted
 * callback can run an entire inline pipeline. Time is checked between calls;
 * a slow callback finishes even after its time allowance expires.
 * Vary: zero duration, two callbacks, then several ready then continuations.
 * The small busy interval illustrates admission only; it is not a benchmark.
 * Source map: PollBudget/PollReport and Context::poll/pollOne.
 */
namespace {
namespace ex = lcf::async::exec;
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value() noexcept { ++m_completions; }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    int & m_completions;
};
TEST(CoroutineLessons, Lesson12CooperativePollingBudgets)
{
    using namespace std::chrono_literals;
    lcf::async::Context context;
    std::vector<int> order;
    for (int index = 0; index < 5; ++index) { context.getExecutor().post([&, index] { order.push_back(index); }); }
    EXPECT_EQ(context.poll({.m_max_duration = 0ms}).m_callbacks, 0u);
    const auto report = context.poll({.m_max_callbacks = 2});
    EXPECT_EQ(report.m_callbacks, 2u);
    EXPECT_TRUE(report.m_count_limit_reached);
    EXPECT_EQ(context.getReadyCount(), 3u);
    EXPECT_EQ(context.poll().m_callbacks, 3u);
    EXPECT_EQ(order, (std::vector<int> {0, 1, 2, 3, 4}));

    int steps = 0, completions = 0;
    auto graph = ex::just() | ex::then([&] { ++steps; }) | ex::then([&] { ++steps; });
    auto operation = ex::connect(std::move(graph), Receiver {completions});
    context.getExecutor().post([&] {
        ex::start(operation);
    });
    EXPECT_EQ(context.poll({.m_max_callbacks = 1}).m_callbacks, 1u);
    EXPECT_EQ(steps, 2);
    EXPECT_EQ(completions, 1);

    context.getExecutor().post([&] {
        const auto finish = std::chrono::steady_clock::now() + 3ms;
        while (std::chrono::steady_clock::now() < finish) { std::this_thread::yield(); }
        ++steps;
    });
    context.getExecutor().post([&] { ++steps; });
    lcf::async::PollReport timed;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    do { timed = context.poll({.m_max_duration = 1ms}); }
    while (timed.m_callbacks == 0 && std::chrono::steady_clock::now() < deadline);
    ASSERT_EQ(timed.m_callbacks, 1u);
    EXPECT_TRUE(timed.m_time_limit_reached);
    EXPECT_GE(timed.m_elapsed, 3ms);
    EXPECT_EQ(steps, 3);
    EXPECT_EQ(context.getReadyCount(), 1u);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(steps, 4);
}
}
