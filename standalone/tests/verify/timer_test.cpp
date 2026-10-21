#include <NativeApi.h>
#include <gtest/gtest.h>
#include <array>
#include <barrier>
#include <future>
#include <functional>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <vector>

namespace {

namespace async = lcf::async;
namespace ex = async::exec;
using namespace std::chrono_literals;

/* Objective: timers belong to one context and delivery requires its pump.
 * Input: two contexts and a zero-duration timer on the first.
 * Procedure: poll the second, then admit one callback on the first.
 * Expected: one value on the polling thread, without running a timer worker.
 * Failure hints: inspect Queue::pop and TimerRegistration::invoke. */
void timer_is_context_local();
TEST(Timer, ContextLocalAndPollDriven) { timer_is_context_local(); }

/* Objective: expired timers obey the same count/time admission budget as posts.
 * Input: 64 expired timers and zero, three-callback and unlimited budgets.
 * Procedure: count ready entries, then advance each budget.
 * Expected: readiness counting never delivers work; one poll admits three timers.
 * Failure hints: inspect TimerQueue::readyCount and TimerService::poll's single removal. */
void expired_timers_obey_budget();
TEST(Timer, ExpiredTimersObeyBudget) { expired_timers_obey_budget(); }

/* Objective: timer checks and continuously reposted callbacks cannot starve each other.
 * Input: a busy FIFO followed by an expired timer, then a timer backlog and a post.
 * Procedure: admit one callback at a time across the internal timer marker.
 * Expected: the timer passes the original FIFO boundary; ordinary work also advances.
 * Failure hints: inspect Queue::queueEventCheck and marker reinsertion. */
void timer_and_callbacks_make_progress();
TEST(Timer, TimerAndCallbacksMakeProgress) { timer_and_callbacks_make_progress(); }

/* Objective: removing any pending timer preserves heap indices and cancellation.
 * Input: 128 future deadlines and independent stop sources.
 * Procedure: cancel every entry in a non-sorted permutation, then poll.
 * Expected: exactly 128 stopped completions and an empty ready queue.
 * Failure hints: inspect TimerQueue::remove/siftUp/siftDown. */
void cancellation_removes_arbitrary_heap_entries();
TEST(Timer, CancellationRemovesArbitraryHeapEntries) { cancellation_removes_arbitrary_heap_entries(); }

/* Objective: runUntilIdle waits for timers and wakes for an earlier insertion.
 * Input: a one-hour timer, a worker, and an explicitly gated immediate timer.
 * Procedure: observe worker entry, insert the earlier timer, cancel the long one.
 * Expected: the short timer finishes on the worker; outstanding work then drains.
 * Failure hints: inspect Queue wait_until notifications and timer work accounting. */
void earlier_deadline_wakes_worker();
TEST(Timer, EarlierDeadlineWakesWorker) { earlier_deadline_wakes_worker(); }

/* Objective: stop/restart pauses timer delivery without permanently closing it.
 * Input: one expired timer and one subsequently cancelled future timer.
 * Procedure: stop, poll, restart and poll for each terminal path.
 * Expected: pending work survives stop and completes once after restart.
 * Failure hints: distinguish Queue::stop from Queue::shutdown. */
void stop_restart_preserves_timers();
TEST(Timer, StopRestartPreservesTimers) { stop_restart_preserves_timers(); }

/* Objective: permanent shutdown releases owned pending/ready records without callbacks.
 * Input: a timer receiver and posted function each owning a tracked object.
 * Procedure: shut down with a pending timer or a queued cancellation; restart.
 * Expected: tracked ownership expires, user callbacks stay silent, closure persists.
 * Failure hints: inspect callback abandonment outside the queue mutex. */
void shutdown_releases_timer_owners();
TEST(Timer, ShutdownReleasesTimerOwners) { shutdown_releases_timer_owners(); }

/* Objective: the context's resource owns timer heap storage through destruction.
 * Input: a tracking resource and 128 pending timers.
 * Procedure: register timers, forbid allocation, shut down and destroy the context.
 * Expected: shutdown does not allocate; destruction returns all resource storage.
 * Failure hints: inspect TimerQueue's allocator and allocation-free release chain. */
void heap_uses_context_resource();
TEST(Timer, HeapUsesContextResource) { heap_uses_context_resource(); }

/* Objective: failed heap allocation completes through the error channel.
 * Input: a context with a failing heap resource, followed by a successful timer.
 * Procedure: start and poll the failure, enable allocations, then start and poll again.
 * Expected: one bad_alloc completion, released receiver ownership, and a usable context.
 * Failure hints: inspect TimerService::submit's retained owner and error enqueue. */
void heap_allocation_failure_completes_with_error();
TEST(Timer, HeapAllocationFailureCompletesWithError) { heap_allocation_failure_completes_with_error(); }

/* Objective: delay discovers timed scheduling through the receiver environment.
 * Input: sync_wait, an empty environment, and a callback-only executor.
 * Procedure: start delays through each environment and post ordinary work.
 * Expected: sync_wait succeeds; unsupported environments report logic_error.
 * Failure hints: inspect DelaySender::connect and optional executor capabilities. */
void delay_uses_environment();
TEST(Timer, DelayUsesEnvironment) { delay_uses_environment(); }

/* Objective: cancellation and deadline selection choose one terminal delivery.
 * Input: expired timers and a barrier-controlled concurrent stop request.
 * Procedure: race one poll against cancellation, join, then drain.
 * Expected: exactly one value or stopped completion per iteration.
 * Failure hints: inspect m_decided under Queue's mutex and stop registration. */
void cancel_races_expiry();
TEST(Timer, CancelRacesExpiry) { cancel_races_expiry(); }

/* Objective: context shutdown and a concurrent stop request release one owner.
 * Input: future timers with receiver-owned markers and a barrier-controlled stop.
 * Procedure: race shutdown against cancellation, join, and inspect ownership.
 * Expected: no value/error, at most one stopped delivery, and no retained marker.
 * Failure hints: inspect the shared transaction decision and teardown outside it. */
void cancel_races_shutdown();
TEST(Timer, CancelRacesShutdown) { cancel_races_shutdown(); }

struct Outcome
{
    unsigned m_values = 0;
    unsigned m_stopped = 0;
    unsigned m_errors = 0;
    std::thread::id m_thread;
    std::exception_ptr m_exception;
    std::promise<void> * m_done_p = nullptr;
};

struct Receiver
{
    using receiver_concept = ex::receiver_t;
public:
    void set_value() noexcept { ++m_outcome.m_values; this->notify(); }
    void set_stopped() noexcept { ++m_outcome.m_stopped; this->notify(); }
    void set_error(std::exception_ptr error) noexcept { ++m_outcome.m_errors; m_outcome.m_exception = error; this->notify(); }
private:
    void notify() noexcept
    {
        m_outcome.m_thread = std::this_thread::get_id();
        if (m_outcome.m_done_p and m_outcome.m_values + m_outcome.m_stopped + m_outcome.m_errors == 1) { m_outcome.m_done_p->set_value(); }
    }
public:
    Outcome & m_outcome;
    std::shared_ptr<int> m_marker_sp;
};

using Source = decltype(ex::with_stop_token(async::schedule_after(std::declval<async::Scheduler>(), 1h), std::stop_token {}));
using Operation = ex::connect_result_t<Source, Receiver>;
using Operations = std::vector<std::unique_ptr<Operation>>;

class TrackingResource : public std::pmr::memory_resource
{
public:
    void setFailing(bool failing) noexcept { m_failing = failing; }
    std::size_t getAllocations() const noexcept { return m_allocations; }
    std::size_t getDeallocations() const noexcept { return m_deallocations; }
    std::size_t getOutstandingBytes() const noexcept { return m_outstanding_bytes; }
private:
    void * do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (m_failing) { throw std::bad_alloc {}; }
        auto * allocation_p = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocations;
        m_outstanding_bytes += bytes;
        return allocation_p;
    }
    void do_deallocate(void * allocation_p, std::size_t bytes, std::size_t alignment) override
    {
        ++m_deallocations;
        m_outstanding_bytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(allocation_p, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource & other) const noexcept override { return this == &other; }
private:
    std::size_t m_allocations = 0;
    std::size_t m_deallocations = 0;
    std::size_t m_outstanding_bytes = 0;
    bool m_failing = false;
};

void timer_is_context_local()
{
    async::Context first;
    async::Context second;
    Outcome outcome;
    auto operation = ex::connect(async::schedule_after(first.getScheduler(), 0ns), Receiver {outcome});
    ex::start(operation);
    EXPECT_EQ(first.getReadyCount(), 1u);
    EXPECT_EQ(second.pollOne(), 0u);
    EXPECT_EQ(outcome.m_values, 0u);
    EXPECT_EQ(first.pollOne(), 1u);
    EXPECT_EQ(outcome.m_values, 1u);
    EXPECT_EQ(outcome.m_thread, std::this_thread::get_id());
    EXPECT_EQ(first.pollOne(), 0u);
}

void expired_timers_obey_budget()
{
    async::Context context;
    Outcome outcome;
    Operations operations;
    for (unsigned index = 0; index < 64; ++index) {
        auto source = ex::with_stop_token(async::schedule_after(context.getScheduler(), 0ns), std::stop_token {});
        operations.emplace_back(new Operation(ex::connect(std::move(source), Receiver {outcome})));
        ex::start(*operations.back());
    }
    EXPECT_EQ(context.getReadyCount(), 64u);
    EXPECT_EQ(outcome.m_values, 0u);
    EXPECT_EQ(context.poll({0}).m_callbacks, 0u);
    EXPECT_EQ(context.poll({64, 0ns}).m_callbacks, 0u);
    EXPECT_EQ(context.poll({3}).m_callbacks, 3u);
    EXPECT_EQ(outcome.m_values, 3u);
    EXPECT_EQ(context.getReadyCount(), 61u);
    EXPECT_EQ(context.poll().m_callbacks, 61u);
    EXPECT_EQ(outcome.m_values, 64u);
    EXPECT_EQ(outcome.m_errors + outcome.m_stopped, 0u);
}

void timer_and_callbacks_make_progress()
{
    async::Context context;
    Outcome outcome;
    bool repost = true;
    unsigned calls = 0;
    std::function<void()> callback = [&] {
        ++calls;
        if (repost) { async::post(context.getExecutor(), callback); }
    };
    for (unsigned index = 0; index < 32; ++index) { async::post(context.getExecutor(), callback); }
    auto timer = ex::connect(async::schedule_after(context.getScheduler(), 0ns), Receiver {outcome});
    ex::start(timer);
    EXPECT_EQ(context.getReadyCount(), 33u);
    EXPECT_EQ(context.poll({32}).m_callbacks, 32u);
    EXPECT_EQ(calls, 32u);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(outcome.m_values, 1u);
    repost = false;
    EXPECT_EQ(context.poll().m_callbacks, 32u);

    Operations operations;
    for (unsigned index = 0; index < 64; ++index) {
        auto source = ex::with_stop_token(async::schedule_after(context.getScheduler(), 0ns), std::stop_token {});
        operations.emplace_back(new Operation(ex::connect(std::move(source), Receiver {outcome})));
        ex::start(*operations.back());
    }
    async::post(context.getExecutor(), callback);
    EXPECT_EQ(context.poll({2}).m_callbacks, 2u);
    EXPECT_EQ(calls, 65u);
    EXPECT_EQ(outcome.m_values, 2u);
    EXPECT_EQ(context.poll().m_callbacks, 63u);
}

void cancellation_removes_arbitrary_heap_entries()
{
    async::Context context;
    Outcome outcome;
    std::array<std::stop_source, 128> stops;
    Operations operations;
    for (unsigned index = 0; index < stops.size(); ++index) {
        auto source = ex::with_stop_token(async::schedule_after(context.getScheduler(), 1h + std::chrono::seconds {(index * 53) % 128}), stops[index].get_token());
        operations.emplace_back(new Operation(ex::connect(std::move(source), Receiver {outcome})));
        ex::start(*operations.back());
    }
    EXPECT_EQ(context.getReadyCount(), 0u);
    for (unsigned index = 0; index < stops.size(); ++index) { EXPECT_TRUE(stops[(index * 37) % 128].request_stop()); }
    EXPECT_EQ(context.getReadyCount(), 128u);
    EXPECT_EQ(context.poll().m_callbacks, 128u);
    EXPECT_EQ(outcome.m_stopped, 128u);
    EXPECT_EQ(outcome.m_values + outcome.m_errors, 0u);
    EXPECT_EQ(context.getReadyCount(), 0u);
}

void earlier_deadline_wakes_worker()
{
    async::Context context;
    std::promise<void> entered;
    std::promise<void> short_done;
    std::promise<void> exited;
    auto entered_future = entered.get_future();
    auto short_future = short_done.get_future();
    auto exit_future = exited.get_future();
    Outcome long_outcome;
    Outcome short_outcome;
    short_outcome.m_done_p = &short_done;
    std::stop_source stop;
    auto long_operation = ex::connect(ex::with_stop_token(async::schedule_after(context.getScheduler(), 1h), stop.get_token()), Receiver {long_outcome});
    auto short_operation = ex::connect(async::schedule_after(context.getScheduler(), 0ns), Receiver {short_outcome});
    ex::start(long_operation);
    async::post(context.getExecutor(), [&] { entered.set_value(); });
    std::jthread worker {[&](std::stop_token token) {
        std::stop_callback shutdown {token, [&] { context.stop(); }};
        context.runUntilIdle();
        exited.set_value();
    }};
    ASSERT_EQ(entered_future.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(exit_future.wait_for(0s), std::future_status::timeout);
    ex::start(short_operation);
    const auto short_status = short_future.wait_for(5s);
    stop.request_stop();
    const auto exit_status = exit_future.wait_for(5s);
    if (exit_status != std::future_status::ready) { worker.request_stop(); }
    worker.join();
    EXPECT_EQ(short_status, std::future_status::ready);
    EXPECT_EQ(exit_status, std::future_status::ready);
    EXPECT_EQ(short_outcome.m_values, 1u);
    EXPECT_EQ(long_outcome.m_stopped, 1u);
    EXPECT_NE(short_outcome.m_thread, std::this_thread::get_id());
}

void stop_restart_preserves_timers()
{
    for (const bool cancel : {false, true}) {
        SCOPED_TRACE(cancel ? "cancellation while stopped" : "expired while stopped");
        async::Context context;
        Outcome outcome;
        std::stop_source stop;
        auto operation = ex::connect(ex::with_stop_token(async::schedule_after(context.getScheduler(), cancel ? 1h : 0h), stop.get_token()), Receiver {outcome});
        ex::start(operation);
        context.stop();
        if (cancel) { stop.request_stop(); }
        EXPECT_EQ(context.pollOne(), 0u);
        EXPECT_EQ(outcome.m_values + outcome.m_stopped, 0u);
        context.restart();
        EXPECT_EQ(context.pollOne(), 1u);
        EXPECT_EQ(outcome.m_values, cancel ? 0u : 1u);
        EXPECT_EQ(outcome.m_stopped, cancel ? 1u : 0u);
    }
}

void shutdown_releases_timer_owners()
{
    for (const bool cancel : {false, true}) {
        SCOPED_TRACE(cancel ? "queued cancellation" : "pending timer");
        async::Context context;
        Outcome outcome;
        std::stop_source stop;
        auto timer_sp = std::make_shared<int>(1);
        auto posted_sp = std::make_shared<int>(2);
        const std::weak_ptr<int> timer_wp = timer_sp;
        const std::weak_ptr<int> posted_wp = posted_sp;
        auto scheduler = context.getScheduler();
        auto operation = ex::connect(ex::with_stop_token(async::schedule_after(scheduler, 1h), stop.get_token()), Receiver {outcome, std::move(timer_sp)});
        async::post(context.getExecutor(), [marker_sp = std::move(posted_sp), &outcome] { ++outcome.m_values; });
        ex::start(operation);
        if (cancel) { stop.request_stop(); }
        context.shutdown();
        EXPECT_TRUE(timer_wp.expired());
        EXPECT_TRUE(posted_wp.expired());
        EXPECT_EQ(outcome.m_values + outcome.m_stopped + outcome.m_errors, 0u);
        context.restart();
        EXPECT_EQ(context.pollOne(), 0u);
        auto rejected = ex::connect(async::schedule_after(scheduler, 0ns), Receiver {outcome});
        ex::start(rejected);
        EXPECT_EQ(outcome.m_stopped, 1u);
    }
}

void heap_uses_context_resource()
{
    TrackingResource resource;
    {
        async::Context context {&resource};
        Outcome outcome;
        Operations operations;
        for (unsigned index = 0; index < 128; ++index) {
            auto source = ex::with_stop_token(async::schedule_after(context.getScheduler(), 1h), std::stop_token {});
            operations.emplace_back(new Operation(ex::connect(std::move(source), Receiver {outcome})));
            ex::start(*operations.back());
        }
        EXPECT_GT(resource.getAllocations(), 0u);
        EXPECT_GT(resource.getOutstandingBytes(), 0u);
        EXPECT_EQ(context.getReadyCount(), 0u);
        const auto allocations = resource.getAllocations();
        resource.setFailing(true);
        context.shutdown();
        EXPECT_EQ(resource.getAllocations(), allocations);
        EXPECT_EQ(outcome.m_values + outcome.m_stopped + outcome.m_errors, 0u);
    }
    EXPECT_EQ(resource.getOutstandingBytes(), 0u);
    EXPECT_EQ(resource.getDeallocations(), resource.getAllocations());
}

void heap_allocation_failure_completes_with_error()
{
    TrackingResource resource;
    async::Context context {&resource};
    const auto initial_bytes = resource.getOutstandingBytes();
    resource.setFailing(true);
    Outcome failed;
    auto marker_sp = std::make_shared<int>(1);
    const std::weak_ptr<int> marker_wp = marker_sp;
    auto rejected = ex::connect(async::schedule_after(context.getScheduler(), 0ns), Receiver {failed, std::move(marker_sp)});
    ex::start(rejected);
    EXPECT_EQ(failed.m_errors, 0u);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(failed.m_errors, 1u);
    EXPECT_EQ(failed.m_values + failed.m_stopped, 0u);
    ASSERT_TRUE(failed.m_exception);
    EXPECT_THROW(std::rethrow_exception(failed.m_exception), std::bad_alloc);
    EXPECT_TRUE(marker_wp.expired());
    EXPECT_EQ(context.pollOne(), 0u);
    EXPECT_EQ(resource.getOutstandingBytes(), initial_bytes);

    resource.setFailing(false);
    Outcome recovered;
    auto accepted = ex::connect(async::schedule_after(context.getScheduler(), 0ns), Receiver {recovered});
    ex::start(accepted);
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(recovered.m_values, 1u);
    EXPECT_EQ(recovered.m_errors + recovered.m_stopped, 0u);
    resource.setFailing(true);
    context.shutdown();
}

struct CallbackOnly
{
    bool operator==(const CallbackOnly &) const noexcept = default;
    template <typename Callback>
    void enqueue(Callback * callback_p) const noexcept { m_scheduler.enqueue(callback_p); }
    void workStarted() const noexcept { m_scheduler.workStarted(); }
    void workFinished() const noexcept { m_scheduler.workFinished(); }
    async::Scheduler m_scheduler;
};

struct EnvironmentReceiver
{
    using receiver_concept = ex::receiver_t;
    auto get_env() const noexcept { return ex::prop {ex::get_scheduler, m_executor}; }
    void set_value() noexcept { m_receiver.set_value(); }
    void set_stopped() noexcept { m_receiver.set_stopped(); }
    void set_error(std::exception_ptr error) noexcept { m_receiver.set_error(error); }
    Receiver m_receiver;
    async::Executor m_executor;
};

void delay_uses_environment()
{
    EXPECT_TRUE(ex::sync_wait(async::delay(0ns)));
    Outcome missing;
    auto no_environment = ex::connect(async::delay(0ns), Receiver {missing});
    EXPECT_EQ(missing.m_errors, 0u);
    ex::start(no_environment);
    EXPECT_EQ(missing.m_errors, 1u);
    ASSERT_TRUE(missing.m_exception);
    EXPECT_THROW(std::rethrow_exception(missing.m_exception), std::logic_error);

    async::Context context;
    async::Executor executor {CallbackOnly {context.getScheduler()}};
    EXPECT_FALSE(static_cast<bool>(executor.getEventSource()));
    unsigned calls = 0;
    async::post(executor, [&] { ++calls; });
    EXPECT_EQ(context.pollOne(), 1u);
    EXPECT_EQ(calls, 1u);
    Outcome unsupported;
    auto callback_only = ex::connect(async::delay(0ns), EnvironmentReceiver {Receiver {unsupported}, executor});
    ex::start(callback_only);
    EXPECT_EQ(unsupported.m_errors, 1u);
    ASSERT_TRUE(unsupported.m_exception);
    EXPECT_THROW(std::rethrow_exception(unsupported.m_exception), std::logic_error);
}

void cancel_races_expiry()
{
    for (unsigned index = 0; index < 128; ++index) {
        SCOPED_TRACE(index);
        async::Context context;
        Outcome outcome;
        std::stop_source stop;
        auto operation = ex::connect(ex::with_stop_token(async::schedule_after(context.getScheduler(), 0ns), stop.get_token()), Receiver {outcome});
        ex::start(operation);
        std::barrier gate {2};
        std::jthread stopper {[&] { gate.arrive_and_wait(); stop.request_stop(); }};
        gate.arrive_and_wait();
        context.pollOne();
        stopper.join();
        context.poll();
        EXPECT_EQ(outcome.m_values + outcome.m_stopped, 1u);
        EXPECT_EQ(outcome.m_errors, 0u);
    }
}

void cancel_races_shutdown()
{
    for (unsigned index = 0; index < 128; ++index) {
        SCOPED_TRACE(index);
        async::Context context;
        Outcome outcome;
        std::stop_source stop;
        auto marker_sp = std::make_shared<int>(1);
        const std::weak_ptr<int> marker_wp = marker_sp;
        auto operation = ex::connect(ex::with_stop_token(async::schedule_after(context.getScheduler(), 1h), stop.get_token()),
            Receiver {outcome, std::move(marker_sp)});
        ex::start(operation);
        std::barrier gate {2};
        std::jthread stopper {[&] { gate.arrive_and_wait(); stop.request_stop(); }};
        gate.arrive_and_wait();
        context.shutdown();
        stopper.join();
        EXPECT_EQ(outcome.m_values + outcome.m_errors, 0u);
        EXPECT_LE(outcome.m_stopped, 1u);
        EXPECT_TRUE(marker_wp.expired());
        EXPECT_EQ(context.pollOne(), 0u);
    }
}

}
