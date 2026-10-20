#include <Observation.h>
#include <gtest/gtest.h>

namespace {

/*
 * Objective: an application factory creates independent lazy executions.
 * Input: a worker scheduler and separate initial values.
 * Procedure: build, share, branch and join each execution through public algorithms.
 * Expected: each producer runs once and its two branches produce the expected sum.
 * Failure hints: inspect starts_on, split caching and when_all result order.
 */
void independent_factory_runs();
TEST(Sandbox, IndependentFactoryRuns) { independent_factory_runs(); }

void independent_factory_runs()
{
    namespace async = lcf::async;
    namespace ex = async::exec;
    async::testing::Worker worker;
    unsigned calls = 0;
    const auto make_work = [&](int initial) {
        auto shared = ex::split(ex::starts_on(worker.getScheduler(), ex::just(initial) | ex::then([&](int value) { ++calls; return value * 2; })));
        return ex::when_all(shared | ex::then([](const int & value) { return value + 1; }),
            shared | ex::then([](const int & value) { return value + 2; }));
    };
    auto first = ex::sync_wait(make_work(10));
    auto second = ex::sync_wait(make_work(20));
    ASSERT_TRUE(first); ASSERT_TRUE(second);
    EXPECT_EQ(std::get<0>(*first), 21); EXPECT_EQ(std::get<1>(*first), 22);
    EXPECT_EQ(std::get<0>(*second), 41); EXPECT_EQ(std::get<1>(*second), 42);
    EXPECT_EQ(calls, 2u);
}

}
