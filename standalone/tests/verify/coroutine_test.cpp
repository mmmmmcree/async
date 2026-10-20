#include <Observation.h>
#include <gtest/gtest.h>
#include <barrier>

namespace {

namespace async = lcf::async;
namespace ex = async::exec;
namespace test = async::testing;
using namespace std::chrono_literals;

/*
 * Objective: synchronous sender completions return owned multiple values and preserve errors.
 * Input: a move-only payload, a second argument and an error-only sender.
 * Procedure: await each sender inside the same coroutine and catch the original system_error.
 * Expected: value 42, one caught permission_denied and no dangling result reference.
 * Failure hints: inspect SenderAwaiter storage, await_resume and exception conversion.
 */
void synchronous_sender_values();
TEST(Coroutine, SynchronousSenderValues) { synchronous_sender_values(); }

/*
 * Objective: asynchronous sender completion restores the awaiting scheduler.
 * Input: a coroutine started on one worker awaiting schedule on another worker.
 * Procedure: record both thread identities before and after the await.
 * Expected: the continuation executes on the original worker.
 * Failure hints: inspect SenderAwaiter::publish and its queued resume record.
 */
void asynchronous_sender_restores_scheduler();
TEST(Coroutine, AsynchronousSenderRestoresScheduler) { asynchronous_sender_restores_scheduler(); }

/*
 * Objective: nested rescheduling restores the parent scheduler on completion.
 * Input: a child that explicitly reschedules to a second worker.
 * Procedure: await that child and observe the parent thread afterward.
 * Expected: the child runs on the second worker and the parent resumes on the first.
 * Failure hints: inspect CoroutineState return executor and final-suspend dispatch.
 */
void nested_reschedule_restores_parent();
TEST(Coroutine, NestedRescheduleRestoresParent) { nested_reschedule_restores_parent(); }

/*
 * Objective: cancellation unwinds suspended nested coroutine frames through stopped.
 * Input: parent/child coroutines holding a marker and awaiting a one-hour timer.
 * Procedure: register the wait, then request stop and pump its owning context.
 * Expected: one stopped, no code after either await and released marker ownership.
 * Failure hints: inspect stop propagation, timer cancellation and recursive frame destruction.
 */
void cancellation_releases_nested_frames();
TEST(Coroutine, CancellationReleasesNestedFrames) { cancellation_releases_nested_frames(); }

/*
 * Objective: a completion racing await_suspend never resumes the coroutine twice.
 * Input: 256 waits whose producer completes on another worker as start returns.
 * Procedure: consume each coroutine and inspect its stage count.
 * Expected: exactly two body stages and one returned value per invocation.
 * Failure hints: inspect the registering/suspended/completed atomic handshake.
 */
void completion_races_suspend();
TEST(Coroutine, CompletionRacesSuspend) { completion_races_suspend(); }

/*
 * Objective: a terminal receiver may destroy the root operation and its frame immediately.
 * Input: synchronous, suspended and stopped coroutine roots with a lifetime marker.
 * Procedure: reset the owning operation inside each terminal receiver.
 * Expected: one terminal notification, no surviving frame marker and no post-completion access.
 * Failure hints: inspect final-suspend dispatch, stopped propagation and StartedOperation forwarding.
 */
void completion_can_destroy_root();
TEST(Coroutine, CompletionCanDestroyRoot) { completion_can_destroy_root(); }

/*
 * Objective: throwing receiver construction cannot leak a coroutine frame during connect.
 * Input: a frame-owned marker and receiver moves failing at each transfer position.
 * Procedure: attempt connect, destroy any resulting state and inspect marker ownership.
 * Expected: every attempted transfer releases the frame exactly once.
 * Failure hints: inspect AwaitableOperation's RAII work parameter before extracting its handle.
 */
void throwing_receiver_releases_frame();
TEST(Coroutine, ThrowingReceiverReleasesFrame) { throwing_receiver_releases_frame(); }

async::Awaitable<int> synchronous_values()
{
    auto [value_up, extra] = co_await ex::just(std::make_unique<int>(20), 22);
    try { co_await ex::just_error(std::make_error_code(std::errc::permission_denied)); }
    catch (const std::system_error & error) {
        if (error.code() == std::errc::permission_denied) { co_return *value_up + extra; }
    }
    co_return -1;
}

void synchronous_sender_values()
{
    auto result = ex::sync_wait(synchronous_values());
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
}

async::Awaitable<int> cross_thread(test::Worker & other)
{
    auto original = std::this_thread::get_id();
    co_await ex::schedule(other.getScheduler());
    co_return original == std::this_thread::get_id() ? 42 : -1;
}

void asynchronous_sender_restores_scheduler()
{
    test::Worker first;
    test::Worker second;
    auto result = ex::sync_wait(async::as_sender(first.getScheduler(), cross_thread(second)));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
}

async::Awaitable<std::thread::id> rescheduled_child(async::Scheduler target)
{
    co_await async::reschedule_on(target);
    co_return std::this_thread::get_id();
}
async::Awaitable<int> rescheduling_parent(test::Worker & other)
{
    auto original = std::this_thread::get_id();
    auto child_thread = co_await rescheduled_child(other.getScheduler());
    co_return child_thread != original and std::this_thread::get_id() == original ? 42 : -1;
}
void nested_reschedule_restores_parent()
{
    test::Worker first;
    test::Worker second;
    auto result = ex::sync_wait(async::as_sender(first.getScheduler(), rescheduling_parent(second)));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
}

async::Awaitable<void> suspended_child(std::shared_ptr<int> marker_sp, unsigned & stages)
{
    ++stages;
    co_await async::delay(1h);
    ++stages;
    (void)marker_sp;
}
async::Awaitable<void> suspended_parent(std::shared_ptr<int> marker_sp, unsigned & stages)
{
    co_await suspended_child(std::move(marker_sp), stages);
    ++stages;
}
void cancellation_releases_nested_frames()
{
    test::Context context;
    std::stop_source stop;
    unsigned stages = 0;
    auto marker_sp = std::make_shared<int>(42);
    std::weak_ptr<int> marker_wp = marker_sp;
    test::Observation observation;
    {
        auto operation = ex::connect(ex::with_stop_token(async::as_sender(context.getScheduler(), suspended_parent(std::move(marker_sp), stages)), stop.get_token()), test::Receiver {&observation});
        ex::start(operation);
        test::pump_until(context, [&] { return stages == 1; });
        stop.request_stop();
        test::pump_until(context, [&] { return observation.m_done.load(); });
        EXPECT_EQ(observation.m_stopped_calls, 1u);
        EXPECT_EQ(observation.m_value_calls + observation.m_error_calls, 0u);
    }
    EXPECT_EQ(stages, 1u);
    EXPECT_TRUE(marker_wp.expired());
}

async::Awaitable<int> racing_wait(async::Scheduler scheduler, unsigned & stages)
{
    ++stages;
    co_await ex::schedule(scheduler);
    ++stages;
    co_return 42;
}
void completion_races_suspend()
{
    test::Worker worker;
    for (unsigned index = 0; index < 256; ++index) {
        SCOPED_TRACE(index);
        unsigned stages = 0;
        auto result = ex::sync_wait(racing_wait(worker.getScheduler(), stages));
        ASSERT_TRUE(result);
        EXPECT_EQ(std::get<0>(*result), 42);
        EXPECT_EQ(stages, 2u);
    }
}

struct DestructionState
{
    void * m_operation_p = nullptr;
    void (*m_destroy)(void *) noexcept = nullptr;
    unsigned m_completions = 0;
    unsigned m_errors = 0;
    std::atomic_bool m_done = false;
};
struct DestroyReceiver
{
    using receiver_concept = ex::receiver_t;
    void finish(bool error) noexcept
    {
        auto * state_p = m_state_p;
        state_p->m_destroy(std::exchange(state_p->m_operation_p, nullptr));
        ++state_p->m_completions;
        state_p->m_errors += error;
        state_p->m_done.store(true);
    }
    void set_value() noexcept { this->finish(false); }
    void set_error(std::exception_ptr) noexcept { this->finish(true); }
    void set_stopped() noexcept { this->finish(false); }
    DestructionState * m_state_p;
};
async::Awaitable<void> terminal_root(std::shared_ptr<int> marker_sp, unsigned mode)
{
    if (mode == 1) { co_await async::yield(); }
    if (mode == 2) { co_await ex::just_stopped(); }
    (void)marker_sp;
}
void completion_can_destroy_root()
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode);
        test::Context context;
        auto marker_sp = std::make_shared<int>(42);
        std::weak_ptr<int> marker_wp = marker_sp;
        DestructionState state;
        auto work = async::as_sender(context.getScheduler(), terminal_root(std::move(marker_sp), mode));
        using Operation = ex::connect_result_t<decltype(work), DestroyReceiver>;
        state.m_operation_p = new Operation(ex::connect(std::move(work), DestroyReceiver {&state}));
        state.m_destroy = [](void * operation_p) noexcept { delete static_cast<Operation *>(operation_p); };
        ex::start(*static_cast<Operation *>(state.m_operation_p));
        test::pump_until(context, [&] { return state.m_done.load(); });
        EXPECT_EQ(state.m_completions, 1u);
        EXPECT_EQ(state.m_errors, 0u);
        EXPECT_EQ(state.m_operation_p, nullptr);
        EXPECT_TRUE(marker_wp.expired());
    }
}

struct ThrowingReceiver
{
    using receiver_concept = ex::receiver_t;
    explicit ThrowingReceiver(unsigned * remaining_p) noexcept : m_remaining_p(remaining_p) {}
    ThrowingReceiver(ThrowingReceiver && other) : m_remaining_p(other.m_remaining_p)
    { if (--*m_remaining_p == 0) { throw std::runtime_error("receiver move"); } }
    void set_value() noexcept {}
    void set_error(std::exception_ptr) noexcept {}
    void set_stopped() noexcept {}
    unsigned * m_remaining_p;
};
void throwing_receiver_releases_frame()
{
    for (unsigned failing_move = 1; failing_move <= 5; ++failing_move) {
        SCOPED_TRACE(failing_move);
        auto marker_sp = std::make_shared<int>(42);
        std::weak_ptr<int> marker_wp = marker_sp;
        {
            auto work = terminal_root(std::move(marker_sp), 0);
            unsigned remaining = failing_move;
            try {
                auto operation = ex::connect(std::move(work), ThrowingReceiver {&remaining});
                ex::start(operation);
            } catch (const std::runtime_error &) {}
        }
        EXPECT_TRUE(marker_wp.expired());
    }
}

}
