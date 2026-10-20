#include <Observation.h>
#include <gtest/gtest.h>

lcf::async::Scheduler copy_module_scheduler(lcf::async::Scheduler scheduler);

namespace {
/*
 * Objective: module consumers exchange the same public scheduler type across translation units.
 * Input: a scheduler bound to a control context and a separate import-only bridge.
 * Procedure: round-trip the scheduler through the bridge, then schedule one receiver.
 * Expected: the original context delivers exactly one value completion.
 * Failure hints: inspect the exported Scheduler declaration, module mapping, and queue ownership.
 */
void module_consumers_exchange_types();
TEST(Modules, ConsumersExchangeTypes) { module_consumers_exchange_types(); }

void module_consumers_exchange_types()
{
    namespace async = lcf::async;
    async::testing::Context context;
    auto scheduler = copy_module_scheduler(context.getScheduler());
    async::testing::Observation observation;
    async::testing::run(context, async::exec::schedule(scheduler), observation);
    EXPECT_EQ(observation.m_value_calls, 1u);
}
}
