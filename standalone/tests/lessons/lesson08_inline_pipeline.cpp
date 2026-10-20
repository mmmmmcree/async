#include <gtest/gtest.h>
#include <exception>
#include <utility>
#if LCF_ASYNC_TEST_USE_MODULES
import lcf.async;
#else
#include <async.h>
#endif

/*
 * Lesson 08: can one start execute an entire graph?
 * Predict: building and connecting execute no continuation.
 * Read the TEST. A pipe passes its left sender to the right adaptor.
 * just delivers inline; then maps and immediately completes its receiver.
 * Therefore the whole chain finishes before start returns.
 * Vary: a second then adds a logical step without adding a queue callback.
 * Source map: execution/factories.h, continuations.h, details/adaptor.h.
 */
namespace {
namespace ex = lcf::async::exec;
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int value) noexcept { m_value = value; ++m_completions; }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    int & m_value; int & m_completions;
};
TEST(CoroutineLessons, Lesson08InlinePipeline)
{
    int maps = 0, value = 0, completions = 0;
    auto graph = ex::just(21)
        | ex::then([&](int input) { ++maps; return input * 2; })
        | ex::then([&](int input) { ++maps; return input + 1; });
    EXPECT_EQ(maps, 0);
    auto operation = ex::connect(std::move(graph), Receiver {value, completions});
    EXPECT_EQ(maps, 0);
    EXPECT_EQ(completions, 0);
    ex::start(operation);
    EXPECT_EQ(maps, 2);
    EXPECT_EQ(completions, 1);
    EXPECT_EQ(value, 43);
}
}
