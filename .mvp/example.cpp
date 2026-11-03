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

// Only the caller thread starts CPU operations in this example.
using ExampleContext = async::BasicContext<async::BasicExecutor<>,
    async::BasicExecutor<async::FastProducerSpmcInbox>>;

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
    std::thread::id m_control_thread;
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
    constexpr std::size_t k_queue_count = 3;
    const auto main_thread = std::this_thread::get_id();

    async::ContextInfo info{k_worker_count};

    const auto control_token = info.addControlQueue();
    const auto left_token = info.addCpuQueue();
    const auto right_token = info.addCpuQueue();

    constexpr auto k_storage = ExampleContext::storageRequirements(k_worker_count, k_queue_count);
    alignas(k_storage.m_workers.m_alignment)
        std::array<std::byte, k_storage.m_workers.m_bytes> worker_bytes;
    alignas(k_storage.m_routes.m_alignment)
        std::array<std::byte, k_storage.m_routes.m_bytes> route_bytes;
    std::pmr::monotonic_buffer_resource worker_arena{
        worker_bytes.data(), worker_bytes.size(), std::pmr::null_memory_resource()};
    std::pmr::monotonic_buffer_resource route_arena{
        route_bytes.data(), route_bytes.size(), std::pmr::null_memory_resource()};

    ExampleContext context{std::move(info), async::ContextAllocators{
        std::pmr::polymorphic_allocator<std::byte>{&worker_arena},
        std::pmr::polymorphic_allocator<std::byte>{&route_arena}}};

    auto control_scheduler = context.getSchedulerHandle(control_token);
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
            async::LogicalQueue caller_queue{caller_executor};

            auto caller_scheduler = async::SchedulerHandle{caller_queue};
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
                ex::continues_on(control_scheduler) |
                ex::then([](BranchResult lhs, BranchResult rhs) noexcept {
                    // Context's owner thread consumes the merged values.
                    return GraphResult{
                        lhs.m_value + rhs.m_value,
                        lhs.m_worker_thread,
                        rhs.m_worker_thread,
                        std::this_thread::get_id()};
                }) | ex::continues_on(caller_scheduler);

            bool caller_completed = false;
            auto operation = ex::connect(std::move(graph), Receiver{&context, &outcome, &caller_completed, caller_thread});

            ex::start(operation);

            std::size_t heartbeat_count = 0;
            while (not caller_completed) {
                std::cout << "caller heartbeat: " << ++heartbeat_count << '\n';
                caller_executor.pollOne();
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
        && result.m_control_thread == main_thread
        && result.m_left_thread != result.m_right_thread
        && result.m_left_thread != main_thread
        && result.m_right_thread != main_thread
        && result.m_left_thread != caller_thread
        && result.m_right_thread != caller_thread;

    std::cout << "result: " << result.m_value << '\n';
    std::cout << "acceptance: " << (passed ? "passed" : "failed") << '\n';
    return passed ? 0 : 1;
}
