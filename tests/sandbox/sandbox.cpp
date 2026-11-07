#include <async/async.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>


namespace async = lcf::async;
namespace ex = async::exec;
namespace proto = async::protocols;
using namespace std::chrono_literals;

using ExampleContext = async::Context;

class SandboxNotifier
{
public:
    explicit SandboxNotifier(async::details::WorkSignal & signal) noexcept : m_signal_p(&signal) {}
    ~SandboxNotifier() noexcept = default;
    void operator()() const noexcept { m_signal_p->notifyAll(); }
private:
    async::details::WorkSignal * m_signal_p;
};

template <typename Source>
class SandboxVictimSelector
{
    using Selector = async::RoundRobinVictimSelector<Source>;
public:
    using Value = typename Source::Value;
    ~SandboxVictimSelector() noexcept = default;
    SandboxVictimSelector(std::span<Source> sources, std::size_t owner_index) noexcept :
        m_selector(sources, owner_index) {}
    Value * trySteal() noexcept { return m_selector.trySteal(); }
private:
    Selector m_selector;
};

template <typename Inbox, typename ReadyQueue, typename Selector, typename Notifier>
class SandboxCpuExecutor
{
    using Executor = async::WorkStealingExecutor<Inbox, ReadyQueue, Selector, Notifier>;
public:
    ~SandboxCpuExecutor() noexcept = default;
    SandboxCpuExecutor(Inbox & inbox, ReadyQueue & ready, Selector selector, Notifier notify) noexcept :
        m_executor(inbox, ready, std::move(selector), std::move(notify)) {}
    bool executeOne() noexcept { return m_executor.executeOne(); }
private:
    Executor m_executor;
};

void * aligned_storage(std::vector<std::byte> & bytes, std::size_t alignment, std::size_t size_in_bytes) noexcept
{
    void * storage_p = bytes.data();
    auto space = bytes.size();
    return std::align(alignment, size_in_bytes, storage_p, space);
}

bool validates_custom_context_policies()
{
    using PolicyContext = async::BasicContext<std::thread, 64,
        SandboxCpuExecutor, SandboxVictimSelector, SandboxNotifier>;

    async::ContextInfo context_info{0};
    context_info.addScheduler();
    const auto [runtime_size, runtime_alignment, worker_size, worker_alignment] = context_info
        .getStorageRequirements<std::thread, 64, SandboxCpuExecutor, SandboxVictimSelector, SandboxNotifier>();
    std::vector<std::byte> runtime_bytes(runtime_size + runtime_alignment - 1);
    std::pmr::monotonic_buffer_resource runtime_resource{
        aligned_storage(runtime_bytes, runtime_alignment, runtime_size), runtime_size,
        std::pmr::null_memory_resource()};
    std::pmr::monotonic_buffer_resource worker_resource{std::pmr::null_memory_resource()};
    context_info.setMemoryResources(runtime_resource, worker_resource);

    PolicyContext context;
    if (context.create(context_info)) { return false; }
    context.requestStop();
    context.joinWorkers();
    return context.isEmpty() and worker_size == 0 and worker_alignment != 0;
}

struct BranchResult
{
    int m_value;
    std::thread::id m_worker_thread;
};

struct GraphResult
{
    int m_value;
    std::thread::id m_left_thread;
    std::thread::id m_right_thread;
    std::thread::id m_continuation_thread;
};

enum class Completion
{
    ePending,
    eValue,
    eError,
    eStopped,
};

struct Outcome
{
    Completion m_completion = Completion::ePending;
    std::optional<GraphResult> m_result;
    std::thread::id m_caller_thread;
    std::thread::id m_receiver_thread;
};

struct Receiver
{
    using receiver_concept = proto::receiver_t;
public:
    ~Receiver() noexcept = default;
    Receiver() = delete;
    Receiver(ExampleContext * context_p, Outcome * outcome_p, bool * caller_completed_p,
        std::thread::id caller_thread) noexcept
        : m_context_p(context_p), m_outcome_p(outcome_p),
          m_caller_completed_p(caller_completed_p), m_caller_thread(caller_thread) {}
    Receiver(const Receiver &) noexcept = default;
    Receiver(Receiver &&) noexcept = default;
    Receiver & operator=(const Receiver &) noexcept = default;
    Receiver & operator=(Receiver &&) noexcept = default;
public:
    void set_value(GraphResult result) && noexcept
    {
        this->finish(Completion::eValue, std::move(result));
    }

    template <typename Error>
    void set_error(Error &&) && noexcept
    {
        this->finish(Completion::eError);
    }

    void set_stopped() && noexcept
    {
        this->finish(Completion::eStopped);
    }

    ExampleContext * m_context_p;
    Outcome * m_outcome_p;
    bool * m_caller_completed_p;
    std::thread::id m_caller_thread;

private:
    void finish(
        Completion completion,
        std::optional<GraphResult> result = std::nullopt) noexcept
    {
        if (std::this_thread::get_id() != m_caller_thread) {
            std::terminate();
        }

        m_outcome_p->m_completion = completion;
        m_outcome_p->m_result = std::move(result);
        m_outcome_p->m_receiver_thread = std::this_thread::get_id();

        *m_caller_completed_p = true;
        m_context_p->requestStop();
    }
};

int main()
{
    if (not validates_custom_context_policies()) {
        std::cerr << "Custom Context policy setup failed\n";
        return 1;
    }

    constexpr std::uint32_t k_worker_count = 2;
    const auto main_thread = std::this_thread::get_id();

    async::ContextInfo context_info;
    context_info.setWorkerCount(k_worker_count).setDispatcherCount(2);
    const auto caller_port = context_info.addSubmissionPort();

    const auto left_token = context_info.addScheduler();
    const auto right_token = context_info.addScheduler();

    const auto [runtime_size_in_bytes, runtime_alignment, workers_size_in_bytes, workers_alignment] =
        context_info.getStorageRequirements<>();
    std::vector<std::byte> worker_bytes(workers_size_in_bytes + workers_alignment - 1);
    std::vector<std::byte> runtime_bytes(runtime_size_in_bytes + runtime_alignment - 1);
    std::pmr::monotonic_buffer_resource worker_arena{
        aligned_storage(worker_bytes, workers_alignment, workers_size_in_bytes), workers_size_in_bytes, std::pmr::null_memory_resource()};
    std::pmr::monotonic_buffer_resource runtime_arena{
        aligned_storage(runtime_bytes, runtime_alignment, runtime_size_in_bytes), runtime_size_in_bytes, std::pmr::null_memory_resource()};

    context_info.setMemoryResources(runtime_arena, worker_arena);
    ExampleContext context;
    if (const auto error = context.create(context_info)) {
        std::cerr << "Context creation failed: " << error.message() << '\n';
        return 1;
    }

    const auto owner_scheduler = context.getEventLoopSchedulerHandle();

    Outcome outcome;

    std::thread caller;
    try {
        caller = std::thread{[&] {
            const auto caller_thread = std::this_thread::get_id();
            outcome.m_caller_thread = caller_thread;

            auto & submissions = context.getSubmissionPort(caller_port);
            const auto left_scheduler = submissions.getSchedulerHandle(left_token);
            const auto right_scheduler = submissions.getSchedulerHandle(right_token);
            const auto caller_scheduler = context.getReturnSchedulerHandle(caller_port);
            auto left = ex::schedule(left_scheduler) |
                ex::then([] {
                    std::this_thread::sleep_for(1s);
                    return BranchResult{20, std::this_thread::get_id()};
                });
            auto right = ex::schedule(right_scheduler) |
                ex::then([] {
                    std::this_thread::sleep_for(1s);
                    return BranchResult{22, std::this_thread::get_id()};
                });
            auto graph = ex::when_all(std::move(left), std::move(right)) |
                ex::then([](BranchResult lhs, BranchResult rhs) noexcept {
                    return GraphResult{
                        lhs.m_value + rhs.m_value,
                        lhs.m_worker_thread,
                        rhs.m_worker_thread,
                        std::this_thread::get_id()};
                }) | ex::continues_on(owner_scheduler) |
                ex::then([main_thread](GraphResult result) noexcept {
                    if (std::this_thread::get_id() != main_thread) { std::terminate(); }
                    return result;
                }) | ex::continues_on(caller_scheduler);
            bool caller_completed = false;
            auto operation = ex::connect(std::move(graph), Receiver{&context, &outcome, &caller_completed, caller_thread});
            ex::start(operation);
            std::size_t heartbeat_count = 0;
            auto time = std::chrono::steady_clock::now();
            while (not caller_completed) {
                auto elapsed = std::chrono::steady_clock::now() - time;
                std::cout << "heartbeat: " << heartbeat_count++ << " elapsed: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
                context.pollReturn(caller_port);
                if (not caller_completed) {
                    std::this_thread::sleep_for(100ms);
                }
            }
        }};
    } catch (...) {
        context.close();
        context.joinWorkers();
        throw;
    }
    context.run();
    caller.join();
    context.close();
    context.joinWorkers();
    if (outcome.m_completion != Completion::eValue or not outcome.m_result) {
        std::cerr << "work completed with error or stopped\n";
        return 1;
    }
    const auto & result = *outcome.m_result;
    const auto caller_thread = outcome.m_caller_thread;
    const bool passed = result.m_value == 42 and
       caller_thread != main_thread and
       outcome.m_receiver_thread == caller_thread and
       result.m_continuation_thread != caller_thread and
       result.m_left_thread != caller_thread and
       result.m_right_thread != caller_thread;
    std::cout << "result: " << result.m_value << '\n';
    std::cout << "acceptance: " << (passed ? "passed" : "failed") << '\n';
    return passed ? 0 : 1;
}
