#include <Observation.h>
#include <gtest/gtest.h>
#include <expected>
#include <memory_resource>
#include <functional>
#include <barrier>

namespace {

namespace async = lcf::async;
namespace ex = async::exec;
namespace test = async::testing;
using namespace std::chrono_literals;

/*
 * Objective: completion order does not reorder concatenated arguments.
 * Input: the graph and controlled resources in when_all_preserves_input_order.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect sharing completion forwarding and operation ownership.
 */
void when_all_preserves_input_order();
TEST(Sharing, WhenAllPreservesInputOrder) { when_all_preserves_input_order(); }

/*
 * Objective: an error cannot destroy an unfinished sibling.
 * Input: the graph and controlled resources in when_all_waits_after_error.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect sharing completion forwarding and operation ownership.
 */
void when_all_waits_after_error();
TEST(Sharing, WhenAllWaitsAfterError) { when_all_waits_after_error(); }

/*
 * Objective: an error wins over stopped after every child finishes.
 * Input: the graph and controlled resources in when_all_error_precedes_stopped.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect sharing completion forwarding and operation ownership.
 */
void when_all_error_precedes_stopped();
TEST(Sharing, WhenAllErrorPrecedesStopped) { when_all_error_precedes_stopped(); }

/*
 * Objective: a failed child requests cancellation of an already running timer.
 * Input: the graph and controlled resources in when_all_cancels_waiting_sibling.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect sharing completion forwarding and operation ownership.
 */
void when_all_cancels_waiting_sibling();
TEST(Sharing, WhenAllCancelsWaitingSibling) { when_all_cancels_waiting_sibling(); }

/*
 * Objective: a shared when_all node feeds multiple then branches.
 * Input: the graph and controlled resources in split_joined_work_runs_once.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect sharing completion forwarding and operation ownership.
 */
void split_joined_work_runs_once();
TEST(Sharing, SplitJoinedWorkRunsOnce) { split_joined_work_runs_once(); }

/*
 * Objective: late error subscribers observe the same error.
 * Input: the graph and controlled resources in split_cached_error_preserves_identity.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect sharing completion forwarding and operation ownership.
 */
void split_cached_error_preserves_identity();
TEST(Sharing, SplitCachedErrorPreservesIdentity) { split_cached_error_preserves_identity(); }

/*
 * Objective: late subscribers retain the stopped completion channel.
 * Input: the graph and controlled resources in split_caches_stopped.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect sharing completion forwarding and operation ownership.
 */
void split_caches_stopped();
TEST(Sharing, SplitCachesStopped) { split_caches_stopped(); }

/*
 * Objective: transferring a shared value retains references and object identity.
 * Input: the graph and controlled resources in shared_move_only_value_crosses_scheduler.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect sharing completion forwarding and operation ownership.
 */
void shared_move_only_value_crosses_scheduler();
TEST(Sharing, SharedMoveOnlyValueCrossesScheduler) { shared_move_only_value_crosses_scheduler(); }

/*
 * Objective: concurrent subscriptions cannot miss publication or execute the source twice.
 * Input: the graph and controlled resources in split_subscribe_races_completion.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect sharing completion forwarding and operation ownership.
 */
void split_subscribe_races_completion();
TEST(Sharing, SplitSubscribeRacesCompletion) { split_subscribe_races_completion(); }

/*
 * Objective: cached results survive subscriptions and release with the last owner.
 * Input: the graph and controlled resources in split_allocator_releases_cached_payload.
 * Procedure: connect/start or sync_wait, then observe completion and ownership.
 * Expected: the values, channel and lifetime asserted by this case.
 * Failure hints: inspect sharing completion forwarding and operation ownership.
 */
void split_allocator_releases_cached_payload();
TEST(Sharing, SplitAllocatorReleasesCachedPayload) { split_allocator_releases_cached_payload(); }

/*
 * Objective: canceling a branch leaves its source and another subscriber running.
 * Input: two subscriptions to a signal-backed shared producer.
 * Procedure: stop one subscription, then release the producer.
 * Expected: one stopped result and one value, with exactly one producer execution.
 * Failure hints: inspect split's independent subscription stop callbacks and shared ownership.
 */
void split_cancel_preserves_sibling();
TEST(Sharing, SplitCancelPreservesSibling) { split_cancel_preserves_sibling(); }

/*
 * Objective: the final canceled consumer requests upstream stop and releases source captures.
 * Input: a shared one-hour timer without a retained sender descriptor.
 * Procedure: start its only subscription, cancel it, then drain runtime completions.
 * Expected: stopped once and expiration of the producer's lifetime marker.
 * Failure hints: inspect consumer detachment, the shared stop token and cached operation release.
 */
void split_final_consumer_stops_source();
TEST(Sharing, SplitFinalConsumerStopsSource) { split_final_consumer_stops_source(); }

/*
 * Objective: when_all admits independent work on both workers before either finishes.
 * Input: two gated branches and a control scheduler.
 * Procedure: start both, observe concurrent entry, release the gate and pump completion.
 * Expected: both branches entered before release and returned their own values.
 * Failure hints: inspect when_all child startup rather than comparing only thread identities.
 */
void branches_overlap_in_time();
TEST(Sharing, BranchesOverlapInTime) { branches_overlap_in_time(); }

struct ManualState
{
    std::function<void()> m_complete;
    bool m_started = false;
};
struct ManualSender
{
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    template <typename Receiver>
    struct Operation
    {
        using operation_state_concept = ex::operation_state_t;
        void start() noexcept
        {
            m_state_sp->m_started = true;
            m_state_sp->m_complete = [this]() noexcept { ex::set_value(std::move(m_receiver), 7); };
        }
        Receiver m_receiver;
        std::shared_ptr<ManualState> m_state_sp;
    };
    template <typename Receiver>
    auto connect(Receiver receiver) const { return Operation<Receiver> {std::move(receiver), m_state_sp}; }
    std::shared_ptr<ManualState> m_state_sp;
};

class TrackingResource : public std::pmr::memory_resource
{
public:
    unsigned m_allocations = 0;
    unsigned m_deallocations = 0;
private:
    void * do_allocate(std::size_t size, std::size_t alignment) override
    { ++m_allocations; return std::pmr::new_delete_resource()->allocate(size, alignment); }
    void do_deallocate(void * memory_p, std::size_t size, std::size_t alignment) override
    { ++m_deallocations; std::pmr::new_delete_resource()->deallocate(memory_p, size, alignment); }
    bool do_is_equal(const std::pmr::memory_resource & other) const noexcept override { return this == &other; }
};

void when_all_preserves_input_order()
{
    test::Worker first;
    test::Worker second;
    auto result = ex::sync_wait(ex::when_all(
        ex::starts_on(first.getScheduler(), ex::just(1, 2)),
        ex::starts_on(second.getScheduler(), ex::just(3)), ex::just()));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 1);
    EXPECT_EQ(std::get<1>(*result), 2);
    EXPECT_EQ(std::get<2>(*result), 3);
}

void when_all_waits_after_error()
{
    auto state_sp = std::make_shared<ManualState>();
    test::Observation observation;
    auto operation = ex::connect(ex::when_all(ex::just_error(std::make_error_code(std::errc::io_error)), ManualSender {state_sp}), test::Receiver {&observation});
    ex::start(operation);
    EXPECT_TRUE(state_sp->m_started);
    EXPECT_FALSE(observation.m_done.load());
    auto finish = std::move(state_sp->m_complete);
    finish();
    EXPECT_TRUE(observation.m_done.load());
    EXPECT_EQ(observation.m_error_calls, 1u);
    EXPECT_EQ(observation.m_error, std::errc::io_error);
}

void when_all_error_precedes_stopped()
{
    test::Observation observation;
    auto operation = ex::connect(ex::when_all(ex::just_stopped(), ex::just_error(std::make_error_code(std::errc::io_error))), test::Receiver {&observation});
    ex::start(operation);
    EXPECT_EQ(observation.m_error_calls, 1u);
    EXPECT_EQ(observation.m_stopped_calls, 0u);
}

void when_all_cancels_waiting_sibling()
{
    test::Context context;
    auto work = ex::when_all(async::schedule_after(context.getScheduler(), 1h), ex::just_error(std::make_error_code(std::errc::io_error)));
    test::Observation observation;
    test::run(context, std::move(work), observation);
    EXPECT_EQ(observation.m_error_calls, 1u);
    EXPECT_EQ(observation.m_error, std::errc::io_error);
}

void split_joined_work_runs_once()
{
    unsigned calls = 0;
    auto shared = ex::split(ex::when_all(ex::just(20), ex::just(22)) | ex::then([&](int first, int second) { ++calls; return first + second; }));
    auto result = ex::sync_wait(ex::when_all(shared | ex::then([](const int & value) { return value + 1; }),
        shared | ex::then([](const int & value) { return value + 2; })));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 43);
    EXPECT_EQ(std::get<1>(*result), 44);
    EXPECT_EQ(calls, 1u);
}

void split_cached_error_preserves_identity()
{
    auto error = std::make_exception_ptr(std::runtime_error("shared"));
    auto shared = ex::split(ex::just_error(error));
    for (unsigned index = 0; index < 2; ++index) {
        test::Observation observation;
        auto operation = ex::connect(shared, test::Receiver {&observation});
        ex::start(operation);
        EXPECT_EQ(observation.m_error_calls, 1u);
        EXPECT_EQ(observation.m_exception, error);
    }
}

void split_caches_stopped()
{
    auto shared = ex::split(test::StoppedSender {});
    EXPECT_FALSE(ex::sync_wait(shared));
    EXPECT_FALSE(ex::sync_wait(shared));
}

void shared_move_only_value_crosses_scheduler()
{
    test::Worker first;
    test::Worker second;
    auto value_up = std::make_unique<int>(42);
    const auto * original_p = value_up.get();
    auto shared = ex::split(ex::just(std::move(value_up)));
    const int * first_p = nullptr;
    const int * second_p = nullptr;
    auto result = ex::sync_wait(ex::when_all(
        shared | ex::then([](const auto & value) { return std::cref(value); }) | ex::continues_on(first.getScheduler()) | ex::then([&](auto value) { first_p = value.get().get(); return *value.get(); }),
        shared | ex::then([](const auto & value) { return std::cref(value); }) | ex::continues_on(second.getScheduler()) | ex::then([&](auto value) { second_p = value.get().get(); return *value.get(); })));
    ASSERT_TRUE(result);
    EXPECT_EQ(first_p, original_p);
    EXPECT_EQ(second_p, original_p);
}

void split_subscribe_races_completion()
{
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        SCOPED_TRACE(repeat);
        std::atomic_uint calls = 0;
        auto shared = ex::split(ex::just() | ex::then([&] { ++calls; return 42; }));
        std::barrier gate {3};
        int first = 0;
        int second = 0;
        std::jthread one {[&] { gate.arrive_and_wait(); first = std::get<0>(*ex::sync_wait(shared)); }};
        std::jthread two {[&] { gate.arrive_and_wait(); second = std::get<0>(*ex::sync_wait(shared)); }};
        gate.arrive_and_wait();
        one.join(); two.join();
        EXPECT_EQ(first, 42); EXPECT_EQ(second, 42); EXPECT_EQ(calls, 1u);
    }
}

void split_allocator_releases_cached_payload()
{
    TrackingResource resource;
    auto lifetime_sp = std::make_shared<int>(42);
    std::weak_ptr<int> lifetime_wp = lifetime_sp;
    {
        auto source = ex::with_allocator(ex::just(std::move(lifetime_sp)), &resource);
#ifdef LCF_ASYNC_STDEXEC
        auto shared = ex::split(std::move(source), ex::prop {ex::get_allocator, std::pmr::polymorphic_allocator<std::byte> {&resource}});
#else
        auto shared = ex::split(std::move(source), &resource);
#endif
        auto result = ex::sync_wait(shared | ex::then([](const auto & value_sp) { return *value_sp; }));
        ASSERT_TRUE(result); EXPECT_EQ(std::get<0>(*result), 42);
        EXPECT_FALSE(lifetime_wp.expired());
    }
    EXPECT_TRUE(lifetime_wp.expired());
    EXPECT_GT(resource.m_allocations, 0u);
    EXPECT_EQ(resource.m_allocations, resource.m_deallocations);
}

void split_cancel_preserves_sibling()
{
    test::Context context;
    async::Signal signal;
    std::stop_source stop;
    unsigned calls = 0;
    auto shared = ex::split(async::as_sender(context.getScheduler(), signal.wait()) | ex::then([&] { ++calls; return 42; }));
    test::Observation canceled;
    test::Observation active;
    auto first = ex::connect(ex::with_stop_token(shared, stop.get_token()), test::Receiver {&canceled});
    auto second = ex::connect(shared, test::Receiver {&active});
    ex::start(first); ex::start(second); context.poll();
    stop.request_stop();
    test::pump_until(context, [&] { return canceled.m_done.load(); });
    EXPECT_EQ(canceled.m_stopped_calls, 1u);
    EXPECT_FALSE(active.m_done.load());
    signal.set();
    test::pump_until(context, [&] { return active.m_done.load(); });
    EXPECT_EQ(active.m_value_calls, 1u);
    EXPECT_EQ(active.m_values[0], 42);
    EXPECT_EQ(calls, 1u);
}

void split_final_consumer_stops_source()
{
    test::Context context;
    std::stop_source stop;
    auto marker_sp = std::make_shared<int>(42);
    std::weak_ptr<int> marker_wp = marker_sp;
    test::Observation observation;
    {
        auto work = ex::with_stop_token(ex::split(async::schedule_after(context.getScheduler(), 1h)
            | ex::then([marker_sp = std::move(marker_sp)] { return *marker_sp; })), stop.get_token());
        auto operation = ex::connect(std::move(work), test::Receiver {&observation});
        ex::start(operation);
        context.poll();
        stop.request_stop();
        test::pump_until(context, [&] { return observation.m_done.load(); });
        EXPECT_EQ(observation.m_stopped_calls, 1u);
    }
    test::pump_until(context, [&] { return marker_wp.expired(); });
    EXPECT_TRUE(marker_wp.expired());
}

void branches_overlap_in_time()
{
    test::Context control;
    test::Worker first;
    test::Worker second;
    std::atomic_uint entered = 0;
    std::atomic_bool released = false;
    const auto body = [&](int value) {
        entered.fetch_add(1, std::memory_order_release);
        while (not released.load(std::memory_order_acquire)) { std::this_thread::yield(); }
        return value;
    };
    test::Observation observation;
    auto operation = ex::connect(ex::when_all(
        ex::starts_on(first.getScheduler(), ex::just(20) | ex::then(body)),
        ex::starts_on(second.getScheduler(), ex::just(22) | ex::then(body)))
        | ex::continues_on(control.getScheduler()), test::Receiver {&observation});
    ex::start(operation);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (entered.load(std::memory_order_acquire) != 2 and std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
    const bool overlap = entered.load(std::memory_order_acquire) == 2;
    released.store(true, std::memory_order_release);
    test::pump_until(control, [&] { return observation.m_done.load(); });
    EXPECT_TRUE(overlap);
    EXPECT_EQ(observation.m_value_calls, 1u);
    EXPECT_EQ(observation.m_values[0], 20); EXPECT_EQ(observation.m_values[1], 22);
}

}
