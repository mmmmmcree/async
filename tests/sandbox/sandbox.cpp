#include <async/config/execution_provider.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#include "library_under_test.h"


namespace async = lcf::async;
namespace ex = async::exec;
namespace proto = async::protocols;
using namespace std::chrono_literals;

using ExampleContext = async::Context;

void * aligned_storage(std::vector<std::byte> & bytes, std::size_t alignment, std::size_t size_in_bytes) noexcept
{
    void * storage_p = bytes.data();
    auto space = bytes.size();
    return std::align(alignment, size_in_bytes, storage_p, space);
}

struct BranchResult
{
    std::int32_t m_value;
    std::thread::id m_worker_thread;
};

struct GraphResult
{
    std::int32_t m_value;
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
    Receiver(Outcome * outcome_p, bool * caller_completed_p,
        std::thread::id caller_thread) noexcept
        : m_outcome_p(outcome_p),
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
    }
};

template <async::scheduler_c SharedScheduler, async::scheduler_c CallerScheduler,
    async::work_executor_c TargetExecutor, async::notify::notifier_c Notifier>
void run_caller(SharedScheduler shared_scheduler_handle, CallerScheduler caller_scheduler_handle,
    async::ExecutionPort<TargetExecutor, Notifier> caller_execution_port, Outcome & outcome)
{
    const auto caller_thread = std::this_thread::get_id();
    outcome.m_caller_thread = caller_thread;

    auto left = ex::schedule(shared_scheduler_handle) |
        ex::then([] {
            std::this_thread::sleep_for(1s);
            return BranchResult{20, std::this_thread::get_id()};
        });
    auto right = ex::schedule(shared_scheduler_handle) |
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
        }) | ex::then([](GraphResult result) noexcept {
            std::cout << "graph result: " << result.m_value
                << " left execution thread id: " << result.m_left_thread
                << " right execution thread id: " << result.m_right_thread << '\n';
            return result;
        }) | ex::continues_on(caller_scheduler_handle);
    bool caller_completed = false;
    auto operation = ex::connect(std::move(graph), Receiver{&outcome, &caller_completed, caller_thread});
    ex::start(operation);
    std::uint64_t heartbeat_count = 0;
    auto time = std::chrono::steady_clock::now();
    while (not caller_completed) {
        auto elapsed = std::chrono::steady_clock::now() - time;
        std::cout << "heartbeat: " << heartbeat_count++ << " elapsed: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
        caller_execution_port.pollOne();
        if (not caller_completed) {
            std::this_thread::sleep_for(100ms);
        }
    }
}

int main()
{
    constexpr std::uint32_t k_worker_count = 2;
    const auto main_thread = std::this_thread::get_id();

    async::ContextInfo context_info;
    context_info.setWorkerCount(k_worker_count).setDispatcherCount(2);
    const async::PortToken caller_port_token = context_info.addPort();

    const async::SchedulerToken shared_scheduler_token = context_info.addSharedScheduler();

    const auto [runtime_size_in_bytes, runtime_alignment, workers_size_in_bytes, workers_alignment] = context_info.getStorageRequirements<>();
    std::vector<std::byte> worker_bytes(workers_size_in_bytes + workers_alignment - 1);
    std::vector<std::byte> runtime_bytes(runtime_size_in_bytes + runtime_alignment - 1);
    std::pmr::monotonic_buffer_resource worker_arena{
        aligned_storage(worker_bytes, workers_alignment, workers_size_in_bytes), workers_size_in_bytes, std::pmr::null_memory_resource()};
    std::pmr::monotonic_buffer_resource runtime_arena{
        aligned_storage(runtime_bytes, runtime_alignment, runtime_size_in_bytes), runtime_size_in_bytes, std::pmr::null_memory_resource()};

    context_info.setMemoryResources(runtime_arena, worker_arena);
    ExampleContext context;
    if (const auto error = context.create(std::move(context_info))) {
        std::cerr << "Context creation failed: " << error.message() << '\n';
        return 1;
    }

    const auto caller_submission_port = context.getSubmissionPort(caller_port_token);
    const auto shared_scheduler_handle = caller_submission_port.getSchedulerHandle(shared_scheduler_token);
    const auto caller_scheduler_handle = context.getSubmissionPort().getSchedulerHandle(caller_port_token);
    const auto caller_execution_port = context.getExecutionPort(caller_port_token);
    Outcome outcome;

    std::thread caller;
    try {
        caller = std::thread{[&] {
            run_caller(shared_scheduler_handle, caller_scheduler_handle, caller_execution_port, outcome);
            context.requestStopProcessing();
        }};
    } catch (...) {
        context.closeSubmissions();
        throw;
    }
    context.run();
    caller.join();
    context.closeSubmissions();
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
