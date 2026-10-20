#include <NativeApi.h>
#include <gtest/gtest.h>
#include <functional>
#include <thread>

/*
 * Lesson 16: completion and suspension setup agree on one continuation.
 * Predict these timelines in the real SenderAwaiter (not a teaching copy):
 *   inline: registering -> completed, await_suspend returns false;
 *   worker joined inside start: the same path, with cross-thread publication;
 *   delayed: registering -> suspended -> completed, publish resumes once.
 * Read the TEST, waiting(), ControlledSender::Operation::start.
 * The atomic exchange publishes stored values and chooses who continues.
 * An arbitrary racing producer is harder to observe; gates here force both
 * sides of that race deterministically. No scheduler is supplied, so delayed
 * completion resumes directly on the completing thread.
 * Source map: SenderAwaiter::await_suspend/publish/resume and Phase.
 */
namespace {
namespace ex = lcf::async::exec;
struct Control { std::function<void()> m_complete; int m_starts = 0; int m_deliveries = 0; };
struct ControlledSender
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    template <class R> struct Operation
    {
        void start() noexcept
        {
            auto & control = m_control;
            ++control.m_starts;
            control.m_complete = [this] {
                ++m_control.m_deliveries;
                auto receiver = std::move(m_receiver);
                ex::set_value(std::move(receiver), 21);
            };
            if (m_mode != 2) {
                auto complete = std::exchange(control.m_complete, {});
                if (m_mode == 0) { complete(); }
                else { std::jthread producer(std::move(complete)); } // joins before start returns
            }
        }
        R m_receiver; Control & m_control; int m_mode;
    };
    template <class R> auto connect(R receiver) const { return Operation<R> {std::move(receiver), m_control, m_mode}; }
    Control & m_control; int m_mode;
};
lcf::async::Awaitable<int> waiting(Control & control, int mode, int & stages, std::thread::id & resumed)
{
    ++stages;
    const auto answer = co_await ControlledSender {control, mode};
    resumed = std::this_thread::get_id();
    ++stages;
    co_return answer * 2;
}
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int value) noexcept { m_value = value; ++m_completions; }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    int & m_value; int & m_completions;
};
TEST(CoroutineLessons, Lesson16CompletionDuringSuspensionSetup)
{
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode);
        Control control;
        int stages = 0, value = 0, completions = 0;
        std::thread::id resumed, producer_id;
        auto operation = ex::connect(waiting(control, mode, stages, resumed), Receiver {value, completions});
        ex::start(operation);
        if (mode == 2) {
            EXPECT_EQ(stages, 1);
            EXPECT_EQ(completions, 0);
            ASSERT_TRUE(control.m_complete);
            auto complete = std::exchange(control.m_complete, {});
            std::jthread producer([&] { producer_id = std::this_thread::get_id(); complete(); });
            producer.join();
            EXPECT_EQ(resumed, producer_id);
        } else { EXPECT_EQ(resumed, std::this_thread::get_id()); }
        EXPECT_EQ(control.m_starts, 1);
        EXPECT_EQ(control.m_deliveries, 1);
        EXPECT_EQ(stages, 2);
        EXPECT_EQ(completions, 1);
        EXPECT_EQ(value, 42);
        EXPECT_FALSE(control.m_complete);
    }
}
}
