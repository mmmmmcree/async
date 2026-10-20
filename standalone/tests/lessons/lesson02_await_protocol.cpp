#include <gtest/gtest.h>
#include <array>
#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

/*
 * Lesson 02: co_await is a protocol, not an instruction to enqueue work.
 * Reading order: the TEST's three inputs, await_answer(), then ProbeAwaiter.
 *
 * Objective: observe the three paths through await_ready/suspend/resume.
 * Input: an already-ready awaiter, a suspension rejected by await_suspend,
 *        and a suspension that requires an explicit later resumption.
 * Procedure: start each coroutine and resume its borrowed handle when suspended.
 * Expected: all paths yield 21 to the co_await expression and finally store 42;
 *           only the last path returns to the caller before await_resume.
 * Failure hints: await_ready=true skips await_suspend, not await_resume;
 *                await_suspend=false continues without another resume call.
 * Source map: details/sender_awaiter.h and details/awaitable.h::TaskAwaiter.
 */
namespace {

struct AwaitState
{
    bool m_ready = false;
    bool m_should_suspend = false;
    int m_ready_calls = 0;
    int m_suspend_calls = 0;
    int m_resume_calls = 0;
    // A handle is a non-owning reference to a frame, not an owning smart pointer.
    std::coroutine_handle<> m_waiting;
};

class ProbeAwaiter
{
public:
    explicit ProbeAwaiter(AwaitState & state) noexcept : m_state(state) {}
public:
    bool await_ready() const noexcept
    {
        ++m_state.m_ready_calls;
        return m_state.m_ready;
    }
    bool await_suspend(std::coroutine_handle<> waiting) const noexcept
    {
        ++m_state.m_suspend_calls;
        if (m_state.m_should_suspend) { m_state.m_waiting = waiting; }
        return m_state.m_should_suspend;
    }
    int await_resume() const noexcept
    {
        ++m_state.m_resume_calls;
        return 21;
    }
private:
    AwaitState & m_state;
};

// The small owner from Lesson 01, without its lifecycle instrumentation.
class PausedCoroutine
{
public:
    struct promise_type
    {
        PausedCoroutine get_return_object() noexcept
        { return PausedCoroutine {std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}
        void unhandled_exception() const noexcept { std::terminate(); }
    };
private:
    using Self = PausedCoroutine;
    using Handle = std::coroutine_handle<promise_type>;
public:
    ~PausedCoroutine() noexcept { if (m_handle) { m_handle.destroy(); } }
    explicit PausedCoroutine(Handle handle) noexcept : m_handle(handle) {}
    PausedCoroutine(const Self &) = delete;
    PausedCoroutine(Self && other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    Self & operator=(const Self &) = delete;
    Self & operator=(Self &&) = delete;
public:
    void resume() const { m_handle.resume(); }
    bool done() const noexcept { return m_handle.done(); }
private:
    Handle m_handle;
};

PausedCoroutine await_answer(AwaitState & state, std::optional<int> & answer_opt)
{
    // The expression's value comes from await_resume(), not await_suspend().
    const int value = co_await ProbeAwaiter {state};
    answer_opt = value * 2;
}

TEST(CoroutineLessons, Lesson02AwaitReadySuspendResume)
{
    struct Input
    {
        const char * m_name_p;
        bool m_ready;
        bool m_should_suspend;
    };
    const std::array inputs {
        Input {"await_ready returns true", true, true},
        Input {"await_suspend returns false", false, false},
        Input {"await_suspend returns true", false, true},
    };

    for (const auto & input : inputs) {
        SCOPED_TRACE(input.m_name_p);
        AwaitState state {input.m_ready, input.m_should_suspend};
        std::optional<int> answer_opt;
        auto work = await_answer(state, answer_opt);
        EXPECT_EQ(state.m_ready_calls, 0);

        work.resume();
        EXPECT_EQ(state.m_ready_calls, 1);
        EXPECT_EQ(state.m_suspend_calls, input.m_ready ? 0 : 1);

        if (not input.m_ready and input.m_should_suspend) {
            EXPECT_FALSE(work.done());
            EXPECT_FALSE(answer_opt);
            EXPECT_EQ(state.m_resume_calls, 0);
            ASSERT_TRUE(state.m_waiting);

            // This is the missing resumer. No event loop or worker exists here.
            auto waiting = std::exchange(state.m_waiting, {});
            waiting.resume();
        } else {
            EXPECT_FALSE(state.m_waiting);
        }

        EXPECT_EQ(state.m_resume_calls, 1);
        ASSERT_TRUE(answer_opt);
        EXPECT_EQ(*answer_opt, 42);
        EXPECT_TRUE(work.done());
        // Only work owns and destroys the frame; waiting never destroys it.
    }
}

}
