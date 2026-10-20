#include <gtest/gtest.h>
#include <coroutine>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#if LCF_ASYNC_TEST_USE_MODULES
import lcf.async;
#else
#include <async.h>
#endif

/*
 * Lesson 03: an awaited child transfers control back to its suspended parent.
 * Reading order: the TEST, parent()/child(), IntTask::Awaiter, FinalAwaiter.
 *
 * Objective: follow value/error transport and the continuation across two frames.
 * Input: a lazy parent awaiting a child that pauses before returning or throwing.
 * Procedure: start the parent; resume the paused child; inspect the parent result.
 * Expected: final_suspend transfers to the parent; await_resume retrieves 21 or
 *           rethrows; the root completes without a second explicit parent resume.
 * Failure hints: check continuation assignment, final transfer and frame ownership.
 * Source map: TaskAwaiter, AwaitablePromise::FinalAwaiter, ReturnStorage,
 *             CoroutineState::finish/dispatch in core/coroutine/details/.
 */
namespace {

class IntTask
{
public:
    struct promise_type
    {
        struct FinalAwaiter
        {
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> completed) const noexcept
            { return completed.promise().m_continuation; }
            void await_resume() const noexcept {}
        };
        IntTask get_return_object() noexcept
        { return IntTask {std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        FinalAwaiter final_suspend() const noexcept { return {}; }
        void return_value(int value) noexcept { m_value = value; }
        void unhandled_exception() noexcept { m_exception = std::current_exception(); }
        int m_value = 0;
        std::exception_ptr m_exception;
        // A root has no parent. This handle resumes a special no-op coroutine.
        std::coroutine_handle<> m_continuation = std::noop_coroutine();
    };
private:
    using Self = IntTask;
    using Handle = std::coroutine_handle<promise_type>;
    class Awaiter
    {
    public:
        ~Awaiter() noexcept { m_handle.destroy(); }
        explicit Awaiter(Handle handle) noexcept : m_handle(handle) {}
        Awaiter(const Awaiter &) = delete;
        Awaiter & operator=(const Awaiter &) = delete;
    public:
        bool await_ready() const noexcept { return false; }
        Handle await_suspend(std::coroutine_handle<> parent) const noexcept
        {
            m_handle.promise().m_continuation = parent;
            // Returning a handle transfers to the child, without enqueueing it.
            return m_handle;
        }
        int await_resume() const
        {
            const auto & promise = m_handle.promise();
            if (promise.m_exception) { std::rethrow_exception(promise.m_exception); }
            return promise.m_value;
        }
    private:
        Handle m_handle;
    };
public:
    ~IntTask() noexcept { if (m_handle) { m_handle.destroy(); } }
    explicit IntTask(Handle handle) noexcept : m_handle(handle) {}
    IntTask(const Self &) = delete;
    IntTask(Self && other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    Self & operator=(const Self &) = delete;
    Self & operator=(Self &&) = delete;
public:
    auto operator co_await() && noexcept { return Awaiter {std::exchange(m_handle, {})}; }
    void start() const { m_handle.resume(); }
    bool done() const noexcept { return m_handle.done(); }
    // The TEST checks done() before reading a completed root's promise.
    int getResult() const
    {
        const auto & promise = m_handle.promise();
        if (promise.m_exception) { std::rethrow_exception(promise.m_exception); }
        return promise.m_value;
    }
private:
    Handle m_handle;
};

class ManualPause
{
public:
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> waiting) noexcept { m_waiting = waiting; }
    void await_resume() const noexcept {}
    std::coroutine_handle<> takeWaiting() noexcept { return std::exchange(m_waiting, {}); }
private:
    std::coroutine_handle<> m_waiting;
};

using Events = std::vector<std::string_view>;

IntTask child(ManualPause & pause, Events & events, bool fail)
{
    events.push_back("child: before pause");
    co_await pause;
    if (fail) {
        events.push_back("child: throw");
        throw std::runtime_error("lesson child failed");
    }
    events.push_back("child: co_return 21");
    co_return 21;
}

IntTask parent(ManualPause & pause, Events & events, bool fail)
{
    events.push_back("parent: before co_await");
    auto work = child(pause, events, fail);
    // Creation is lazy: the child has not entered its body yet.
    events.push_back("parent: child created");
    const int value = co_await std::move(work);
    events.push_back("parent: after co_await");
    co_return value * 2;
}

lcf::async::Awaitable<int> standalone_child(int & body_entries)
{
    ++body_entries;
    co_return 21;
}

lcf::async::Awaitable<int> standalone_parent(int & body_entries)
{
    ++body_entries;
    const int value = co_await standalone_child(body_entries);
    co_return value * 2;
}

TEST(CoroutineLessons, Lesson03ChildResultAndContinuation)
{
    for (const bool fail : {false, true}) {
        SCOPED_TRACE(fail ? "child throws" : "child returns 21");
        ManualPause pause;
        Events events;
        auto work = parent(pause, events, fail);
        EXPECT_TRUE(events.empty());

        work.start();
        EXPECT_FALSE(work.done());
        const Events paused_events {
            "parent: before co_await", "parent: child created", "child: before pause",
        };
        EXPECT_EQ(events, paused_events);

        // This handle refers to the child, not to work's parent frame.
        auto waiting = pause.takeWaiting();
        ASSERT_TRUE(waiting);
        events.push_back("caller: resume child");
        waiting.resume();
        // Child completion can destroy its frame through the parent's awaiter.
        // The borrowed waiting handle must not be inspected or resumed again.

        ASSERT_TRUE(work.done());
        auto expected_events = paused_events;
        expected_events.push_back("caller: resume child");
        if (fail) {
            expected_events.push_back("child: throw");
            EXPECT_THROW(work.getResult(), std::runtime_error);
        } else {
            expected_events.push_back("child: co_return 21");
            expected_events.push_back("parent: after co_await");
            EXPECT_EQ(work.getResult(), 42);
        }
        EXPECT_EQ(events, expected_events);
    }

    // Optional production preview. Lessons 06 and 13-18 open its driver and implementation.
    int body_entries = 0;
    auto work = standalone_parent(body_entries);
    EXPECT_EQ(body_entries, 0);
    auto result_opt = lcf::async::exec::sync_wait(std::move(work));
    ASSERT_TRUE(result_opt);
    EXPECT_EQ(std::get<0>(*result_opt), 42);
    EXPECT_EQ(body_entries, 2);
}

}
