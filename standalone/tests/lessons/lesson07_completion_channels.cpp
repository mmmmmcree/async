#include <gtest/gtest.h>
#include <exception>
#include <string>
#include <type_traits>
#include <utility>
#if LCF_ASYNC_TEST_USE_MODULES
import lcf.async;
#else
#include <async.h>
#endif

/*
 * Lesson 07: a terminal outcome has a channel as well as a payload.
 * Predict: each start calls exactly one of value, error or stopped.
 * Read the TEST before ChoiceSender. Signatures describe possibilities at
 * compile time; a runtime choice selects one terminal call for this run.
 * A void result is set_value(); multiple results are separate arguments.
 * set_value_t(int) is a function type used as a label: channel plus argument
 * types. completion_signatures lists those labels; it does not execute them.
 * The small just() examples preview Lesson 08's immediate sender factory.
 * Vary: select all channels and compare just() with just(int, string).
 * Source map: protocol/operations.h and protocol/details/completion.h.
 */
namespace {
namespace ex = lcf::async::exec;
enum class Channel { value, error, stopped };
struct Outcome { int m_calls = 0; Channel m_channel = Channel::value; int m_value = 0; };
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int value) noexcept { ++m_out.m_calls; m_out.m_channel = Channel::value; m_out.m_value = value; }
    void set_error(int error) noexcept { ++m_out.m_calls; m_out.m_channel = Channel::error; m_out.m_value = error; }
    void set_stopped() noexcept { ++m_out.m_calls; m_out.m_channel = Channel::stopped; }
    Outcome & m_out;
};
struct ChoiceSender
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(int), ex::set_stopped_t()>;
    template <class R> struct Operation
    {
        void start() noexcept
        {
            switch (m_choice) {
            case Channel::value: ex::set_value(std::move(m_receiver), 21); return;
            case Channel::error: ex::set_error(std::move(m_receiver), -7); return;
            case Channel::stopped: ex::set_stopped(std::move(m_receiver)); return;
            }
        }
        R m_receiver; Channel m_choice;
    };
    template <class R> auto connect(R receiver) const { return Operation<R> {std::move(receiver), m_choice}; }
    Channel m_choice;
};
struct VoidReceiver
{
    using receiver_concept = ex::receiver_t;
    void set_value() noexcept { ++m_calls; }
    int & m_calls;
};
struct PairReceiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int number, std::string text) noexcept
    { m_number = number; m_text = std::move(text); ++m_calls; }
    int & m_calls; int & m_number; std::string & m_text;
};
TEST(CoroutineLessons, Lesson07CompletionChannelsAndSignatures)
{
    using VoidSignatures = ex::completion_signatures<ex::set_value_t()>;
    using PairSignatures = ex::completion_signatures<ex::set_value_t(int, std::string)>;
    static_assert(std::is_same_v<ex::completion_signatures_of_t<decltype(ex::just())>, VoidSignatures>);
    static_assert(std::is_same_v<ex::completion_signatures_of_t<decltype(ex::just(21, std::string {}))>, PairSignatures>);
    for (auto choice : {Channel::value, Channel::error, Channel::stopped}) {
        Outcome outcome;
        auto operation = ex::connect(ChoiceSender {choice}, Receiver {outcome});
        EXPECT_EQ(outcome.m_calls, 0);
        ex::start(operation);
        EXPECT_EQ(outcome.m_calls, 1);
        EXPECT_EQ(outcome.m_channel, choice);
        if (choice != Channel::stopped) { EXPECT_EQ(outcome.m_value, choice == Channel::value ? 21 : -7); }
    }
    int void_calls = 0, pair_calls = 0, number = 0;
    std::string text;
    auto nothing = ex::connect(ex::just(), VoidReceiver {void_calls});
    auto pair = ex::connect(ex::just(21, std::string {"answer"}), PairReceiver {pair_calls, number, text});
    EXPECT_EQ(void_calls + pair_calls, 0);
    ex::start(nothing);
    ex::start(pair);
    EXPECT_EQ(void_calls, 1);
    EXPECT_EQ(pair_calls, 1);
    EXPECT_EQ(number, 21);
    EXPECT_EQ(text, "answer");
}
}
