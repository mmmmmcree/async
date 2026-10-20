#include <NativeApi.h>
#include <gtest/gtest.h>
#include <expected>

/*
 * Lesson 23: recovery changes the local error edge into a value edge.
 * Predict: the prefix runs once, a failed node skips its then, local recovery
 * supplies 41, and the downstream map resumes with 42. Upstream is not rerun.
 * Read the TEST. from_result converts expected into protocol channels;
 * upon_error returns a value, while let_error creates a new operation.
 * Vary: replace inline recovery with a scheduled recovery sender.
 * Source map: execution/continuations.h and execution/error_handling.h.
 */
namespace {
namespace ex = lcf::async::exec;
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int value) noexcept { m_value = value; ++m_calls; }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    void set_error(int) noexcept { ADD_FAILURE() << "unrecovered error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    int & m_value; int & m_calls;
};
TEST(CoroutineLessons, Lesson23LocalErrorRecovery)
{
    int prefix = 0, skipped = 0, recovered = 0, suffix = 0;
    auto graph = ex::just(21) | ex::then([&](int value) { ++prefix; return value; })
        | ex::let_value([](int &) { return ex::from_result(std::expected<int, int> {std::unexpected(7)}); })
        | ex::then([&](int value) { ++skipped; return value; })
        | ex::upon_error([&](auto error) {
            if constexpr (std::same_as<decltype(error), int>) { EXPECT_EQ(error, 7); }
            ++recovered; return 41;
        })
        | ex::then([&](int value) { ++suffix; return value + 1; });
    auto result = ex::sync_wait(std::move(graph));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(prefix, 1);
    EXPECT_EQ(skipped, 0);
    EXPECT_EQ(recovered, 1);
    EXPECT_EQ(suffix, 1);

    lcf::async::Context context;
    int recovery_bodies = 0;
    auto asynchronous = ex::from_result(std::expected<int, int> {std::unexpected(7)})
        | ex::let_error([&](auto &) {
            return ex::schedule(context.getScheduler()) | ex::then([&] { ++recovery_bodies; return 42; });
        });
    int value = 0, completions = 0;
    auto operation = ex::connect(std::move(asynchronous), Receiver {value, completions});
    ex::start(operation);
    EXPECT_EQ(recovery_bodies, 0);
    EXPECT_EQ(completions, 0);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(completions, 1);
    EXPECT_EQ(value, 42);
    EXPECT_EQ(recovery_bodies, 1);
}
}
