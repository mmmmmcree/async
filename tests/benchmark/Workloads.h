#pragma once

#include <Runtime.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace lcf::async::comparison {

namespace ex = lcf::async::exec;
using Clock = std::chrono::steady_clock;
enum class Workload { ePost, eChain, eFanout, eObservers, eTimerReady, eTimerWait, eInlineAwait, eYieldAwait, eWorkerChain };
struct Case
{
    std::string_view m_name;
    Workload m_workload;
    unsigned m_size;
};
inline constexpr Case k_cases[] {
    {"post", Workload::ePost, 256},
    {"scheduled_chain", Workload::eChain, 16},
    {"shared_fanout", Workload::eFanout, 256},
    {"cached_observers", Workload::eObservers, 256},
    {"timer_ready", Workload::eTimerReady, 256},
    {"timer_wait", Workload::eTimerWait, 256},
    {"inline_await", Workload::eInlineAwait, 256},
    {"yield_await", Workload::eYieldAwait, 256},
    {"worker_scheduled_chain", Workload::eWorkerChain, 16}};
struct Samples
{
    std::vector<double> m_poll_us;
    std::vector<double> m_timer_lateness_us;
    std::uint64_t m_callbacks = 0;
    unsigned m_max_steps_per_poll = 0;
};
inline double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) { return 0; }
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>((values.size() - 1) * fraction)];
}
inline Awaitable<void> ready_step() { co_return; }
inline Awaitable<void> await_steps(unsigned size, bool yield, std::atomic_uint & steps)
{
    for (unsigned index = 0; index < size; ++index) {
        if (yield) { co_await lcf::async::yield(); }
        else { co_await ready_step(); }
        ++steps;
    }
}
struct Receiver
{
    using receiver_concept = ex::receiver_t;
    template <typename... Values>
    void set_value(Values &&...) noexcept { ++*m_completions_p; }
    template <typename Error>
    void set_error(Error && error) noexcept
    {
        try {
            if constexpr (std::same_as<std::remove_cvref_t<Error>, std::exception_ptr>) { *m_error_p = error; }
            else { throw std::forward<Error>(error); }
        } catch (...) { *m_error_p = std::current_exception(); }
        ++*m_completions_p;
    }
    void set_stopped() noexcept { *m_error_p = std::make_exception_ptr(std::runtime_error("unexpected stopped benchmark")); ++*m_completions_p; }
    unsigned * m_completions_p;
    std::exception_ptr * m_error_p;
};
struct Increment
{
    int operator()(int value) const noexcept { ++*m_steps_p; return value + 1; }
    std::atomic_uint * m_steps_p;
};
template <unsigned Remaining, typename Source, typename Scheduler>
auto scheduled_steps(Source source, Scheduler scheduler, std::atomic_uint & steps)
{
    if constexpr (Remaining == 0) { return source; }
    else {
        return scheduled_steps<Remaining - 1>(std::move(source) | ex::continues_on(scheduler)
            | ex::then(Increment {&steps}), scheduler, steps);
    }
}
inline void run_case(Case test_case, Samples * samples_p = nullptr)
{
    testing::Context control;
    std::unique_ptr<testing::Worker> worker_up;
    if (test_case.m_workload == Workload::eWorkerChain) { worker_up = std::make_unique<testing::Worker>(); }
    const auto scheduler = control.getScheduler();
    std::atomic_uint steps = 0;
    unsigned completions = 0;
    unsigned expected_completions = 1;
    std::exception_ptr error;
    using OperationPtr = std::unique_ptr<void, void (*)(void *)>;
    using Operations = std::vector<OperationPtr>;
    Operations operations;
    operations.reserve(test_case.m_size + 1);
    auto launch = [&]<typename Sender>(Sender sender) {
        using Operation = ex::connect_result_t<Sender, Receiver>;
        auto * operation_p = new Operation(ex::connect(std::move(sender), Receiver {&completions, &error}));
        operations.emplace_back(operation_p, [](void * memory_p) { delete static_cast<Operation *>(memory_p); });
        ex::start(*operation_p);
    };
    if (test_case.m_workload == Workload::ePost) {
        expected_completions = test_case.m_size;
        for (unsigned index = 0; index < test_case.m_size; ++index) {
            testing::post(control, [&] { ++steps; ++completions; });
        }
    } else if (test_case.m_workload == Workload::eChain or test_case.m_workload == Workload::eWorkerChain) {
        const auto task_scheduler = test_case.m_workload == Workload::eWorkerChain ? worker_up->getScheduler() : scheduler;
        launch(scheduled_steps<16>(ex::just(0), task_scheduler, steps) | ex::continues_on(scheduler)
            | ex::then([](int value) { if (value != 16) { throw std::runtime_error("incorrect scheduled chain"); } }));
    } else if (test_case.m_workload == Workload::eFanout or test_case.m_workload == Workload::eObservers) {
        auto shared = ex::split(ex::starts_on(scheduler, ex::just(42)));
        expected_completions = test_case.m_size;
        if (test_case.m_workload == Workload::eObservers) {
            launch(shared);
            testing::pump_until(control, [&] { return completions == 1; });
            ++expected_completions;
        }
        for (unsigned index = 0; index < test_case.m_size; ++index) {
            launch(shared | ex::continues_on(scheduler) | ex::then([&](const int & value) {
                if (value != 42) { throw std::runtime_error("incorrect shared value"); }
                ++steps;
            }));
        }
    } else if (test_case.m_workload == Workload::eTimerReady or test_case.m_workload == Workload::eTimerWait) {
        expected_completions = test_case.m_size;
        const auto duration = test_case.m_workload == Workload::eTimerWait ? std::chrono::milliseconds {1} : std::chrono::milliseconds {0};
        for (unsigned index = 0; index < test_case.m_size; ++index) {
            const auto due = Clock::now() + duration;
            launch(lcf::async::schedule_after(scheduler, duration) | ex::then([&, due] {
                ++steps;
                if (samples_p) { samples_p->m_timer_lateness_us.push_back(std::chrono::duration<double, std::micro>(Clock::now() - due).count()); }
            }));
        }
    } else {
        launch(lcf::async::as_sender(scheduler, await_steps(test_case.m_size, test_case.m_workload == Workload::eYieldAwait, steps)));
    }
    const auto deadline = Clock::now() + std::chrono::seconds {30};
    while (completions < expected_completions) {
        const auto before_steps = steps.load(std::memory_order_relaxed);
        const auto before = samples_p ? Clock::now() : Clock::time_point {};
        const auto count = control.poll_one();
        if (count and samples_p) {
            samples_p->m_poll_us.push_back(std::chrono::duration<double, std::micro>(Clock::now() - before).count());
            samples_p->m_callbacks += count;
            samples_p->m_max_steps_per_poll = std::max(samples_p->m_max_steps_per_poll, steps.load(std::memory_order_relaxed) - before_steps);
        }
        if (count == 0u) { std::this_thread::yield(); }
        if (Clock::now() >= deadline) { throw std::runtime_error("backend comparison timed out"); }
    }
    control.poll();
    if (error) { std::rethrow_exception(error); }
    if (steps != test_case.m_size) { throw std::runtime_error("incorrect comparison result"); }
}

}
