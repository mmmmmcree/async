#include <NativeApi.h>
#include <gtest/gtest.h>
#include <expected>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

/*
 * Lesson 31: account for every owner and queue hop in one complete run.
 * Predict the graph before opening its operation types:
 *   frame context --starts_on--> producer context (worker) --split--> branches
 *   branch A: retry locally -> continues_on(frame) -> join
 *   branch B: signal wait -> timer wait -> join -> root receiver
 * A one-callback budget limits dispatch admission, not inline graph work.
 * Read the TEST, application(), produce(), then compare success and stop.
 * Explicit gates: the worker is joined before inspecting its results, and
 * the signal is emitted only after branch B has subscribed. A second gate
 * observes timer registration before expiry or cancellation is delivered.
 * The frame context owns that timer; the producer context cannot deliver it.
 * All waits are bounded. Markers count frame ownership through permanent shutdown.
 * Source map: async.h -> execution/{scheduling,split,when_all,error_handling}
 * -> coroutine/{awaitable,sender_awaiter,state} -> notification/timer
 * -> TimerService/EventSource -> context callback pump.
 */
namespace {
namespace ex = lcf::async::exec;
struct Trace
{
    void add(std::string_view event) { std::lock_guard lock {m_mutex}; m_events.push_back(event); }
    std::vector<std::string_view> events() { std::lock_guard lock {m_mutex}; return m_events; }
    std::mutex m_mutex;
    std::vector<std::string_view> m_events;
    int m_producers = 0, m_attempts = 0, m_joined = 0, m_completed = 0;
    bool m_waiting = false, m_timing = false;
    std::thread::id m_producer_thread;
};
lcf::async::Awaitable<int> produce(Trace & trace, std::shared_ptr<int> marker)
{
    ++trace.m_producers;
    trace.m_producer_thread = std::this_thread::get_id();
    trace.add("producer: body");
    co_return *marker;
}
lcf::async::Awaitable<int> application(lcf::async::Scheduler worker, lcf::async::Scheduler frame,
    lcf::async::Signal & signal, Trace & trace, std::chrono::steady_clock::duration timer_duration, std::shared_ptr<int> marker)
{
    using namespace std::chrono_literals;
    trace.add("root: entry");
    auto shared = ex::split(ex::starts_on(worker, produce(trace, marker)));
    auto first = shared | ex::let_value([&](const int & value) {
        return ex::retry([&, value](const ex::RetryContext & attempt) {
            ++trace.m_attempts;
            trace.add(attempt.getAttemptCount() == 1 ? "retry: failure" : "retry: success");
            return ex::from_result(attempt.getAttemptCount() == 1
                ? std::expected<int, int> {std::unexpected(7)} : std::expected<int, int> {value});
        }, 2);
    }) | ex::continues_on(frame);
    auto second = shared | ex::continues_on(frame) | ex::let_value([&](const int & value) {
        trace.m_waiting = true;
        trace.add("branch: wait signal");
        return signal.wait() | ex::let_value([&, value] {
            trace.add("branch: wait timer");
            trace.m_timing = true;
            return lcf::async::schedule_after(frame, timer_duration) | ex::then([value] { return value + 1; });
        });
    });
    const auto [left, right] = co_await ex::when_all(std::move(first), std::move(second));
    ++trace.m_joined;
    trace.add("root: joined");
    co_return left + right;
}
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int value) noexcept { m_value = value; finish(false); }
    void set_error(std::exception_ptr) noexcept { ++m_errors; finish(false); }
    void set_stopped() noexcept { finish(true); }
    void finish(bool stopped) noexcept
    { ++m_trace.m_completed; m_stopped += stopped; m_trace.add(stopped ? "receiver: stopped" : "receiver: value"); }
    Trace & m_trace; int & m_value; int & m_stopped; int & m_errors;
};
TEST(CoroutineLessons, Lesson31TraceTheWholeBackend)
{
    using namespace std::chrono_literals;
    for (bool cancel : {false, true}) {
        SCOPED_TRACE(cancel);
        lcf::async::Context frame, producer;
        lcf::async::Signal signal {frame.getExecutor()};
        std::stop_source stop;
        Trace trace;
        int value = 0, stopped = 0, errors = 0;
        auto marker = std::make_shared<int>(20);
        std::weak_ptr<int> observer = marker;
        const auto caller = std::this_thread::get_id();
        {
            auto graph = lcf::async::as_sender(frame.getScheduler(), application(producer.getScheduler(),
                frame.getScheduler(), signal, trace, cancel ? 1h : 0ns, std::move(marker)));
            auto operation = ex::connect(ex::with_stop_token(std::move(graph), stop.get_token()), Receiver {trace, value, stopped, errors});
            struct Cleanup
            {
                std::stop_source & m_stop; lcf::async::Context & m_frame; lcf::async::Context & m_producer;
                ~Cleanup() { m_stop.request_stop(); m_producer.poll(); m_frame.poll(); }
            } cleanup {stop, frame, producer}; // Drain callbacks before releasing their operation owner.
            ex::start(operation);
            EXPECT_TRUE(trace.events().empty());
            EXPECT_EQ(frame.poll({.m_max_callbacks = 1}).m_callbacks, 1u);
            EXPECT_EQ(trace.events(), (std::vector<std::string_view> {"root: entry"}));
            std::size_t callbacks = 0;
            std::jthread worker([&] { callbacks = producer.poll().m_callbacks; });
            worker.join();
            EXPECT_EQ(callbacks, 1u);
            EXPECT_EQ(trace.m_producers, 1);
            EXPECT_NE(trace.m_producer_thread, caller);
            EXPECT_EQ(trace.m_attempts, 2);
            auto pump_until = [&](auto predicate) {
                const auto deadline = std::chrono::steady_clock::now() + 5s;
                while (!predicate() && std::chrono::steady_clock::now() < deadline) {
                    EXPECT_LE(frame.poll({.m_max_callbacks = 1}).m_callbacks, 1u);
                    std::this_thread::yield();
                }
                return predicate();
            };
            ASSERT_TRUE(pump_until([&] { return trace.m_waiting; }));
            EXPECT_EQ(trace.m_completed, 0);
            EXPECT_FALSE(observer.expired());
            trace.add("caller: signal");
            EXPECT_TRUE(signal.set());
            ASSERT_TRUE(pump_until([&] { return trace.m_timing; }));
            EXPECT_EQ(trace.m_completed, 0);
            EXPECT_EQ(producer.pollOne(), 0u);
            if (cancel) {
                EXPECT_EQ(frame.pollOne(), 0u); // Future timer is still outstanding.
                trace.add("caller: stop");
                stop.request_stop();
            }
            ASSERT_TRUE(pump_until([&] { return trace.m_completed == 1; }));
            frame.poll(); // Drain retained queued registrations before destroying owners.
            EXPECT_EQ(errors, 0);
            EXPECT_EQ(stopped, cancel ? 1 : 0);
            EXPECT_EQ(trace.m_joined, cancel ? 0 : 1);
            EXPECT_EQ(value, cancel ? 0 : 41);
            auto expected = std::vector<std::string_view> {"root: entry", "producer: body", "retry: failure",
                "retry: success", "branch: wait signal", "caller: signal", "branch: wait timer"};
            if (cancel) { expected.insert(expected.end(), {"caller: stop", "receiver: stopped"}); }
            else { expected.insert(expected.end(), {"root: joined", "receiver: value"}); }
            EXPECT_EQ(trace.events(), expected);
        }
        EXPECT_TRUE(observer.expired());
        EXPECT_EQ(frame.getReadyCount(), 0u);
        EXPECT_EQ(producer.getReadyCount(), 0u);
        frame.shutdown(); producer.shutdown();
        frame.restart(); producer.restart();
        EXPECT_EQ(frame.pollOne(), 0u);
        EXPECT_EQ(producer.pollOne(), 0u);
    }
}
}
