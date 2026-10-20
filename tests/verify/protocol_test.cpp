#include <Observation.h>
#include <gtest/gtest.h>
#include <expected>
#include <memory_resource>
#include <functional>
#include <string>
#include <barrier>

namespace {

namespace async = lcf::async;
namespace ex = async::exec;
namespace test = async::testing;
using namespace std::chrono_literals;

/*
 * Objective: connecting a custom sender never executes user work.
 * Input: the graph and controlled resources in connect_is_lazy.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect protocol completion forwarding and operation ownership.
 */
void connect_is_lazy();
TEST(Protocol, ConnectIsLazy) { connect_is_lazy(); }

/*
 * Objective: then preserves argument ordering and handles void.
 * Input: the graph and controlled resources in multiple_values_and_void.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect protocol completion forwarding and operation ownership.
 */
void multiple_values_and_void();
TEST(Protocol, MultipleValuesAndVoid) { multiple_values_and_void(); }

/*
 * Objective: then transfers move-only callbacks and values.
 * Input: the graph and controlled resources in move_only_function_and_value.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect protocol completion forwarding and operation ownership.
 */
void move_only_function_and_value();
TEST(Protocol, MoveOnlyFunctionAndValue) { move_only_function_and_value(); }

/*
 * Objective: expected conversion is explicit.
 * Input: the graph and controlled resources in then_does_not_unwrap_expected.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect protocol completion forwarding and operation ownership.
 */
void then_does_not_unwrap_expected();
TEST(Protocol, ThenDoesNotUnwrapExpected) { then_does_not_unwrap_expected(); }

/*
 * Objective: a throwing continuation uses the error channel.
 * Input: the graph and controlled resources in throwing_then_preserves_exception.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect protocol completion forwarding and operation ownership.
 */
void throwing_then_preserves_exception();
TEST(Protocol, ThrowingThenPreservesException) { throwing_then_preserves_exception(); }

/*
 * Objective: an error_code payload does not decide its completion channel.
 * Input: the graph and controlled resources in zero_error_keeps_error_channel.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect protocol completion forwarding and operation ownership.
 */
void zero_error_keeps_error_channel();
TEST(Protocol, ZeroErrorKeepsErrorChannel) { zero_error_keeps_error_channel(); }

/*
 * Objective: stopped is distinct from a void value.
 * Input: the graph and controlled resources in stopped_sync_wait_is_empty.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect protocol completion forwarding and operation ownership.
 */
void stopped_sync_wait_is_empty();
TEST(Protocol, StoppedSyncWaitIsEmpty) { stopped_sync_wait_is_empty(); }

/*
 * Objective: on returns to sync_wait scheduling environment.
 * Input: the graph and controlled resources in sync_wait_restores_calling_thread.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect protocol completion forwarding and operation ownership.
 */
void sync_wait_restores_calling_thread();
TEST(Protocol, SyncWaitRestoresCallingThread) { sync_wait_restores_calling_thread(); }

/*
 * Objective: terminal delivery permits immediate destruction of an operation graph.
 * Input: the graph and controlled resources in completion_can_destroy_operation.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect protocol completion forwarding and operation ownership.
 */
void completion_can_destroy_operation();
TEST(Protocol, CompletionCanDestroyOperation) { completion_can_destroy_operation(); }

struct CustomSender
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    template <typename Receiver>
    struct Operation
    {
        using operation_state_concept = ex::operation_state_t;
        void start() noexcept { ++*m_starts_p; ex::set_value(std::move(m_receiver), 42); }
        Receiver m_receiver;
        unsigned * m_starts_p;
    };
    template <typename Receiver>
    auto connect(Receiver receiver) const { return Operation<Receiver> {std::move(receiver), m_starts_p}; }
    unsigned * m_starts_p;
};

struct DestroyReceiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int value) noexcept
    {
        auto * operation_p = std::exchange(*m_slot_p, nullptr);
        auto destroy = m_destroy;
        ++*m_calls_p;
        EXPECT_EQ(value, 42);
        destroy(operation_p);
    }
    void set_error(std::exception_ptr) noexcept { std::terminate(); }
    void set_stopped() noexcept { std::terminate(); }
    void ** m_slot_p;
    void (*m_destroy)(void *);
    unsigned * m_calls_p;
};

void connect_is_lazy()
{
    unsigned starts = 0;
    auto sender = CustomSender {&starts};
    static_assert(ex::sender<CustomSender>);
    static_assert(not ex::sender<int>);
    test::Observation observation;
    auto operation = ex::connect(sender, test::Receiver {&observation});
    static_assert(ex::operation_state<decltype(operation)>);
    EXPECT_EQ(starts, 0u);
    EXPECT_FALSE(observation.m_done.load());
    ex::start(operation);
    EXPECT_EQ(starts, 1u);
    EXPECT_EQ(observation.m_values[0], 42);
    EXPECT_EQ(observation.m_value_calls, 1u);
}

void multiple_values_and_void()
{
    unsigned calls = 0;
    auto result = ex::sync_wait(ex::just(20, 22) | ex::then([&](int first, int second) {
        EXPECT_EQ(first, 20); EXPECT_EQ(second, 22); ++calls;
    }) | ex::then([&] { ++calls; return 42; }));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(calls, 2u);
}

void move_only_function_and_value()
{
    auto result = ex::sync_wait(ex::just(std::make_unique<int>(20))
        | ex::then([value_up = std::make_unique<int>(22)](std::unique_ptr<int> input_up) { return std::make_unique<int>(*input_up + *value_up); }));
    ASSERT_TRUE(result);
    ASSERT_TRUE(std::get<0>(*result));
    EXPECT_EQ(*std::get<0>(*result), 42);
}

void then_does_not_unwrap_expected()
{
    auto result = ex::sync_wait(ex::just() | ex::then([] { return std::expected<int, std::error_code> {42}; }));
    ASSERT_TRUE(result);
    static_assert(std::same_as<decltype(std::get<0>(*result)), std::expected<int, std::error_code> &>);
    ASSERT_TRUE(std::get<0>(*result));
    EXPECT_EQ(*std::get<0>(*result), 42);
}

void throwing_then_preserves_exception()
{
    EXPECT_THROW(ex::sync_wait(ex::just() | ex::then([]() -> int { throw std::runtime_error("body"); })), std::runtime_error);
}

void zero_error_keeps_error_channel()
{
    test::Observation observation;
    auto operation = ex::connect(ex::just_error(std::error_code {}), test::Receiver {&observation});
    ex::start(operation);
    EXPECT_EQ(observation.m_error_calls, 1u);
    EXPECT_EQ(observation.m_value_calls, 0u);
    EXPECT_FALSE(observation.m_error);
}

void stopped_sync_wait_is_empty()
{
    EXPECT_FALSE(ex::sync_wait(test::StoppedSender {}));
    EXPECT_TRUE(ex::sync_wait(ex::just()));
}

void sync_wait_restores_calling_thread()
{
    test::Worker worker;
    const auto caller = std::this_thread::get_id();
    std::thread::id worker_id;
    std::thread::id continuation_id;
    auto result = ex::sync_wait(ex::on(worker.getScheduler(), ex::just(42) | ex::then([&](int value) {
        worker_id = std::this_thread::get_id(); return value;
    })) | ex::then([&](int value) { continuation_id = std::this_thread::get_id(); return value; }));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_NE(worker_id, caller);
    EXPECT_EQ(continuation_id, caller);
}

void completion_can_destroy_operation()
{
    unsigned calls = 0;
    void * slot_p = nullptr;
    auto source = ex::when_all(ex::just(20), ex::just(22)) | ex::then([](int first, int second) { return first + second; });
    using Operation = ex::connect_result_t<decltype(source), DestroyReceiver>;
    DestroyReceiver receiver {&slot_p, [](void * operation_p) { delete static_cast<Operation *>(operation_p); }, &calls};
    auto * operation_p = new Operation(ex::connect(std::move(source), receiver));
    slot_p = operation_p;
    ex::start(*operation_p);
    EXPECT_EQ(slot_p, nullptr);
    EXPECT_EQ(calls, 1u);
}



}
