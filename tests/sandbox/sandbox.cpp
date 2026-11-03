#include <async/async.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>


namespace async = lcf::async;
namespace ex = async::exec;
namespace proto = async::protocols;
using namespace std::chrono_literals;

// Only the caller thread submits operations to this Context.
using WorkQueue = async::queue::intrusive::spmc::Queue<>;
using ExampleContext = async::BasicContext<async::BasicExecutor<WorkQueue>>;

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
    constexpr async::WorkerCount k_worker_count = 2;
    const auto main_thread = std::this_thread::get_id();

    async::ContextInfo info{k_worker_count};

    const auto left_token = info.addScheduler();
    const auto right_token = info.addScheduler();

    constexpr auto k_storage = ExampleContext::storageRequirements(k_worker_count, 2);
    alignas(k_storage.m_executors.m_alignment)
        std::array<std::byte, k_storage.m_executors.m_bytes> executor_bytes;
    alignas(k_storage.m_workers.m_alignment)
        std::array<std::byte, k_storage.m_workers.m_bytes> worker_bytes;
    std::pmr::monotonic_buffer_resource worker_arena{
        worker_bytes.data(), worker_bytes.size(), std::pmr::null_memory_resource()};
    std::pmr::monotonic_buffer_resource executor_arena{
        executor_bytes.data(), executor_bytes.size(), std::pmr::null_memory_resource()};

    ExampleContext context{std::move(info), async::ContextAllocators{
        std::pmr::polymorphic_allocator<std::byte>{&executor_arena},
        std::pmr::polymorphic_allocator<std::byte>{&worker_arena}}};

    auto left_scheduler = context.getSchedulerHandle(left_token);
    auto right_scheduler = context.getSchedulerHandle(right_token);

    Outcome outcome;

    std::thread caller;
    try {
        caller = std::thread{[&] {
            const auto caller_thread = std::this_thread::get_id();
            outcome.m_caller_thread = caller_thread;

            // The caller owns and drives this return executor independently of Context.
            async::BasicExecutor<> caller_executor;
            auto caller_scheduler = async::SchedulerHandle{caller_executor};
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
                }) | ex::continues_on(context.getEventLoopSchedulerHandle())
                | ex::then([main_thread](GraphResult result) noexcept {
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
