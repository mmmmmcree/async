#include <NativeApi.h>
#include <gtest/gtest.h>
#include <functional>
#include <stdexcept>
#include <vector>

/*
 * Lesson 21: a join stores outcomes until every child has terminated.
 * Predict: finishing the second branch first cannot notify the receiver;
 * successful values retain input order, not completion order. Even a known
 * error waits for an uncooperative sibling that ignores its stop token.
 * Read the TEST, ManualSender, Receiver. The saved callbacks borrow child
 * operation states; consume them before the owning join is destroyed.
 * Vary: finish successfully in reverse input order, then deliver an error
 * before its still-pending sibling. Neither first outcome completes the join.
 * Source map: execution/when_all.h, remaining count and terminal storage.
 */
namespace {
namespace ex = lcf::async::exec;
struct Gate { std::function<void()> m_finish; int m_started = 0; };
struct ManualSender
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr)>;
    template <class R> struct Operation
    {
        void start() noexcept
        {
            ++m_gate.m_started;
            m_gate.m_finish = [this] {
                auto receiver = std::move(m_receiver);
                if (m_fail) { ex::set_error(std::move(receiver), std::make_exception_ptr(std::runtime_error("branch"))); }
                else { ex::set_value(std::move(receiver), m_value); }
            };
        }
        R m_receiver; Gate & m_gate; int m_value; bool m_fail;
    };
    template <class R> auto connect(R receiver) const { return Operation<R> {std::move(receiver), m_gate, m_value, m_fail}; }
    Gate & m_gate; int m_value; bool m_fail;
};
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int first, int second) noexcept { ++m_values; m_results = {first, second}; }
    void set_error(std::exception_ptr) noexcept { ++m_errors; }
    void set_stopped() noexcept { ADD_FAILURE() << "manual branches do not stop"; }
    int & m_values; int & m_errors; std::vector<int> & m_results;
};
TEST(CoroutineLessons, Lesson21JoinIndependentBranches)
{
    for (bool fail : {false, true}) {
        SCOPED_TRACE(fail);
        Gate first, second;
        int values = 0, errors = 0;
        std::vector<int> results;
        auto operation = ex::connect(ex::when_all(ManualSender {first, 20, fail}, ManualSender {second, 22, false}),
            Receiver {values, errors, results});
        ex::start(operation);
        EXPECT_EQ(first.m_started, 1);
        EXPECT_EQ(second.m_started, 1);
        ASSERT_TRUE(first.m_finish);
        ASSERT_TRUE(second.m_finish);
        // Move the callback out before invoking it; its captured operation
        // may be released by terminal delivery in more general receivers.
        auto finish_earlier = std::exchange(fail ? first.m_finish : second.m_finish, {});
        finish_earlier();
        EXPECT_EQ(values + errors, 0);
        auto finish_later = std::exchange(fail ? second.m_finish : first.m_finish, {});
        finish_later();
        EXPECT_EQ(values + errors, 1);
        EXPECT_EQ(errors, fail ? 1 : 0);
        if (!fail) { EXPECT_EQ(results, (std::vector<int> {20, 22})); }
        EXPECT_FALSE(first.m_finish);
        EXPECT_FALSE(second.m_finish);
    }
}
}
