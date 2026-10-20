#include <NativeApi.h>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

/*
 * Lesson 18: yield keeps an executor; reschedule_on changes it.
 * Predict: yield queues locally, the child then enters the target executor,
 * and final_suspend queues its parent back to the saved return executor.
 * Read the TEST, parent(), child(). The target is pumped by a joined thread;
 * IDs demonstrate the handoff without a permanently running worker.
 * Vary: inspect the parent before pumping its return queue. A nested child's
 * executor change does not replace the parent's inherited executor.
 * Source map: coroutine/operations.h, scheduling-request await_transform,
 * CoroutineState::finish and its final callback.
 */
namespace {
namespace ex = lcf::async::exec;
lcf::async::Awaitable<int> child(lcf::async::Executor target, std::vector<std::thread::id> & trace)
{
    trace.push_back(std::this_thread::get_id());
    co_await lcf::async::yield();
    trace.push_back(std::this_thread::get_id());
    co_await lcf::async::reschedule_on(target);
    trace.push_back(std::this_thread::get_id());
    co_return 21;
}
lcf::async::Awaitable<int> parent(lcf::async::Executor target, std::vector<std::thread::id> & trace)
{
    const auto value = co_await child(target, trace);
    trace.push_back(std::this_thread::get_id());
    co_return value * 2;
}
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    auto get_env() const noexcept { return ex::prop {ex::get_scheduler, m_home}; }
    void set_value(int value) noexcept { m_value = value; }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    lcf::async::Scheduler m_home; int & m_value;
};
TEST(CoroutineLessons, Lesson18YieldAndReschedule)
{
    lcf::async::Context home, target;
    std::vector<std::thread::id> trace;
    int value = 0;
    const auto caller = std::this_thread::get_id();
    auto operation = ex::connect(parent(target.getExecutor(), trace), Receiver {home.getScheduler(), value});
    ex::start(operation);
    ASSERT_EQ(trace.size(), 1u);
    EXPECT_EQ(home.getReadyCount(), 1u);
    EXPECT_EQ(home.pollOne(), 1u); // schedule receiver completes; awaiter resume is queued.
    EXPECT_EQ(trace.size(), 1u);
    EXPECT_EQ(home.pollOne(), 1u); // yield returns; reschedule queues on target.
    ASSERT_EQ(trace.size(), 2u);
    EXPECT_EQ(target.getReadyCount(), 1u);
    std::size_t target_callbacks = 0;
    std::jthread worker([&] { target_callbacks = target.poll().m_callbacks; });
    worker.join();
    EXPECT_EQ(target_callbacks, 2u); // schedule completion, then queued awaiter resume
    ASSERT_EQ(trace.size(), 3u);
    EXPECT_NE(trace[2], caller);
    EXPECT_EQ(value, 0);
    EXPECT_EQ(home.getReadyCount(), 1u); // child's final callback routes to parent
    EXPECT_EQ(home.pollOne(), 1u);
    EXPECT_EQ(value, 42);
    ASSERT_EQ(trace.size(), 4u);
    EXPECT_EQ(trace[0], caller);
    EXPECT_EQ(trace[1], caller);
    EXPECT_EQ(trace[3], caller);
}
}
