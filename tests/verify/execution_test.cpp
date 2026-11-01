#if LCF_TEST_USE_MODULES
import lcf.async;
#else
#include <async/execution.h>
#endif

#include <gtest/gtest.h>

#include <concepts>
#include <exception>
#include <system_error>
#include <tuple>
#include <utility>

namespace {

namespace ex = lcf::async::exec;
namespace proto = lcf::async::protocols;
using lcf::async::this_thread::sync_wait;

// Public aliases preserve the provider's actual CPO and sender types.
#if !LCF_TEST_USE_MODULES
static_assert(std::same_as<decltype(ex::then),
    decltype(lcf::async::stde::then)>);
static_assert(std::same_as<decltype(ex::connect),
    decltype(lcf::async::stde::connect)>);
static_assert(std::same_as<decltype(ex::set_value),
    decltype(lcf::async::stde::set_value)>);
static_assert(std::same_as<decltype(ex::get_env),
    decltype(lcf::async::stde::get_env)>);
#endif
static_assert(proto::sender<decltype(ex::just(1))>);

struct Receiver
{
    using receiver_concept = proto::receiver_t;

    void set_value(int value) && noexcept { *m_result = value; }
    void set_error(std::exception_ptr) && noexcept { *m_result = -1; }
    void set_stopped() && noexcept { *m_result = -2; }

    int * m_result;
};

static_assert(proto::receiver<Receiver>);
static_assert(proto::sender_to<decltype(ex::just(1)), Receiver>);

TEST(Execution, ConnectDefersWorkUntilStart)
{
    int calls = 0;
    int result = 0;
    auto work = ex::just(21) | ex::then([&](int value) noexcept {
        ++calls;
        return value * 2;
    });
    auto operation = ex::connect(std::move(work), Receiver {&result});
    static_assert(proto::operation_state<decltype(operation)>);

    EXPECT_EQ(calls, 0);
    EXPECT_EQ(result, 0);
    ex::start(operation);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(result, 42);
}

TEST(Execution, LetValueComposesNativeSenders)
{
    auto work = ex::just(20)
        | ex::let_value([](int value) { return ex::just(value + 1); })
        | ex::then([](int value) noexcept { return value * 2; });
    auto result = sync_wait(std::move(work));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(Execution, ErrorSkipsThenAndCanRecover)
{
    const auto missing = std::make_error_code(std::errc::no_such_file_or_directory);
    int skipped_calls = 0;
    int resumed_calls = 0;
    auto work = ex::just_error(missing)
        | ex::then([&]() noexcept { ++skipped_calls; })
        | ex::let_error([missing](std::error_code error) {
            EXPECT_EQ(error, missing);
            return ex::just(21);
        })
        | ex::then([&](int value) noexcept {
            ++resumed_calls;
            return value * 2;
        });
    auto result = sync_wait(std::move(work));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(skipped_calls, 0);
    EXPECT_EQ(resumed_calls, 1);
}

TEST(Execution, StoppedSkipsThenAndCanRecover)
{
    int calls = 0;
    auto work = ex::just_stopped()
        | ex::then([&]() noexcept { ++calls; })
        | ex::upon_stopped([]() noexcept { return 42; });
    auto result = sync_wait(std::move(work));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(calls, 0);
}

TEST(Execution, UnrecoveredStoppedNotifiesReceiver)
{
    int calls = 0;
    int result = 0;
    auto work = ex::just_stopped() | ex::then([&]() noexcept {
        ++calls;
        return 42;
    });
    auto operation = ex::connect(std::move(work), Receiver {&result});
    ex::start(operation);

    EXPECT_EQ(result, -2);
    EXPECT_EQ(calls, 0);
}

TEST(Execution, WhenAllCombinesNativeSenders)
{
    auto work = ex::when_all(ex::just(20), ex::just(22))
        | ex::then([](int first, int second) noexcept { return first + second; });
    auto result = sync_wait(std::move(work));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

} // namespace
