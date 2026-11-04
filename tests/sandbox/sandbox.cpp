#include <async/async.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>


namespace async = lcf::async;
namespace ex = async::exec;
namespace proto = async::protocols;
using namespace std::chrono_literals;

// Only the caller thread submits operations to this Context.
using WorkQueue = async::queue::intrusive::spmc::Queue<>;
using ExampleContext = async::BasicContext<async::BasicExecutor<WorkQueue>>;

void * aligned_storage(std::vector<std::byte> & bytes, async::StorageRequirement requirement) noexcept
{
    void * storage_p = bytes.data();
    auto space = bytes.size();
    return std::align(requirement.m_alignment, requirement.m_bytes, storage_p, space);
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
        // All three completion channels must run on the caller thread.
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
    constexpr std::uint32_t k_worker_count = 2;
    const auto main_thread = std::this_thread::get_id();

    async::ContextInfo context_info;
    context_info.setWorkerCount(k_worker_count);

    const auto left_token = context_info.addScheduler();
    const auto right_token = context_info.addScheduler();

    const auto storage = context_info.storageRequirements<async::BasicExecutor<WorkQueue>>();
    std::vector<std::byte> worker_bytes(storage.m_workers.m_bytes + storage.m_workers.m_alignment - 1);
    std::vector<std::byte> executor_bytes(storage.m_executors.m_bytes + storage.m_executors.m_alignment - 1);
    std::pmr::monotonic_buffer_resource worker_arena{
        aligned_storage(worker_bytes, storage.m_workers), storage.m_workers.m_bytes, std::pmr::null_memory_resource()};
    std::pmr::monotonic_buffer_resource executor_arena{
        aligned_storage(executor_bytes, storage.m_executors), storage.m_executors.m_bytes, std::pmr::null_memory_resource()};

    context_info.setAllocators(async::ContextAllocators{
        std::pmr::polymorphic_allocator<std::byte>{&executor_arena},
        std::pmr::polymorphic_allocator<std::byte>{&worker_arena}});
    ExampleContext context;
    if (const auto error = context.create(context_info)) {
        std::cerr << "Context creation failed: " << error.message() << '\n';
        return 1;
    }

    // Logical targets are routed by Context's Dispatcher, independently of Workers.
    const auto left_scheduler = context.getSchedulerHandle(left_token);
    const auto right_scheduler = context.getSchedulerHandle(right_token);
    const auto owner_scheduler = context.getEventLoopSchedulerHandle();

    Outcome outcome;

    std::thread caller;
    try {
        caller = std::thread{[&] {
            const auto caller_thread = std::this_thread::get_id();
            outcome.m_caller_thread = caller_thread;

            // This Queue/Executor pair is a standalone return target; Context does not drive it.
            async::BasicExecutor<> caller_executor;
            const auto caller_scheduler = async::SchedulerHandle{caller_executor};
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
                    // The completing execution agent consumes the merged values.
                    return GraphResult{
                        lhs.m_value + rhs.m_value,
                        lhs.m_worker_thread,
                        rhs.m_worker_thread,
                        std::this_thread::get_id()};
                }) | ex::continues_on(owner_scheduler)
                | ex::then([main_thread](GraphResult result) noexcept {
                    if (std::this_thread::get_id() != main_thread) { std::terminate(); }
                    return result;
                }) | ex::continues_on(caller_scheduler);

            bool caller_completed = false;
            auto operation = ex::connect(std::move(graph), Receiver{&context, &outcome, &caller_completed, caller_thread});

            // Start the graph; its schedule operations submit to the Dispatcher.
            ex::start(operation);

            std::size_t heartbeat_count = 0;
            auto time = std::chrono::steady_clock::now();
            while (not caller_completed) {
                auto elapsed = std::chrono::steady_clock::now() - time;
                std::cout << "heartbeat: " << heartbeat_count++ << " elapsed: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
                caller_executor.executeOne();
                if (not caller_completed) {
                    std::this_thread::sleep_for(100ms);
                }
            }
            caller_executor.close();
        }};
    } catch (...) {
        // No root was submitted if launching the caller thread failed.
        context.close();
        context.joinWorkers();
        throw;
    }

    // Wait for work internally; the final Receiver requests this run to stop.
    context.run();

    caller.join();
    context.close();
    context.joinWorkers();

    if (outcome.m_completion != Completion::eValue
        || not outcome.m_result) {
        std::cerr << "work completed with error or stopped\n";
        return 1;
    }

    const auto & result = *outcome.m_result;
    const auto caller_thread = outcome.m_caller_thread;

    const bool passed =
        result.m_value == 42
        && caller_thread != main_thread
        && outcome.m_receiver_thread == caller_thread
        && result.m_continuation_thread != caller_thread
        && result.m_left_thread != caller_thread
        && result.m_right_thread != caller_thread;

    std::cout << "result: " << result.m_value << '\n';
    std::cout << "acceptance: " << (passed ? "passed" : "failed") << '\n';
    return passed ? 0 : 1;
}
