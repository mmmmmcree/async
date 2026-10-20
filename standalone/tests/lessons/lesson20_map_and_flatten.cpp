#include <NativeApi.h>
#include <gtest/gtest.h>
#include <memory>

/*
 * Lesson 20: returning a sender as a value differs from starting a sender.
 * Predict: then(just) produces a sender object; let_value(just) produces 42.
 * Read the TEST. let_value retains input storage while its factory's nested
 * operation is pending, so a nested callback may borrow that stored input.
 * Vary: defer that nested operation onto a context and track owned input.
 * Keep the outer operation alive through completion and inspect destruction
 * after its scope ends; terminal delivery need not destroy operation storage.
 * Source map: execution/continuations.h and protocol/details/utility.h.
 */
namespace {
namespace ex = lcf::async::exec;
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value(int value) noexcept { m_value = value; ++m_calls; }
    void set_error(std::exception_ptr) noexcept { ADD_FAILURE() << "unexpected error"; }
    void set_stopped() noexcept { ADD_FAILURE() << "unexpected stop"; }
    int & m_value; int & m_calls;
};
TEST(CoroutineLessons, Lesson20ValueMappingAndOperationCreation)
{
    auto boxed = ex::sync_wait(ex::just(21) | ex::then([](int value) { return ex::just(value * 2); }));
    ASSERT_TRUE(boxed);
    static_assert(ex::sender<std::tuple_element_t<0, typename decltype(boxed)::value_type>>);
    auto unboxed = ex::sync_wait(std::move(std::get<0>(*boxed)));
    ASSERT_TRUE(unboxed);
    EXPECT_EQ(std::get<0>(*unboxed), 42);
    auto flattened = ex::sync_wait(ex::just(21) | ex::let_value([](int & value) { return ex::just(value * 2); }));
    ASSERT_TRUE(flattened);
    EXPECT_EQ(std::get<0>(*flattened), 42);

    lcf::async::Context context;
    auto payload = std::make_shared<int>(21);
    std::weak_ptr<int> observer = payload;
    int factories = 0, nested_calls = 0, value = 0, completions = 0;
    {
        auto graph = ex::just(std::move(payload)) | ex::let_value([&](std::shared_ptr<int> & retained) {
            ++factories;
            return ex::schedule(context.getScheduler()) | ex::then([&retained, &nested_calls] {
                ++nested_calls; return *retained * 2;
            });
        });
        auto operation = ex::connect(std::move(graph), Receiver {value, completions});
        EXPECT_EQ(factories, 0);
        ex::start(operation);
        EXPECT_EQ(factories, 1);
        EXPECT_EQ(nested_calls, 0);
        EXPECT_FALSE(observer.expired());
        EXPECT_EQ(context.pollOne(), 1u);
        EXPECT_EQ(nested_calls, 1);
        EXPECT_EQ(completions, 1);
        EXPECT_EQ(value, 42);
    }
    EXPECT_TRUE(observer.expired());
}
}
