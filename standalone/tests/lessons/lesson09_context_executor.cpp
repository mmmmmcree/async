#include <NativeApi.h>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

/*
 * Lesson 09: an executor submits; its context admits and invokes callbacks.
 * Predict: two posts change readiness without executing their bodies.
 * Read the TEST and compare the executor handle with its context owner.
 * pollOne removes one intrusive Callback and invokes it on the polling thread.
 * Vary: copy the executor; both handles submit to the same queue.
 * Source map: Context.h, Executor.h, details/CallbackExecutor.h, queue.h.
 */
namespace {
TEST(CoroutineLessons, Lesson09ContextAndExecutor)
{
    lcf::async::Context context;
    auto executor = context.getExecutor();
    auto another_handle = executor;
    std::vector<int> events;
    const auto caller = std::this_thread::get_id();
    executor.post([&] { EXPECT_EQ(std::this_thread::get_id(), caller); events.push_back(1); });
    another_handle.post([&] { events.push_back(2); });
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(context.getReadyCount(), 2u);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(events, (std::vector<int> {1}));
    EXPECT_EQ(context.getReadyCount(), 1u);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(events, (std::vector<int> {1, 2}));
    EXPECT_EQ(context.pollOne(), 0u);
}
}
