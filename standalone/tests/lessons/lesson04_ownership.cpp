#include <gtest/gtest.h>
#include <array>
#include <coroutine>
#include <exception>
#include <memory>
#include <utility>

/*
 * Lesson 04: ownership survives suspension and moves between return objects.
 * Reading order: the TEST's three cases, parent()/child(), then OwnedCoroutine.
 * Prerequisites: Lessons 01-03 and ordinary unique_ptr/move semantics.
 *
 * Objective: account for frames, owned parameters and borrowed references.
 * Input: a lazy parent owning a payload, an externally owned integer, and a child
 *        that receives the payload and pauses while borrowing that same integer.
 * Procedure: move the return object; discard before start, finish normally, or
 *            destroy the parent while both parent and child are suspended.
 * Expected: one payload release, both constructed locals destroyed, no execution
 *           after a discarded suspension, and the external integer still alive.
 * Failure hints: inspect exchange in the move/co_await operations, the awaiter's
 *                destructor, and parameter lifetime independently of body locals.
 * Source map: core/coroutine/Awaitable.h; TaskAwaiter and its FrameOwner in
 *             core/coroutine/details/awaitable.h.
 *
 * This experiment has no external callbacks. Destroying suspended work here is
 * safe because its handles are used only by this test and its owning awaiter.
 * Later registration lessons add the coordination required by external resumers.
 */
namespace {

struct Observation
{
    int m_body_entries = 0;
    int m_live_locals = 0;
    int m_destroyed_locals = 0;
    int m_after_await = 0;
    int m_seen_before = 0;
    int m_seen_after = 0;
    const int * m_borrowed_p = nullptr;
};

class Payload
{
public:
    ~Payload() noexcept { --m_live_payloads; }
    explicit Payload(int & live_payloads) noexcept : m_live_payloads(live_payloads) { ++m_live_payloads; }
    Payload(const Payload &) = delete;
    Payload & operator=(const Payload &) = delete;
private:
    int & m_live_payloads;
};

class BodyLocal
{
public:
    ~BodyLocal() noexcept
    {
        --m_observation.m_live_locals;
        ++m_observation.m_destroyed_locals;
    }
    explicit BodyLocal(Observation & observation) noexcept : m_observation(observation)
    {
        ++m_observation.m_body_entries;
        ++m_observation.m_live_locals;
    }
    BodyLocal(const BodyLocal &) = delete;
    BodyLocal & operator=(const BodyLocal &) = delete;
private:
    Observation & m_observation;
};

// Lesson 03's owning task, reduced to void results to focus on ownership.
class OwnedCoroutine
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
        OwnedCoroutine get_return_object() noexcept
        { return OwnedCoroutine {std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        FinalAwaiter final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}
        void unhandled_exception() const noexcept { std::terminate(); }
        std::coroutine_handle<> m_continuation = std::noop_coroutine();
    };
private:
    using Self = OwnedCoroutine;
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
            return m_handle;
        }
        void await_resume() const noexcept {}
    private:
        Handle m_handle;
    };
public:
    ~OwnedCoroutine() noexcept { if (m_handle) { m_handle.destroy(); } }
    explicit OwnedCoroutine(Handle handle) noexcept : m_handle(handle) {}
    OwnedCoroutine(const Self &) = delete;
    OwnedCoroutine(Self && other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    Self & operator=(const Self &) = delete;
    Self & operator=(Self &&) = delete;
public:
    auto operator co_await() && noexcept { return Awaiter {std::exchange(m_handle, {})}; }
    bool ownsFrame() const noexcept { return static_cast<bool>(m_handle); }
    void start() const { m_handle.resume(); }
    bool done() const noexcept { return m_handle.done(); }
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

OwnedCoroutine child(std::unique_ptr<Payload> payload_up, int & borrowed, ManualPause & pause, Observation & observation)
{
    BodyLocal local {observation};
    observation.m_borrowed_p = &borrowed;
    observation.m_seen_before = borrowed;
    // The by-value unique_ptr is retained in this frame, even across this await.
    (void)payload_up;
    co_await pause;
    observation.m_seen_after = borrowed;
    ++observation.m_after_await;
}

OwnedCoroutine parent(std::unique_ptr<Payload> payload_up, int & borrowed, ManualPause & pause, Observation & observation)
{
    BodyLocal local {observation};
    co_await child(std::move(payload_up), borrowed, pause, observation);
    ++observation.m_after_await;
}

TEST(CoroutineLessons, Lesson04OwnershipAcrossSuspension)
{
    struct Input
    {
        const char * m_name_p;
        bool m_start;
        bool m_finish;
    };
    const std::array inputs {
        Input {"discard before first resume", false, false},
        Input {"complete parent and child", true, true},
        Input {"destroy suspended parent and child", true, false},
    };

    for (const auto & input : inputs) {
        SCOPED_TRACE(input.m_name_p);
        // These objects outlive every frame that borrows them.
        int live_payloads = 0;
        int borrowed = 10;
        ManualPause pause;
        Observation observation;
        auto original = parent(std::make_unique<Payload>(live_payloads), borrowed, pause, observation);
        EXPECT_EQ(live_payloads, 1);
        EXPECT_EQ(observation.m_body_entries, 0);
        {
            // Moving the return object transfers a handle, not the frame itself.
            auto owner = std::move(original);
            EXPECT_FALSE(original.ownsFrame());
            ASSERT_TRUE(owner.ownsFrame());
            EXPECT_EQ(live_payloads, 1);
            borrowed = 20;

            if (input.m_start) {
                owner.start();
                ASSERT_FALSE(owner.done());
                EXPECT_EQ(observation.m_body_entries, 2);
                EXPECT_EQ(observation.m_live_locals, 2);
                EXPECT_EQ(observation.m_seen_before, 20);
                EXPECT_EQ(observation.m_borrowed_p, &borrowed);
                EXPECT_EQ(live_payloads, 1);

                auto waiting = pause.takeWaiting();
                ASSERT_TRUE(waiting);
                borrowed = 30;
                if (input.m_finish) {
                    waiting.resume();
                    EXPECT_TRUE(owner.done());
                    EXPECT_EQ(observation.m_seen_after, 30);
                    EXPECT_EQ(observation.m_after_await, 2);
                    EXPECT_EQ(observation.m_live_locals, 0);
                    EXPECT_EQ(live_payloads, 0);
                }
                // Forget this borrowed handle. It never owns/destroys a frame.
                // After normal completion it already refers to a destroyed child.
                waiting = {};
            }
            // If still suspended, destroy the parent and its owning child awaiter.
            // Their destructors release locals/parameters without executing further.
        }

        EXPECT_EQ(live_payloads, 0);
        EXPECT_EQ(observation.m_live_locals, 0);
        EXPECT_EQ(observation.m_destroyed_locals, input.m_start ? 2 : 0);
        EXPECT_EQ(observation.m_after_await, input.m_finish ? 2 : 0);
        EXPECT_EQ(borrowed, input.m_start ? 30 : 20);
        // A reference in a frame did not own or destroy this external integer.
    }
}

}
