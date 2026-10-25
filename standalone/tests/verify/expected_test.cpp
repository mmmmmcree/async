#include <Observation.h>
#include <gtest/gtest.h>

namespace {
namespace async = lcf::async;
namespace ex = async::exec;
namespace ext = ex::ext;
namespace test = async::testing;
using Result = std::expected<int, std::error_code>;

template <typename Function, typename... Arguments>
concept CanFromExpected = requires(Function function, Arguments &&... arguments) {
    ext::from_expected(std::move(function), std::forward<Arguments>(arguments)...);
};

TEST(ExpectedExtensions, ConstrainsCallableAndCompletionSignatures)
{
    auto valid = [](int value) noexcept -> Result { return value; };
    auto throwing = [](int value) -> Result { return value; };
    auto plain = [](int value) noexcept { return value; };
    auto wrong_error = [](int value) noexcept -> std::expected<int, int> { return value; };
    auto reference = [](Result & result) noexcept -> Result & { return result; };
    using Source = decltype(ex::just(42));
    static_assert(CanFromExpected<decltype(valid), int>);
    static_assert(not CanFromExpected<decltype(valid)>);
    static_assert(not CanFromExpected<decltype(throwing), int>);
    static_assert(not CanFromExpected<decltype(plain), int>);
    static_assert(not CanFromExpected<decltype(wrong_error), int>);
    static_assert(not CanFromExpected<decltype(reference), std::reference_wrapper<Result>>);
    static_assert(std::invocable<decltype(ext::then_expected), Source, decltype(valid)>);
    static_assert(not std::invocable<decltype(ext::then_expected), Source, decltype(throwing)>);
    static_assert(not std::invocable<decltype(ext::then_expected), Source, decltype(plain)>);
    static_assert(not std::invocable<decltype(ext::then_expected), Source, decltype(wrong_error)>);
    static_assert(not std::invocable<decltype(ext::then_expected), Source, decltype(reference)>);
    using Sender = decltype(ext::from_expected(valid, 42));
    static_assert(std::same_as<ex::completion_signatures_of_t<Sender>,
        ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::error_code)>>);
    using VoidSender = decltype(ext::from_expected([]() noexcept -> std::expected<void, std::error_code> { return {}; }));
    static_assert(std::same_as<ex::completion_signatures_of_t<VoidSender>,
        ex::completion_signatures<ex::set_value_t(), ex::set_error_t(std::error_code)>>);
}

TEST(ExpectedExtensions, DefersInvocationAndOwnsArguments)
{
    unsigned calls = 0;
    int input = 40;
    auto work = ext::from_expected([&](int value) noexcept -> Result { ++calls; return value + 2; }, input);
    input = 100;
    EXPECT_EQ(calls, 0u);
    test::Observation observation;
    auto operation = ex::connect(std::move(work), test::Receiver {&observation});
    EXPECT_EQ(calls, 0u);
    ex::start(operation);
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(observation.m_values[0], 42);
    EXPECT_EQ(observation.m_value_calls, 1u);
    EXPECT_EQ(observation.m_error_calls, 0u);

    auto referenced = ext::from_expected([](int & value) noexcept -> Result { return value; }, std::ref(input));
    input = 7;
    auto result = ex::sync_wait(std::move(referenced));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 7);
}

TEST(ExpectedExtensions, ChainsExpectedAndOrdinaryContinuations)
{
    auto add = [](int left, int right) noexcept -> Result { return left + right; };
    const auto step = ext::then_expected(add);
    auto work = ex::just(19, 2) | step
        | ext::then_expected([](int value) noexcept -> Result { return value * 2; })
        | ex::then([](int value) noexcept { return value + 1; });
    auto result = ex::sync_wait(std::move(work));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 43);
}

TEST(ExpectedExtensions, FailureSkipsExpectedAndOrdinaryContinuations)
{
    const auto original = std::make_error_code(std::errc::permission_denied);
    unsigned calls = 0;
    auto work = ext::from_expected([=]() noexcept -> Result { return std::unexpected(original); })
        | ext::then_expected([&](int value) noexcept -> Result { ++calls; return value; })
        | ex::then([&](int value) noexcept { ++calls; return value; });
    test::Observation observation;
    auto operation = ex::connect(std::move(work), test::Receiver {&observation});
    ex::start(operation);
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(observation.m_error, original);
    EXPECT_EQ(observation.m_error_calls, 1u);
    EXPECT_EQ(observation.m_value_calls, 0u);
}

TEST(ExpectedExtensions, ContinuationFailureShortCircuitsLaterSteps)
{
    const auto original = std::make_error_code(std::errc::invalid_argument);
    unsigned calls = 0;
    auto work = ext::from_expected([]() noexcept -> Result { return 42; })
        | ext::then_expected([=](int) noexcept -> Result { return std::unexpected(original); })
        | ext::then_expected([&](int value) noexcept -> Result { ++calls; return value; });
    test::Observation observation;
    auto operation = ex::connect(std::move(work), test::Receiver {&observation});
    ex::start(operation);
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(observation.m_error, original);
    EXPECT_EQ(observation.m_error_calls, 1u);
}

TEST(ExpectedExtensions, PreservesErrorCategoryAndUnexpectedZero)
{
    for (const auto error : {std::error_code {13, std::system_category()}, std::error_code {13, std::generic_category()}, std::error_code {}}) {
        auto work = ext::from_expected([=]() noexcept -> Result { return std::unexpected(error); });
        test::Observation observation;
        auto operation = ex::connect(std::move(work), test::Receiver {&observation});
        ex::start(operation);
        EXPECT_EQ(observation.m_error, error);
        EXPECT_EQ(observation.m_error_calls, 1u);
        EXPECT_EQ(observation.m_value_calls, 0u);
    }
}

TEST(ExpectedExtensions, RecoversWithDefaultValue)
{
    const auto original = std::make_error_code(std::errc::io_error);
    std::error_code observed;
    auto work = ext::from_expected([=]() noexcept -> Result { return std::unexpected(original); })
        | ex::upon_error([&](std::error_code error) noexcept { observed = error; return 42; });
    auto result = ex::sync_wait(std::move(work));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(observed, original);
}

TEST(ExpectedExtensions, SelectivelyRecoversOrPropagatesError)
{
    const auto recoverable = std::make_error_code(std::errc::no_such_file_or_directory);
    const auto fatal = std::make_error_code(std::errc::permission_denied);
    for (const auto error : {recoverable, fatal}) {
        auto work = ext::from_expected([=]() noexcept -> Result { return std::unexpected(error); })
            | ex::let_error([=](std::error_code & failure) {
                return ext::from_expected([=]() noexcept -> Result {
                    if (failure == recoverable) { return 42; }
                    return std::unexpected(failure);
                });
            });
        test::Observation observation;
        auto operation = ex::connect(std::move(work), test::Receiver {&observation});
        ex::start(operation);
        if (error == recoverable) {
            EXPECT_EQ(observation.m_values[0], 42);
            EXPECT_EQ(observation.m_value_calls, 1u);
        } else {
            EXPECT_EQ(observation.m_error, fatal);
            EXPECT_EQ(observation.m_error_calls, 1u);
        }
    }
}

TEST(ExpectedExtensions, HandlesVoidSuccessAndFailure)
{
    using VoidResult = std::expected<void, std::error_code>;
    auto work = ext::from_expected([]() noexcept -> VoidResult { return {}; })
        | ext::then_expected([]() noexcept -> Result { return 42; })
        | ext::then_expected([](int) noexcept -> VoidResult { return {}; });
    auto result = ex::sync_wait(std::move(work));
    static_assert(std::same_as<decltype(result), std::optional<std::tuple<>>>);
    EXPECT_TRUE(result);

    const auto original = std::make_error_code(std::errc::io_error);
    auto failure = ext::from_expected([=]() noexcept -> VoidResult { return std::unexpected(original); });
    test::Observation observation;
    auto operation = ex::connect(std::move(failure), test::Receiver {&observation});
    ex::start(operation);
    EXPECT_EQ(observation.m_error, original);
    EXPECT_EQ(observation.m_error_calls, 1u);
    EXPECT_EQ(observation.m_value_calls, 0u);
}

struct MoveOnlyFunction
{
    std::expected<std::unique_ptr<int>, std::error_code> operator()(std::unique_ptr<int> value) && noexcept
    { *value += *m_increment; return value; }
    std::unique_ptr<int> m_increment;
};

TEST(ExpectedExtensions, SupportsMoveOnlyCallableArgumentsAndResult)
{
    auto work = ext::from_expected(MoveOnlyFunction {std::make_unique<int>(1)}, std::make_unique<int>(40))
        | ext::then_expected(MoveOnlyFunction {std::make_unique<int>(1)});
    auto result = ex::sync_wait(std::move(work));
    ASSERT_TRUE(result);
    ASSERT_TRUE(std::get<0>(*result));
    EXPECT_EQ(*std::get<0>(*result), 42);
}

TEST(ExpectedExtensions, ForwardsUpstreamErrorAndStopped)
{
    unsigned calls = 0;
    auto callback = [&]() noexcept -> Result { ++calls; return 42; };
    const auto original = std::make_exception_ptr(std::runtime_error("upstream"));
    auto failure = ex::just_error(original) | ext::then_expected(callback);
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(failure)>,
        ex::completion_signatures<ex::set_error_t(std::exception_ptr)>>);
    test::Observation observation;
    auto operation = ex::connect(std::move(failure), test::Receiver {&observation});
    ex::start(operation);
    EXPECT_EQ(observation.m_exception, original);
    EXPECT_EQ(observation.m_error_calls, 1u);
    auto stopped = test::StoppedSender {} | ext::then_expected(callback);
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(stopped)>,
        ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::error_code), ex::set_stopped_t()>>);
    EXPECT_FALSE(ex::sync_wait(std::move(stopped)));
    EXPECT_EQ(calls, 0u);
}

TEST(ExpectedExtensions, RunsAfterScheduledCompletionAndForwardsEnvironment)
{
    test::Context context;
    unsigned calls = 0;
    auto work = ex::schedule(context.getScheduler())
        | ext::then_expected([&]() noexcept -> Result { ++calls; return 42; });
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(work)), context.getScheduler());
    test::Observation observation;
    auto operation = ex::connect(std::move(work), test::Receiver {&observation});
    ex::start(operation);
    EXPECT_EQ(calls, 0u);
    test::pump_until(context, [&] { return observation.m_done.load(); });
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(observation.m_values[0], 42);

    std::stop_source stop;
    stop.request_stop();
    auto canceled = ex::with_stop_token(ex::schedule(context.getScheduler())
        | ext::then_expected([&]() noexcept -> Result { ++calls; return 42; }), stop.get_token());
    test::Observation cancellation;
    test::run(context, std::move(canceled), cancellation);
    EXPECT_EQ(cancellation.m_stopped_calls, 1u);
    EXPECT_EQ(calls, 1u);
}

}
