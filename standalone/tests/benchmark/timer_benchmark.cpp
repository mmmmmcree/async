#include <NativeApi.h>
#include <benchmark/benchmark.h>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

namespace async = lcf::async;
namespace ex = async::exec;
using namespace std::chrono_literals;

struct Outcome
{
    std::size_t m_completions = 0;
    bool m_error = false;
};

struct Receiver
{
    using receiver_concept = ex::receiver_t;
    void set_value() noexcept { ++m_outcome.m_completions; }
    void set_stopped() noexcept { ++m_outcome.m_completions; }
    void set_error(std::exception_ptr) noexcept { m_outcome.m_error = true; ++m_outcome.m_completions; }
    Outcome & m_outcome;
};

using TimerSender = decltype(ex::with_stop_token(async::schedule_after(std::declval<async::Scheduler>(), 1h), std::stop_token {}));
using TimerOperation = ex::connect_result_t<TimerSender, Receiver>;
using Operations = std::vector<std::unique_ptr<TimerOperation>>;

void register_timers(Operations & operations, async::Context & context, Outcome & outcome, std::stop_token token, std::size_t count)
{
    operations.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto sender = ex::with_stop_token(async::schedule_after(context.getScheduler(), 1h), token);
        auto operation_up = std::unique_ptr<TimerOperation> {new TimerOperation(ex::connect(std::move(sender), Receiver {outcome}))};
        ex::start(*operation_up);
        operations.push_back(std::move(operation_up));
    }
}

void drain(async::Context & context, Outcome & outcome, std::size_t count)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (outcome.m_completions < count) {
        context.pollOne();
        if (std::chrono::steady_clock::now() >= deadline) { throw std::runtime_error("timer benchmark timed out"); }
    }
    if (outcome.m_error) { throw std::runtime_error("unexpected timer benchmark error"); }
}

void timer_register_cancel(benchmark::State & state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    for (auto iteration : state) {
        async::Context context;
        Outcome outcome;
        std::stop_source stop;
        Operations operations;
        register_timers(operations, context, outcome, stop.get_token(), count);
        stop.request_stop();
        drain(context, outcome, count);
        benchmark::DoNotOptimize(outcome.m_completions);
    }
    state.SetItemsProcessed(state.iterations() * count);
}

void poll_empty(benchmark::State & state)
{
    async::Context context;
    for (auto iteration : state) { benchmark::DoNotOptimize(context.pollOne()); }
}

void post_with_pending_timers(benchmark::State & state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    async::Context context;
    Outcome outcome;
    std::stop_source stop;
    Operations operations;
    register_timers(operations, context, outcome, stop.get_token(), count);
    for (auto iteration : state) {
        unsigned calls = 0;
        for (unsigned index = 0; index < 256; ++index) { async::post(context.getExecutor(), [&] { ++calls; }); }
        while (context.pollOne()) {}
        benchmark::DoNotOptimize(calls);
        if (calls != 256) { state.SkipWithError("incorrect callback count"); break; }
    }
    stop.request_stop();
    drain(context, outcome, count);
    state.SetItemsProcessed(state.iterations() * 256);
}

enum class PostPhase { eEnqueue, eDispatch };

void post_phase(benchmark::State & state, PostPhase phase)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    async::Context context;
    Outcome outcome;
    std::stop_source stop;
    Operations operations;
    register_timers(operations, context, outcome, stop.get_token(), count);
    for (auto iteration : state) {
        unsigned calls = 0;
        if (phase == PostPhase::eDispatch) { state.PauseTiming(); }
        for (unsigned index = 0; index < 256; ++index) { async::post(context.getExecutor(), [&] { ++calls; }); }
        if (phase == PostPhase::eDispatch) { state.ResumeTiming(); }
        else { state.PauseTiming(); }
        while (context.pollOne()) {}
        if (phase == PostPhase::eEnqueue) { state.ResumeTiming(); }
        benchmark::DoNotOptimize(calls);
        if (calls != 256) { state.SkipWithError("incorrect callback count"); break; }
    }
    stop.request_stop();
    drain(context, outcome, count);
    state.SetItemsProcessed(state.iterations() * 256);
}

void timer_run_until_idle(benchmark::State & state)
{
    for (auto iteration : state) {
        async::Context context;
        Outcome outcome;
        auto operation = ex::connect(async::schedule_after(context.getScheduler(), 1ms), Receiver {outcome});
        ex::start(operation);
        context.runUntilIdle();
        if (outcome.m_completions != 1 or outcome.m_error) { state.SkipWithError("timer did not keep runUntilIdle alive"); break; }
    }
}

BENCHMARK(timer_register_cancel)->Arg(32)->Arg(256)->Arg(4096)->UseRealTime();
BENCHMARK(poll_empty)->UseRealTime();
BENCHMARK(post_with_pending_timers)->Arg(0)->Arg(256)->Arg(4096)->UseRealTime();
BENCHMARK_CAPTURE(post_phase, enqueue, PostPhase::eEnqueue)->Arg(0)->Arg(4096)->UseRealTime();
BENCHMARK_CAPTURE(post_phase, dispatch, PostPhase::eDispatch)->Arg(0)->Arg(4096)->UseRealTime();
BENCHMARK(timer_run_until_idle)->UseRealTime();

}

#if defined(LCF_ASYNC_TIMER_BENCHMARK_MAIN)
BENCHMARK_MAIN();
#endif
