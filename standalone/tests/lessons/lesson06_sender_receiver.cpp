#include <gtest/gtest.h>
#include <exception>
#include <utility>
#if LCF_ASYNC_TEST_USE_MODULES
import lcf.async;
#else
#include <async.h>
#endif

/*
 * Lesson 06: when does a description of work become a live execution?
 * Predict: construction and connect deliver nothing; start delivers 21 inline.
 * Read the TEST, IntegerSender::connect, Operation::start, RecordingReceiver.
 * The sender describes work. Its operation owns the receiver and the value.
 * The operation must outlive terminal delivery; call start exactly once.
 * ex::connect/start/set_value are function objects: they dispatch to the
 * corresponding member function. A concept is a compile-time requirement;
 * sender_concept/receiver_concept are marker aliases, not base classes.
 * Terminal receiver functions must be noexcept: an error travels through
 * set_error, not by throwing out of completion. Here delivery is synchronous.
 * Vary: connect the same copyable sender to a second receiver.
 * Source map: core/protocol/operations.h and details/utility.h.
 */
namespace {
namespace ex = lcf::async::exec;
struct RecordingReceiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int value) noexcept { *m_value = value; ++*m_calls; }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    int * m_value;
    int * m_calls;
};
struct IntegerSender
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    template <class Receiver> struct Operation
    {
        void start() noexcept { ex::set_value(std::move(m_receiver), m_value); }
        Receiver m_receiver;
        int m_value;
    };
    template <class Receiver>
    auto connect(Receiver receiver) const { return Operation<Receiver> {std::move(receiver), m_value}; }
    int m_value;
};
TEST(CoroutineLessons, Lesson06SenderReceiverAndOperationState)
{
    int value = 0, calls = 0;
    const IntegerSender description {21};
    EXPECT_EQ(calls, 0);
    auto operation = ex::connect(description, RecordingReceiver {&value, &calls});
    static_assert(ex::sender<IntegerSender>);
    static_assert(ex::operation_state<decltype(operation)>);
    EXPECT_EQ(calls, 0);
    ex::start(operation);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(value, 21);
    auto second = ex::connect(description, RecordingReceiver {&value, &calls});
    EXPECT_EQ(calls, 1);
    ex::start(second);
    EXPECT_EQ(calls, 2);
}
}
