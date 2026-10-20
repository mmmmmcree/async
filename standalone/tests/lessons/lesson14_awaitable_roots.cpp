#include <NativeApi.h>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

/*
 * Lesson 14: the same lazy Awaitable can be a child or a root sender.
 * Predict: calls retain parameters but do not enter bodies. Both paths move
 * a unique_ptr without copying it; void supplies no value argument.
 * Read the TEST, parent(), then the small child factories.
 * TaskAwaiter owns a child until await_resume takes its result and destroys
 * its frame. AwaitableOperation instead owns a root and notifies a receiver.
 * Vary: value, void, move-only value and an exception through both paths.
 * Source map: Awaitable.h; AwaitablePromise/TaskAwaiter/AwaitableOperation;
 * ReturnStorage.h and coroutine/details/traits.h.
 */
namespace {
namespace ex = lcf::async::exec;
lcf::async::Awaitable<int> answer(int & entries) { ++entries; co_return 21; }
lcf::async::Awaitable<void> visit(int & entries) { ++entries; co_return; }
lcf::async::Awaitable<std::unique_ptr<int>> transfer(std::unique_ptr<int> value, int & entries)
{ ++entries; co_return std::move(value); }
lcf::async::Awaitable<int> failure() { throw std::runtime_error("lesson 14"); co_return 0; }
lcf::async::Awaitable<std::unique_ptr<int>> parent(std::unique_ptr<int> value, int & entries)
{
    const int first = co_await answer(entries);
    co_await visit(entries);
    auto owned = co_await transfer(std::move(value), entries);
    *owned += first;
    try { co_await failure(); }
    catch (const std::runtime_error &) { ++entries; }
    co_return std::move(owned);
}
TEST(CoroutineLessons, Lesson14AwaitableAsChildAndRoot)
{
    int entries = 0;
    auto value = std::make_unique<int>(21);
    const auto * identity = value.get();
    auto nested = parent(std::move(value), entries);
    EXPECT_EQ(entries, 0);
    EXPECT_FALSE(value);
    auto result = ex::sync_wait(std::move(nested));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result).get(), identity);
    EXPECT_EQ(*std::get<0>(*result), 42);
    EXPECT_EQ(entries, 4);
    auto number = ex::sync_wait(answer(entries));
    ASSERT_TRUE(number);
    EXPECT_EQ(std::get<0>(*number), 21);
    auto nothing = ex::sync_wait(visit(entries));
    ASSERT_TRUE(nothing);
    static_assert(std::tuple_size_v<typename decltype(nothing)::value_type> == 0);
    auto root_owner = ex::sync_wait(transfer(std::make_unique<int>(7), entries));
    ASSERT_TRUE(root_owner);
    EXPECT_EQ(*std::get<0>(*root_owner), 7);
    EXPECT_THROW(ex::sync_wait(failure()), std::runtime_error);
    EXPECT_EQ(entries, 7);
}
}
