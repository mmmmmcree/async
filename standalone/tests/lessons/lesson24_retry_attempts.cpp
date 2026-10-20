#include <NativeApi.h>
#include <gtest/gtest.h>
#include <expected>
#include <vector>

/*
 * Lesson 24: retry invokes a factory for each new attempt.
 * Predict: failing twice then succeeding constructs three operation states;
 * the prefix and suffix outside retry each run once. The limit counts total
 * attempts, including the first, not extra retries after an initial attempt.
 * Read the TEST and AttemptSender. Distinct states can reuse the same address;
 * use creation/destruction counts, not addresses, to identify their lifetimes.
 * Vary: exhaust the limit and then run many synchronous failures. Retry's
 * iterative drive loop defers reentry while an attempt's start is active.
 * Source map: RetryContext, RetryOperation::complete/drive, OperationStorage.
 */
namespace {
namespace ex = lcf::async::exec;
struct Counts { int m_created = 0, m_destroyed = 0, m_live = 0, m_max_live = 0; };
struct AttemptSender
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(int)>;
    template <class R> struct Operation
    {
        Operation(R receiver, Counts & counts, bool success) : m_receiver(std::move(receiver)), m_counts(counts), m_success(success)
        { ++m_counts.m_created; ++m_counts.m_live; m_counts.m_max_live = std::max(m_counts.m_max_live, m_counts.m_live); }
        Operation(const Operation &) = delete;
        ~Operation() { ++m_counts.m_destroyed; --m_counts.m_live; }
        void start() noexcept
        { if (m_success) { ex::set_value(std::move(m_receiver), 41); } else { ex::set_error(std::move(m_receiver), 7); } }
        R m_receiver; Counts & m_counts; bool m_success;
    };
    template <class R> auto connect(R receiver) const { return Operation<R> {std::move(receiver), m_counts, m_success}; }
    Counts & m_counts; bool m_success;
};
TEST(CoroutineLessons, Lesson24RetryAsNewAttempts)
{
    Counts counts;
    int prefix = 0, suffix = 0;
    std::vector<std::size_t> attempts;
    auto graph = ex::just() | ex::then([&] { ++prefix; }) | ex::let_value([&] {
        return ex::retry([&](const ex::RetryContext & context) {
            attempts.push_back(context.getAttemptCount());
            return AttemptSender {counts, context.getAttemptCount() == 3};
        }, 3);
    }) | ex::then([&](int value) { ++suffix; return value + 1; });
    auto result = ex::sync_wait(std::move(graph));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(attempts, (std::vector<std::size_t> {1, 2, 3}));
    EXPECT_EQ(prefix, 1);
    EXPECT_EQ(suffix, 1);
    EXPECT_EQ(counts.m_created, 3);
    EXPECT_EQ(counts.m_destroyed, 3);
    EXPECT_EQ(counts.m_live, 0);
    EXPECT_EQ(counts.m_max_live, 1);
    Counts limited_counts;
    int limited_attempts = 0;
    auto limited = ex::retry([&](const ex::RetryContext & context) {
        ++limited_attempts;
        return AttemptSender {limited_counts, context.getAttemptCount() == 3};
    }, 2);
    try { ex::sync_wait(std::move(limited)); FAIL() << "two attempts cannot reach attempt three"; }
    catch (int error) { EXPECT_EQ(error, 7); }
    EXPECT_EQ(limited_attempts, 2);
    EXPECT_EQ(limited_counts.m_created, 2);
    EXPECT_EQ(limited_counts.m_destroyed, 2);
    int failures = 0;
    auto exhausted = ex::retry([&](const ex::RetryContext &) {
        ++failures; return ex::from_result(std::expected<int, int> {std::unexpected(7)});
    }, 2000);
    EXPECT_THROW(ex::sync_wait(std::move(exhausted)), int);
    EXPECT_EQ(failures, 2000);
    EXPECT_THROW(ex::retry([](const ex::RetryContext &) { return ex::just(); }, 0), std::invalid_argument);
}
}
