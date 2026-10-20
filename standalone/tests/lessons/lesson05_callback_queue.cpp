#include <gtest/gtest.h>
#include <coroutine>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <utility>

/*
 * Lesson 05: who resumes a coroutine when its caller stops doing so?
 * Predict: enqueueing transfers a callable, but does not execute it.
 * Read the TEST, queued_work(), then CallbackQueue and QueuedCoroutine.
 * The queue owns its callable; the callable borrows the frame. Keep the frame
 * owner alive until the callback is consumed. A handle is not cancellation.
 * Vary: pump an empty queue after completion; no callback executes twice.
 * Source map: core/scheduling/details/queue.h (Callback), Context::pollOne.
 */
namespace {
class CallbackQueue
{
public:
    void push(std::function<void()> callback) { m_callbacks.push_back(std::move(callback)); }
    bool pollOne()
    {
        if (m_callbacks.empty()) { return false; }
        auto callback = std::move(m_callbacks.front());
        m_callbacks.pop_front();
        callback();
        return true;
    }
private:
    std::deque<std::function<void()>> m_callbacks;
};
class QueuedCoroutine
{
public:
    struct promise_type
    {
        QueuedCoroutine get_return_object() noexcept
        { return QueuedCoroutine {std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}
        void unhandled_exception() const noexcept { std::terminate(); }
    };
    explicit QueuedCoroutine(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    QueuedCoroutine(const QueuedCoroutine &) = delete;
    QueuedCoroutine(QueuedCoroutine && other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    ~QueuedCoroutine() { if (m_handle) { m_handle.destroy(); } }
    void start() { m_handle.resume(); }
    bool done() const { return m_handle.done(); }
private:
    std::coroutine_handle<promise_type> m_handle;
};
struct QueuePause
{
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> waiting) const
    { m_queue.push([waiting] { waiting.resume(); }); }
    void await_resume() const noexcept {}
    CallbackQueue & m_queue;
};
QueuedCoroutine queued_work(CallbackQueue & queue, int & steps)
{
    ++steps;
    co_await QueuePause {queue};
    ++steps;
}
TEST(CoroutineLessons, Lesson05HandDrivenCallbackQueue)
{
    CallbackQueue queue;
    int steps = 0;
    auto owner = queued_work(queue, steps);
    owner.start();
    EXPECT_EQ(steps, 1);
    EXPECT_FALSE(owner.done());
    EXPECT_TRUE(queue.pollOne());
    EXPECT_EQ(steps, 2);
    EXPECT_TRUE(owner.done());
    EXPECT_FALSE(queue.pollOne());
    auto payload = std::make_shared<int>(21);
    std::weak_ptr<int> observer = payload;
    queue.push([payload, &steps] { steps += *payload; });
    payload.reset();
    EXPECT_FALSE(observer.expired());
    EXPECT_EQ(steps, 2);
    EXPECT_TRUE(queue.pollOne());
    EXPECT_EQ(steps, 23);
    EXPECT_TRUE(observer.expired());
}
}
