#include <NativeApi.h>
#include <gtest/gtest.h>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

/*
 * Lesson 11: work accounting and a stop flag answer different questions.
 * Predict: runUntilIdle waits while an executor holds outstanding work, even
 * with an empty queue. A posted callback runs on the thread pumping run.
 * Read the TEST; the condition variable holds the first callback at a visible
 * gate. Every wait is bounded; no sleep establishes the execution order.
 * workFinished allows idle exit. stop instead prevents admission of queued
 * callbacks; restart makes those callbacks eligible again.
 * Source map: Context::run/runUntilIdle, Queue::pop and workStarted/Finished.
 */
namespace {
TEST(CoroutineLessons, Lesson11WorkerThreadsAndWorkLifetime)
{
    using namespace std::chrono_literals;
    lcf::async::Context context;
    const auto executor = context.getExecutor();
    executor.workStarted();
    std::promise<void> entered, finished;
    auto entered_future = entered.get_future();
    auto finished_future = finished.get_future();
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false, gate_opened = false;
    std::thread::id execution_thread;
    std::jthread worker([&] { context.runUntilIdle(); finished.set_value(); });
    // A scope guard stops the queue before worker's destructor joins it,
    // including on assertion failure. The callback has its own bounded wait.
    struct StopOnExit { lcf::async::Context & m_context; ~StopOnExit() { m_context.stop(); } } guard {context};
    EXPECT_EQ(finished_future.wait_for(0ms), std::future_status::timeout);
    executor.post([&] {
        execution_thread = std::this_thread::get_id();
        entered.set_value();
        std::unique_lock lock {mutex};
        gate_opened = changed.wait_for(lock, 5s, [&] { return released; });
    });
    const auto entered_status = entered_future.wait_for(5s);
    EXPECT_EQ(entered_status, std::future_status::ready);
    executor.post([&] { executor.workFinished(); });
    EXPECT_EQ(context.getReadyCount(), 1u);
    EXPECT_EQ(finished_future.wait_for(0ms), std::future_status::timeout);
    { std::lock_guard lock {mutex}; released = true; }
    changed.notify_one();
    const auto finished_status = finished_future.wait_for(5s);
    context.stop();
    worker.join();
    ASSERT_EQ(entered_status, std::future_status::ready);
    ASSERT_EQ(finished_status, std::future_status::ready);
    EXPECT_TRUE(gate_opened);
    EXPECT_NE(execution_thread, std::this_thread::get_id());
    EXPECT_EQ(context.getReadyCount(), 0u);

    int calls = 0;
    executor.post([&] { ++calls; });
    EXPECT_EQ(context.pollOne(), 0u);
    EXPECT_EQ(calls, 0);
    context.restart();
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(calls, 1);
}
}
