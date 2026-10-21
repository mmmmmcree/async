#include <gtest/gtest.h>
#include <coroutine>
#include <exception>
#include <thread>
#include <utility>

/*
 * Lesson 01: a coroutine is a function that can retain its execution state.
 * Reading order: the TEST, two_steps(), then ManualCoroutine::promise_type.
 * The reading guide is README.md in this directory.
 *
 * Objective: distinguish calling, suspending, completing and destroying a coroutine.
 * Input: a lazy coroutine, one explicit suspension and a local lifetime counter.
 * Procedure: create it, resume twice, then destroy its owning return object.
 * Expected: locals survive suspension; completion precedes frame destruction;
 *           both resumptions execute on the calling thread.
 * Failure hints: initial_suspend controls entry; final_suspend retains the frame;
 *                destroy releases it. This model has no scheduler or queue.
 * Source map: core/coroutine/Awaitable.h and details/awaitable.h.
 */
namespace {

struct Lifecycle
{
    int m_body_entries = 0;
    int m_body_exits = 0;
    int m_live_locals = 0;
    int m_return_calls = 0;
    int m_final_suspends = 0;
    int m_promise_destructions = 0;
    std::thread::id m_body_thread;
};

class LocalLifetime
{
public:
    ~LocalLifetime() noexcept { --m_lifecycle.m_live_locals; }
    explicit LocalLifetime(Lifecycle & lifecycle) noexcept : m_lifecycle(lifecycle) { ++m_lifecycle.m_live_locals; }
    LocalLifetime(const LocalLifetime &) = delete;
    LocalLifetime & operator=(const LocalLifetime &) = delete;
private:
    Lifecycle & m_lifecycle;
};

class ManualCoroutine
{
public:
    // The compiler looks for this exact name in the coroutine's return type.
    struct promise_type
    {
    public:
        ~promise_type() noexcept { ++m_lifecycle.m_promise_destructions; }
        explicit promise_type(Lifecycle & lifecycle) noexcept : m_lifecycle(lifecycle) {}
    public:
        ManualCoroutine get_return_object() noexcept
        { return ManualCoroutine {std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept
        {
            ++m_lifecycle.m_final_suspends;
            return {};
        }
        void return_void() noexcept { ++m_lifecycle.m_return_calls; }
        // Lesson 03 replaces this limited policy with exception transport.
        void unhandled_exception() const noexcept { std::terminate(); }
    private:
        Lifecycle & m_lifecycle;
    };
private:
    using Self = ManualCoroutine;
    using Handle = std::coroutine_handle<promise_type>;
public:
    ~ManualCoroutine() noexcept { if (m_handle) { m_handle.destroy(); } }
    explicit ManualCoroutine(Handle handle) noexcept : m_handle(handle) {}
    ManualCoroutine(const Self &) = delete;
    ManualCoroutine(Self && other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    Self & operator=(const Self &) = delete;
    Self & operator=(Self &&) = delete;
public:
    // The TEST resumes only a suspended, unfinished coroutine.
    void resume() const
    {
        m_handle.resume();
    }
    bool done() const noexcept
    {
        return m_handle.done();
    }
private:
    Handle m_handle;
};

ManualCoroutine two_steps(Lifecycle & lifecycle)
{
    LocalLifetime local {lifecycle};
    ++lifecycle.m_body_entries;
    lifecycle.m_body_thread = std::this_thread::get_id();

    // The local remains alive while control returns to the caller of resume().
    co_await std::suspend_always {};

    ++lifecycle.m_body_exits;
    lifecycle.m_body_thread = std::this_thread::get_id();
    // This calls return_void(), destroys body locals, then reaches final_suspend().
    co_return;
}

TEST(CoroutineLessons, Lesson01FrameLifetimeAndManualResume)
{
    Lifecycle lifecycle;
    const auto calling_thread = std::this_thread::get_id();
    {
        // get_return_object() has run; initial_suspend() prevents body entry.
        auto work = two_steps(lifecycle);
        EXPECT_EQ(lifecycle.m_body_entries, 0);
        EXPECT_EQ(lifecycle.m_live_locals, 0);
        ASSERT_FALSE(work.done());

        // Run from initial_suspend to the explicit co_await.
        work.resume();
        EXPECT_EQ(lifecycle.m_body_entries, 1);
        EXPECT_EQ(lifecycle.m_body_exits, 0);
        EXPECT_EQ(lifecycle.m_live_locals, 1);
        EXPECT_EQ(lifecycle.m_body_thread, calling_thread);
        EXPECT_EQ(lifecycle.m_return_calls, 0);
        ASSERT_FALSE(work.done());

        // Run from that co_await to final_suspend, retaining the completed frame.
        work.resume();
        EXPECT_EQ(lifecycle.m_body_exits, 1);
        EXPECT_EQ(lifecycle.m_body_thread, calling_thread);
        EXPECT_EQ(lifecycle.m_live_locals, 0);
        EXPECT_EQ(lifecycle.m_return_calls, 1);
        EXPECT_EQ(lifecycle.m_final_suspends, 1);
        EXPECT_EQ(lifecycle.m_promise_destructions, 0);
        EXPECT_TRUE(work.done());

        // A coroutine at final_suspend must not be resumed again.
    }
    EXPECT_EQ(lifecycle.m_promise_destructions, 1);
}

}
