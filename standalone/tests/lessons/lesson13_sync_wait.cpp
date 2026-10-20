#include <NativeApi.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

/*
 * Lesson 13: sync_wait supplies and pumps a private run loop on its caller.
 * Predict: inline work needs no queue; awaiting yield uses the supplied loop
 * and still resumes on this thread. No implicit background worker appears.
 * Read the TEST, queued_answer(), WaitReceiver::get_env, then sync_wait.
 * Value becomes an optional tuple, error rethrows, stopped yields nullopt.
 * Vary: success, exception, stopped; revisit Lesson 03's production preview.
 * Source map: execution/sync_wait.h, scheduling/RunLoop.h, CompletionEvent.h.
 */
namespace {
namespace ex = lcf::async::exec;
lcf::async::Awaitable<int> queued_answer(std::thread::id caller, int & steps, int mode)
{
    EXPECT_EQ(std::this_thread::get_id(), caller);
    ++steps;
    co_await lcf::async::yield();
    EXPECT_EQ(std::this_thread::get_id(), caller);
    ++steps;
    if (mode == 1) { throw std::runtime_error("lesson 13"); }
    if (mode == 2) { co_await ex::just_stopped(); ++steps; }
    co_return 42;
}
TEST(CoroutineLessons, Lesson13WhatSyncWaitDrives)
{
    const auto caller = std::this_thread::get_id();
    auto immediate = ex::sync_wait(ex::just(21) | ex::then([&](int value) {
        EXPECT_EQ(std::this_thread::get_id(), caller); return value * 2;
    }));
    ASSERT_TRUE(immediate);
    EXPECT_EQ(std::get<0>(*immediate), 42);
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode);
        int steps = 0;
        auto description = queued_answer(caller, steps, mode);
        EXPECT_EQ(steps, 0);
        if (mode == 1) { EXPECT_THROW(ex::sync_wait(std::move(description)), std::runtime_error); }
        else {
            auto result = ex::sync_wait(std::move(description));
            EXPECT_EQ(result.has_value(), mode == 0);
            if (result) { EXPECT_EQ(std::get<0>(*result), 42); }
        }
        EXPECT_EQ(steps, 2);
    }
}
}
