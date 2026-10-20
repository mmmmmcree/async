#include <NativeApi.h>
#include <Runtime.h>
#include <gtest/gtest.h>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

namespace async = lcf::async;
namespace ex = async::exec;
namespace stde = async::exec;

/*
 * Objective: the execution entry preserves value ownership across composition.
 * Input: just/then/when_all and a move-only value.
 * Procedure: evaluate a synchronous pipeline and inspect its result.
 * Expected: ownership of the original move-only value.
 * Failure hints: inspect value forwarding in the aggregate entry.
 */
void algorithms_preserve_values();
TEST(Execution, AlgorithmsPreserveValues) { algorithms_preserve_values(); }

/*
 * Objective: native branches execute on workers and publish on the control context.
 * Input: two worker contexts, two branches, and a control scheduler.
 * Procedure: start the graph, pump control one callback at a time, and observe threads.
 * Expected: no eager work, result 42, both workers entered and control continuation restored.
 * Failure hints: inspect schedule/connect/start and continues_on, then queue publication.
 */
void native_branches_restore_control();
TEST(Execution, NativeBranchesRestoreControl) { native_branches_restore_control(); }

/*
 * Objective: a queued native pipeline can execute several inline continuations.
 * Input: starts_on followed by three then operations.
 * Procedure: start and admit one queued operation through poll.
 * Expected: one callback executes all three then bodies and completes once.
 * Failure hints: queue budgets count dispatches; native then does not introduce a queue boundary.
 */
void inline_continuations_share_one_dispatch();
TEST(Execution, InlineContinuationsShareOneDispatch) { inline_continuations_share_one_dispatch(); }

/*
 * Objective: explicit scheduler transfers bound admission between user steps.
 * Input: three then bodies separated by continues_on on one context.
 * Procedure: admit one queued operation at a time.
 * Expected: exactly one additional body per poll, with completion on the third.
 * Failure hints: inspect schedule's queue boundary and budget checks before dequeue.
 */
void explicit_boundaries_limit_admission();
TEST(Execution, ExplicitBoundariesLimitAdmission) { explicit_boundaries_limit_admission(); }

/*
 * Objective: native errors preserve the exception and skip value continuations.
 * Input: just_error carrying an existing exception_ptr.
 * Procedure: connect a receiver and start directly.
 * Expected: one error, the original exception, and no value completion.
 * Failure hints: inspect native completion forwarding rather than task Result conversion.
 */
void native_error_keeps_its_channel();
TEST(Execution, NativeErrorKeepsItsChannel) { native_error_keeps_its_channel(); }

/*
 * Objective: stopped completion remains separate from an error or value.
 * Input: just_stopped with a successor then.
 * Procedure: connect and start directly.
 * Expected: one stopped completion and no successor invocation.
 * Failure hints: inspect receiver set_stopped and avoid task-specific error translation.
 */
void native_stopped_keeps_its_channel();
TEST(Execution, NativeStoppedKeepsItsChannel) { native_stopped_keeps_its_channel(); }

/*
 * Objective: several then branches share one execution and one move-only result.
 * Input: a queued producer owning a unique_ptr and two waiting branches.
 * Procedure: start when_all, pump the producer, then attach a late third branch.
 * Expected: one producer call, identical observed addresses, and cached late delivery.
 * Failure hints: inspect split's shared completion state and distinguish sharing from when_all.
 */
void split_shares_one_execution_and_cached_result();
TEST(Execution, SplitSharesOneExecutionAndCachedResult) { split_shares_one_execution_and_cached_result(); }

struct Observation
{
    std::atomic_bool m_done = false;
    int m_value = 0;
    unsigned m_values = 0;
    unsigned m_errors = 0;
    unsigned m_stopped = 0;
    std::exception_ptr m_error;
};

struct Receiver
{
    using receiver_concept = stde::receiver_t;
    void set_value(int value) noexcept
    {
        m_observation_p->m_value = value;
        ++m_observation_p->m_values;
        m_observation_p->m_done.store(true, std::memory_order_release);
    }
    void set_error(std::exception_ptr error) noexcept
    {
        m_observation_p->m_error = std::move(error);
        ++m_observation_p->m_errors;
        m_observation_p->m_done.store(true, std::memory_order_release);
    }
    void set_stopped() noexcept
    {
        ++m_observation_p->m_stopped;
        m_observation_p->m_done.store(true, std::memory_order_release);
    }
    Observation * m_observation_p;
};

class Worker
{
public:
    ~Worker() noexcept { m_context.stop(); }
    Worker() : m_thread([this] { m_context.runForever(); }) {}
    auto getScheduler() noexcept { return m_context.getScheduler(); }
private:
    async::testing::Context m_context;
    std::jthread m_thread;
};

void algorithms_preserve_values()
{
    auto input_up = std::make_unique<int>(42);
    const auto * original_p = input_up.get();
    auto result = ex::sync_wait(ex::just(std::move(input_up)) | ex::then([](auto value_up) { return value_up; }));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result).get(), original_p);
    EXPECT_EQ(*std::get<0>(*result), 42);
}

void native_branches_restore_control()
{
    async::testing::Context control;
    Worker first;
    Worker second;
    const auto control_thread = std::this_thread::get_id();
    std::thread::id first_thread;
    std::thread::id second_thread;
    std::thread::id completion_thread;
    auto work = ex::continues_on(ex::when_all(
        ex::on(first.getScheduler(), ex::just(20) | ex::then([&](int value) {
            first_thread = std::this_thread::get_id();
            return value + 1;
        })),
        ex::on(second.getScheduler(), ex::just(21) | ex::then([&](int value) {
            second_thread = std::this_thread::get_id();
            return value;
        }))), control.getScheduler()) | ex::then([&](int first_value, int second_value) {
            completion_thread = std::this_thread::get_id();
            return first_value + second_value;
        });
    Observation observation;
    auto operation = stde::connect(ex::starts_on(control.getScheduler(), std::move(work)), Receiver {&observation});
    EXPECT_FALSE(observation.m_done.load());
    EXPECT_EQ(first_thread, std::thread::id {});
    stde::start(operation);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {5};
    while (not observation.m_done.load(std::memory_order_acquire) and std::chrono::steady_clock::now() < deadline) {
        control.poll_one();
        std::this_thread::yield();
    }
    ASSERT_TRUE(observation.m_done.load(std::memory_order_acquire));
    EXPECT_EQ(observation.m_values, 1u);
    EXPECT_EQ(observation.m_value, 42);
    EXPECT_NE(first_thread, control_thread);
    EXPECT_NE(second_thread, control_thread);
    EXPECT_NE(first_thread, second_thread);
    EXPECT_EQ(completion_thread, control_thread);
}

void inline_continuations_share_one_dispatch()
{
    async::testing::Context context;
    unsigned calls = 0;
    const auto increment = [&](int value) { ++calls; return value + 1; };
    Observation observation;
    auto work = ex::starts_on(context.getScheduler(), ex::just(0) | ex::then(increment) | ex::then(increment) | ex::then(increment));
    auto operation = stde::connect(std::move(work), Receiver {&observation});
    stde::start(operation);
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(context.poll_one(), 1u);
    EXPECT_EQ(calls, 3u);
    EXPECT_TRUE(observation.m_done.load());
    EXPECT_EQ(observation.m_value, 3);
}

void explicit_boundaries_limit_admission()
{
    async::testing::Context context;
    const auto scheduler = context.getScheduler();
    unsigned calls = 0;
    const auto increment = [&](int value) { ++calls; return value + 1; };
    auto work = ex::just(0) | ex::continues_on(scheduler) | ex::then(increment)
        | ex::continues_on(scheduler) | ex::then(increment)
        | ex::continues_on(scheduler) | ex::then(increment);
    Observation observation;
    auto operation = stde::connect(std::move(work), Receiver {&observation});
    stde::start(operation);
    for (unsigned index = 1; index <= 3; ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(context.poll_one(), 1u);
        EXPECT_EQ(calls, index);
        EXPECT_EQ(observation.m_done.load(), index == 3);
    }
    EXPECT_EQ(observation.m_value, 3);
}

void native_error_keeps_its_channel()
{
    Observation observation;
    const auto error = std::make_exception_ptr(std::runtime_error {"native failure"});
    auto operation = stde::connect(ex::just_error(error), Receiver {&observation});
    stde::start(operation);
    ASSERT_TRUE(observation.m_done.load());
    EXPECT_EQ(observation.m_errors, 1u);
    EXPECT_EQ(observation.m_values, 0u);
    EXPECT_EQ(observation.m_error, error);
}

void native_stopped_keeps_its_channel()
{
    Observation observation;
    bool called = false;
    auto operation = stde::connect(ex::just_stopped() | ex::then([&] { called = true; return 42; }), Receiver {&observation});
    stde::start(operation);
    ASSERT_TRUE(observation.m_done.load());
    EXPECT_EQ(observation.m_stopped, 1u);
    EXPECT_EQ(observation.m_errors, 0u);
    EXPECT_EQ(observation.m_values, 0u);
    EXPECT_FALSE(called);
}

void split_shares_one_execution_and_cached_result()
{
    async::testing::Context context;
    unsigned producer_calls = 0;
    const int * first_p = nullptr;
    const int * second_p = nullptr;
    auto work = ex::starts_on(context.getScheduler(), ex::just(std::make_unique<int>(21))
        | ex::then([&](auto value_up) { ++producer_calls; return value_up; }));
    auto shared = ex::split(std::move(work));
    auto first = shared | ex::then([&](const auto & value_up) { first_p = value_up.get(); return *value_up; });
    auto second = shared | ex::then([&](const auto & value_up) { second_p = value_up.get(); return *value_up; });
    Observation observation;
    auto operation = stde::connect(ex::when_all(std::move(first), std::move(second))
        | ex::then([](int first, int second) { return first + second; }), Receiver {&observation});
    EXPECT_EQ(producer_calls, 0u);
    stde::start(operation);
    EXPECT_EQ(producer_calls, 0u);
    EXPECT_FALSE(observation.m_done.load());
    context.poll();
    ASSERT_TRUE(observation.m_done.load());
    EXPECT_EQ(observation.m_value, 42);
    EXPECT_EQ(producer_calls, 1u);
    EXPECT_EQ(first_p, second_p);
    auto late = ex::sync_wait(shared | ex::then([&](const auto & value_up) {
        EXPECT_EQ(value_up.get(), first_p);
        return *value_up;
    }));
    ASSERT_TRUE(late);
    EXPECT_EQ(std::get<0>(*late), 21);
    EXPECT_EQ(producer_calls, 1u);
}

}
