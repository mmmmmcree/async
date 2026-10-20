#include <Observation.h>
#include <gtest/gtest.h>
#include <expected>
#include <memory_resource>
#include <functional>
#include <barrier>

namespace {

namespace async = lcf::async;
namespace ex = async::exec;
namespace test = async::testing;
using namespace std::chrono_literals;

/*
 * Objective: asynchronous unfolding retains its input until inner completion.
 * Input: the graph and controlled resources in let_value_keeps_inputs_alive.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect recovery completion forwarding and operation ownership.
 */
void let_value_keeps_inputs_alive();
TEST(Recovery, LetValueKeepsInputsAlive) { let_value_keeps_inputs_alive(); }

/*
 * Objective: local error recovery can return delayed work.
 * Input: the graph and controlled resources in let_error_recovers_asynchronously.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect recovery completion forwarding and operation ownership.
 */
void let_error_recovers_asynchronously();
TEST(Recovery, LetErrorRecoversAsynchronously) { let_error_recovers_asynchronously(); }

/*
 * Objective: recovery errors propagate rather than recursively invoke their handler.
 * Input: the graph and controlled resources in recovery_exception_is_not_rehandled.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect recovery completion forwarding and operation ownership.
 */
void recovery_exception_is_not_rehandled();
TEST(Recovery, RecoveryExceptionIsNotRehandled) { recovery_exception_is_not_rehandled(); }

/*
 * Objective: error recovery does not consume stopped.
 * Input: the graph and controlled resources in stopped_skips_error_recovery.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect recovery completion forwarding and operation ownership.
 */
void stopped_skips_error_recovery();
TEST(Recovery, StoppedSkipsErrorRecovery) { stopped_skips_error_recovery(); }

/*
 * Objective: retry preserves node locality and its captured input.
 * Input: the graph and controlled resources in retry_rebuilds_only_local_work.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect recovery completion forwarding and operation ownership.
 */
void retry_rebuilds_only_local_work();
TEST(Recovery, RetryRebuildsOnlyLocalWork) { retry_rebuilds_only_local_work(); }

/*
 * Objective: exhausted attempts retain the original error payload.
 * Input: the graph and controlled resources in retry_exhaustion_keeps_error.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect recovery completion forwarding and operation ownership.
 */
void retry_exhaustion_keeps_error();
TEST(Recovery, RetryExhaustionKeepsError) { retry_exhaustion_keeps_error(); }

/*
 * Objective: retry policy can propagate a selected failure immediately.
 * Input: the graph and controlled resources in retry_predicate_can_decline.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect recovery completion forwarding and operation ownership.
 */
void retry_predicate_can_decline();
TEST(Recovery, RetryPredicateCanDecline) { retry_predicate_can_decline(); }

/*
 * Objective: synchronous retry uses bounded stack depth.
 * Input: the graph and controlled resources in many_inline_retries_do_not_recurse.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect recovery completion forwarding and operation ownership.
 */
void many_inline_retries_do_not_recurse();
TEST(Recovery, ManyInlineRetriesDoNotRecurse) { many_inline_retries_do_not_recurse(); }

/*
 * Objective: delayed retries retain stop propagation.
 * Input: the graph and controlled resources in retry_delay_is_cancelable.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect recovery completion forwarding and operation ownership.
 */
void retry_delay_is_cancelable();
TEST(Recovery, RetryDelayIsCancelable) { retry_delay_is_cancelable(); }

/*
 * Objective: retry requires a first attempt.
 * Input: the graph and controlled resources in zero_attempts_is_rejected.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect recovery completion forwarding and operation ownership.
 */
void zero_attempts_is_rejected();
TEST(Recovery, ZeroAttemptsIsRejected) { zero_attempts_is_rejected(); }


void let_value_keeps_inputs_alive()
{
    test::Context context;
    const int * input_p = nullptr;
    auto work = ex::just(std::make_unique<int>(42)) | ex::let_value([&](std::unique_ptr<int> & value_up) {
        input_p = value_up.get();
        return async::schedule_after(context.getScheduler(), 1ms) | ex::then([&value_up] { return *value_up; });
    });
    test::Observation observation;
    test::run(context, std::move(work), observation);
    EXPECT_NE(input_p, nullptr);
    EXPECT_EQ(observation.m_values[0], 42);
    EXPECT_EQ(observation.m_value_calls, 1u);
}

void let_error_recovers_asynchronously()
{
    test::Context context;
    const auto original = std::make_error_code(std::errc::permission_denied);
    auto work = ex::from_result(std::expected<int, std::error_code> {std::unexpected(original)})
        | ex::let_error([&](auto & error) {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(error)>, std::error_code>) { EXPECT_EQ(error, original); }
            return async::schedule_after(context.getScheduler(), 1ms) | ex::then([] { return 42; });
        });
    test::Observation observation;
    test::run(context, std::move(work), observation);
    EXPECT_EQ(observation.m_values[0], 42);
    EXPECT_EQ(observation.m_value_calls, 1u);
}

void recovery_exception_is_not_rehandled()
{
    unsigned calls = 0;
    auto work = ex::just_error(1) | ex::let_error([&](int &) -> decltype(ex::just(42)) { ++calls; throw std::runtime_error("recovery"); });
    EXPECT_THROW(ex::sync_wait(std::move(work)), std::runtime_error);
    EXPECT_EQ(calls, 1u);
}

void stopped_skips_error_recovery()
{
    unsigned calls = 0;
    auto work = test::StoppedSender {} | ex::upon_error([&](auto) { ++calls; return 42; });
    EXPECT_FALSE(ex::sync_wait(std::move(work)));
    EXPECT_EQ(calls, 0u);
}

void retry_rebuilds_only_local_work()
{
    unsigned prefix = 0;
    unsigned suffix = 0;
    unsigned attempts = 0;
    auto work = ex::just(40) | ex::then([&](int value) { ++prefix; return value; })
        | ex::let_value([&](int & value) {
            return ex::retry([&](const ex::RetryContext & context) {
                ++attempts;
                EXPECT_EQ(context.getAttemptCount(), attempts);
                return ex::from_result(attempts < 3 ? std::expected<int, std::error_code> {std::unexpected(std::make_error_code(std::errc::io_error))}
                    : std::expected<int, std::error_code> {value + 2});
            }, 3);
        }) | ex::then([&](int value) { ++suffix; return value; });
    auto result = ex::sync_wait(std::move(work));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(prefix, 1u);
    EXPECT_EQ(suffix, 1u);
    EXPECT_EQ(attempts, 3u);
}

void retry_exhaustion_keeps_error()
{
    unsigned attempts = 0;
    auto error = std::make_error_code(std::errc::permission_denied);
    auto work = ex::retry([&](const ex::RetryContext &) { ++attempts; return ex::just_error(error); }, 3);
    test::Observation observation;
    auto operation = ex::connect(std::move(work), test::Receiver {&observation});
    ex::start(operation);
    EXPECT_EQ(attempts, 3u);
    EXPECT_EQ(observation.m_error, error);
    EXPECT_EQ(observation.m_error_calls, 1u);
}

void retry_predicate_can_decline()
{
    unsigned attempts = 0;
    auto work = ex::retry([&](const ex::RetryContext &) { ++attempts; return ex::from_result(std::expected<int, int> {std::unexpected(7)}); }, 8,
        [](int error, const ex::RetryContext & context) { EXPECT_EQ(error, 7); EXPECT_EQ(context.getAttemptCount(), 1u); return false; });
    EXPECT_THROW(ex::sync_wait(std::move(work)), int);
    EXPECT_EQ(attempts, 1u);
}

void many_inline_retries_do_not_recurse()
{
    unsigned attempts = 0;
    auto work = ex::retry([&](const ex::RetryContext &) { ++attempts; return ex::from_result(std::expected<int, int> {std::unexpected(7)}); }, 10000);
    EXPECT_THROW(ex::sync_wait(std::move(work)), int);
    EXPECT_EQ(attempts, 10000u);
}

void retry_delay_is_cancelable()
{
    test::Context context;
    std::stop_source stop;
    unsigned attempts = 0;
    auto work = ex::retry([&](const ex::RetryContext &) {
        ++attempts;
        return async::schedule_after(context.getScheduler(), 1h) | ex::let_value([] { return ex::just_error(7); });
    }, 3);
    test::Observation observation;
    auto operation = ex::connect(ex::with_stop_token(std::move(work), stop.get_token()), test::Receiver {&observation});
    ex::start(operation);
    context.poll();
    EXPECT_EQ(attempts, 1u);
    stop.request_stop();
    test::pump_until(context, [&] { return observation.m_done.load(); });
    EXPECT_EQ(observation.m_stopped_calls, 1u);
    EXPECT_EQ(attempts, 1u);
}

void zero_attempts_is_rejected()
{
    EXPECT_THROW(ex::retry([](const ex::RetryContext &) { return ex::just(); }, 0), std::invalid_argument);
}

}
