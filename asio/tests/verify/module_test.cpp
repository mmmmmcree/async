#include <Observation.h>
#include <gtest/gtest.h>

lcf::async::Scheduler copy_module_scheduler(lcf::async::Scheduler scheduler);

namespace {
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
