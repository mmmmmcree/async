#include "library_under_test.h"
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <memory_resource>
#include <span>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace async = lcf::async;
namespace ex = async::exec;
namespace proto = async::protocols;

class TestSignal
{
public:
    using Observation = async::notify::AtomicSequenceSignal::Observation;
    TestSignal() noexcept = default;
    ~TestSignal() noexcept = default;
    TestSignal(const TestSignal &) = delete;
    TestSignal(TestSignal &&) = delete;
    TestSignal & operator=(const TestSignal &) = delete;
    TestSignal & operator=(TestSignal &&) = delete;
    Observation observe() const noexcept { return m_signal.observe(); }
    void wait(Observation observed) const noexcept { m_signal.wait(observed); }
    void notifyOne() noexcept { m_signal.notifyOne(); }
    void notifyAll() noexcept { m_signal.notifyAll(); }
private:
    async::notify::AtomicSequenceSignal m_signal;
};

template <async::notify::signal_c Signal>
class TestNotifier
{
public:
    explicit TestNotifier(Signal & signal) noexcept : m_signal_p(&signal) {}
    ~TestNotifier() noexcept = default;
    TestNotifier() = delete;
    TestNotifier(const TestNotifier &) noexcept = default;
    TestNotifier(TestNotifier &&) noexcept = default;
    TestNotifier & operator=(const TestNotifier &) noexcept = default;
    TestNotifier & operator=(TestNotifier &&) noexcept = default;
    void notifyOne() const noexcept { m_signal_p->notifyOne(); }
    void notifyAll() const noexcept { m_signal_p->notifyAll(); }
private:
    Signal * m_signal_p;
};

static_assert(async::notify::signal_waiter_c<TestSignal>);
static_assert(async::notify::signal_c<TestSignal>);
static_assert(async::notify::signal_notifier_c<TestNotifier<TestSignal>, TestSignal>);
static_assert(not async::notify::notifier_c<decltype([]() noexcept {})>);

template <typename Source>
class TestVictimSelector
{
    using Selector = async::RoundRobinVictimSelector<Source>;
public:
    using Value = typename Source::Value;
    ~TestVictimSelector() noexcept = default;
    TestVictimSelector() = delete;
    TestVictimSelector(std::span<Source> sources, std::size_t owner_index) noexcept :
        m_selector(sources, owner_index) {}
    TestVictimSelector(const TestVictimSelector &) noexcept = default;
    TestVictimSelector(TestVictimSelector &&) noexcept = default;
    TestVictimSelector & operator=(const TestVictimSelector &) noexcept = default;
    TestVictimSelector & operator=(TestVictimSelector &&) noexcept = default;
    Value * trySteal() noexcept { return m_selector.trySteal(); }
private:
    Selector m_selector;
};

template <typename Inbox, typename ReadyQueue, typename Selector, typename Notifier>
class TestCpuExecutor
{
    using Executor = async::WorkStealingExecutor<Inbox, ReadyQueue, Selector, Notifier>;
public:
    ~TestCpuExecutor() noexcept = default;
    TestCpuExecutor() = delete;
    TestCpuExecutor(Inbox & inbox, ReadyQueue & ready, Selector selector, Notifier notifier) noexcept :
        m_executor(inbox, ready, std::move(selector), std::move(notifier)) {}
    TestCpuExecutor(const TestCpuExecutor &) = default;
    TestCpuExecutor(TestCpuExecutor &&) = default;
    TestCpuExecutor & operator=(const TestCpuExecutor &) = default;
    TestCpuExecutor & operator=(TestCpuExecutor &&) = default;
    bool executeOne() noexcept { return m_executor.executeOne(); }
private:
    Executor m_executor;
};

class PollReceiver
{
public:
    using receiver_concept = proto::receiver_t;
public:
    ~PollReceiver() noexcept = default;
    PollReceiver() = delete;
    explicit PollReceiver(int & result) noexcept : m_result_p(&result) {}
    PollReceiver(const PollReceiver &) noexcept = default;
    PollReceiver(PollReceiver &&) noexcept = default;
    PollReceiver & operator=(const PollReceiver &) noexcept = default;
    PollReceiver & operator=(PollReceiver &&) noexcept = default;
public:
    void set_value(int value) && noexcept { *m_result_p = value; }
    void set_error(std::exception_ptr) && noexcept { *m_result_p = -1; }
    void set_stopped() && noexcept { *m_result_p = -2; }
private:
    int * m_result_p;
};

void * aligned_storage(std::vector<std::byte> & bytes, std::size_t alignment, std::size_t size_in_bytes) noexcept
{
    void * storage_p = bytes.data();
    auto space = bytes.size();
    return std::align(alignment, size_in_bytes, storage_p, space);
}

bool validates_custom_context_policies()
{
    TestSignal signal;
    TestNotifier notifier{signal};
    auto observed = signal.observe();
    notifier.notifyOne();
    if (signal.observe() == observed) { return false; }
    signal.wait(observed);
    observed = signal.observe();
    notifier.notifyAll();
    if (signal.observe() == observed) { return false; }
    signal.wait(observed);

    using PolicyContext = async::BasicContext<std::thread, 64,
        TestCpuExecutor, TestVictimSelector, TestSignal, TestNotifier<TestSignal>>;

    async::ContextInfo context_info{0};
    context_info.addScheduler();
    const auto [runtime_size, runtime_alignment, worker_size, worker_alignment] = context_info
        .getStorageRequirements<std::thread, 64, TestCpuExecutor, TestVictimSelector,
            TestSignal, TestNotifier<TestSignal>>();
    std::vector<std::byte> runtime_bytes(runtime_size + runtime_alignment - 1);
    std::pmr::monotonic_buffer_resource runtime_resource{
        aligned_storage(runtime_bytes, runtime_alignment, runtime_size), runtime_size,
        std::pmr::null_memory_resource()};
    std::pmr::monotonic_buffer_resource worker_resource{std::pmr::null_memory_resource()};
    context_info.setMemoryResources(runtime_resource, worker_resource);

    PolicyContext context;
    if (context.create(std::move(context_info))) { return false; }
    if (context_info.getDomainId() != 0) { return false; }
    context.requestStopProcessing();
    return context.isEmpty() and worker_size == 0 and worker_alignment != 0;
}

bool validates_owner_access_after_launch_failure()
{
    async::Context context;
    bool passed = false;
    std::thread owner{[&] {
        async::ContextInfo context_info{1};
        auto & resource = *std::pmr::new_delete_resource();
        context_info.setMemoryResources(resource, resource);
        if (context.create(std::move(context_info))) { return; }

        const auto failure = std::make_error_code(std::errc::resource_unavailable_try_again);
        try {
            context.run([failure](async::ThreadEntry, void *) -> std::thread {
                throw std::system_error{failure};
            });
            return;
        } catch (const std::system_error & error) {
            if (error.code() != failure) { return; }
        }

        if (context.pollOne()) { return; }
        const auto report = context.poll();
        passed = not report and report.m_records == 0 and not context.pollOne();
    }};
    owner.join();
    return passed;
}

bool validates_public_poll_results()
{
    async::ContextInfo context_info {0};
    const auto cpu_token = context_info.addScheduler();
    const auto port_token = context_info.addSubmissionPort();
    auto & resource = *std::pmr::new_delete_resource();
    context_info.setMemoryResources(resource, resource);
    async::Context context;
    if (context.create(std::move(context_info))) { return false; }

    const auto cpu_work = ex::schedule(context.getSchedulerHandle(cpu_token))
        | ex::then([]() noexcept { return 42; });
    const auto port_work = ex::schedule(context.getPortSchedulerHandle(port_token))
        | ex::then([]() noexcept { return 7; });
    int cpu_one_value = 0;
    int cpu_batch_value = 0;
    int port_value = 0;
    auto cpu_one = ex::connect(cpu_work, PollReceiver {cpu_one_value});
    auto cpu_batch = ex::connect(cpu_work, PollReceiver {cpu_batch_value});
    auto port_operation = ex::connect(port_work, PollReceiver {port_value});

    ex::start(port_operation);
    const auto dispatched = context.poll();
    const auto port_executed = context.pollPortOne(port_token);
    const auto port_idle = context.pollPortOne(port_token);
    ex::start(cpu_one);
    const auto cpu_executed = context.pollOne();
    const auto cpu_idle = context.pollOne();
    ex::start(cpu_batch);
    const auto skipped = context.poll(async::PollBudget {.m_max_records = 0});
    const auto executed = context.poll();
    const auto idle = context.poll();
    context.closeSubmissions();
    context.run();

    if (not dispatched or not executed or skipped or idle) { return false; }
    return dispatched == async::PollResult::eDispatched and dispatched.m_records == 0
        and port_executed == async::PollResult::eExecuted and port_idle == async::PollResult::eIdle
        and cpu_executed == async::PollResult::eExecuted and cpu_idle == async::PollResult::eIdle
        and executed == async::PollResult::eExecuted and executed.m_records == 1
        and skipped == async::PollResult::eIdle and skipped.m_count_limit_reached
        and idle == async::PollResult::eIdle and idle.m_records == 0
        and cpu_one_value == 42 and cpu_batch_value == 42 and port_value == 7;
}

TEST(Context, SupportsCustomPoliciesAndExactStorage)
{
    EXPECT_TRUE(validates_custom_context_policies());
}

TEST(Context, ReleasesOwnerAccessAfterLaunchFailure)
{
    EXPECT_TRUE(validates_owner_access_after_launch_failure());
}

TEST(Context, ReportsPublicPollingResults)
{
    EXPECT_TRUE(validates_public_poll_results());
}

TEST(Context, JoinsWorkersBeforeRunReturns)
{
    constexpr std::size_t k_worker_count = 2;
    async::ContextInfo info {k_worker_count};
    auto & resource = *std::pmr::new_delete_resource();
    info.setMemoryResources(resource, resource);
    async::Context context;
    ASSERT_FALSE(context.create(std::move(info)));
    context.closeSubmissions();
    std::atomic<std::size_t> worker_exits {0};
    context.run([&](async::ThreadEntry entry, void * state_p) {
        return std::thread {[&, entry, state_p] {
            entry(state_p);
            worker_exits.fetch_add(1, std::memory_order_relaxed);
        }};
    });
    EXPECT_EQ(worker_exits.load(std::memory_order_relaxed), k_worker_count);
}

} // namespace
