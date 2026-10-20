#include <NativeApi.h>
#include <gtest/gtest.h>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

/*
 * Lesson 26: registration itself may execute a cancellation callback.
 * Predict: with a pre-stopped token the callback runs before its constructor
 * returns. Production must defer destructive completion until registration
 * setup is safe: StopRegistrationGate records which side acts after setup.
 * Read the TEST, DestroyReceiver::finish, then the named source paths.
 * The second experiment makes set() and request_stop() win in either order.
 * The terminal receiver deletes its own operation immediately. Move all data
 * needed after deletion into locals before deleting: receiver can die too.
 * Vary: pre-stop, cancel first, signal first (queued value loses), and delivered
 * value first. Exactly one terminal delivery releases the lifetime marker.
 * Source map: StopRegistrationGate.h; NotificationRegistration::start/complete;
 * TimerRegistration::start and SenderAwaiter publication ownership.
 */
namespace {
namespace ex = lcf::async::exec;
struct State
{
    std::function<void()> m_destroy;
    int m_values = 0, m_stopped = 0, m_errors = 0;
};
struct DestroyReceiver
{
    using receiver_concept = ex::receiver_t;
    void finish(int channel) noexcept
    {
        auto * state = m_state;
        auto marker = std::move(m_marker);
        if (channel == 0) { ++state->m_values; }
        else if (channel == 1) { ++state->m_stopped; }
        else { ++state->m_errors; }
        auto destroy = std::exchange(state->m_destroy, {});
        destroy(); // Destroys the operation and potentially this receiver.
    }
    void set_value() noexcept { finish(0); }
    void set_stopped() noexcept { finish(1); }
    void set_error(std::exception_ptr) noexcept { finish(2); }
    State * m_state; std::shared_ptr<int> m_marker;
};
TEST(CoroutineLessons, Lesson26RegistrationAndDestructionRaces)
{
    std::stop_source already_stopped;
    already_stopped.request_stop();
    std::vector<std::string_view> events {"before registration"};
    {
        std::stop_callback registration(already_stopped.get_token(), [&] { events.push_back("callback"); });
        events.push_back("after registration");
    }
    EXPECT_EQ(events, (std::vector<std::string_view> {"before registration", "callback", "after registration"}));
    for (int ordering = 0; ordering < 4; ++ordering) {
        SCOPED_TRACE(ordering);
        lcf::async::Context context;
        lcf::async::Signal signal {context.getExecutor()};
        std::stop_source stop;
        if (ordering == 0) { stop.request_stop(); }
        auto marker = std::make_shared<int>(42);
        std::weak_ptr<int> observer = marker;
        State state;
        auto sender = ex::with_stop_token(signal.wait(), stop.get_token());
        using Operation = ex::connect_result_t<decltype(sender), DestroyReceiver>;
        auto * operation = new Operation(ex::connect(std::move(sender), DestroyReceiver {&state, std::move(marker)}));
        state.m_destroy = [operation] { delete operation; };
        struct Cleanup { State & m_state; ~Cleanup() { if (m_state.m_destroy) { std::exchange(m_state.m_destroy, {})(); } } } cleanup {state};
        ex::start(*operation); // For ordering 0, this deletes operation inside start.
        if (ordering == 1) { stop.request_stop(); signal.set(); }
        if (ordering == 2) { signal.set(); stop.request_stop(); }
        if (ordering == 3) { signal.set(); EXPECT_EQ(context.pollOne(), 1u); stop.request_stop(); }
        context.poll(); // Also drains a stale queued registration after cancellation.
        EXPECT_EQ(state.m_values, ordering == 3 ? 1 : 0);
        EXPECT_EQ(state.m_stopped, ordering == 3 ? 0 : 1);
        EXPECT_EQ(state.m_errors, 0);
        EXPECT_FALSE(state.m_destroy);
        EXPECT_TRUE(observer.expired());
        EXPECT_EQ(context.getReadyCount(), 0u);
    }
}
}
