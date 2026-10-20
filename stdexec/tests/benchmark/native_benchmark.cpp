#include <NativeWorkloads.h>

#if defined(LCF_ASYNC_NATIVE_DIAGNOSTICS)
#include "../../../tests/benchmark/allocation_probe.h"
#include <fstream>
#include <vector>

int main(int argc, char ** argv)
{
    namespace native = lcf::async::testing::native;
    namespace allocation = lcf::async::comparison::allocation;
    if (argc != 2) { return 1; }
    std::ofstream output {argv[1]};
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << "case,callbacks,poll_p50_us,poll_p99_us,poll_max_us,max_steps_per_poll,new_calls,new_bytes,peak_bytes,remaining_bytes\n";
    const char * names[] {"inline_then", "explicit_transfers", "when_all"};
    for (unsigned index = 0; index < 3; ++index) {
        const auto workload = static_cast<native::Workload>(index);
        native::run(workload);
        allocation::begin();
        native::run(workload);
        const auto memory = allocation::end();
        std::vector<double> timings;
        timings.reserve(800);
        unsigned callbacks = 0;
        unsigned max_steps = 0;
        for (unsigned repeat = 0; repeat < 100; ++repeat) {
            native::Samples samples;
            native::run(workload, &samples);
            callbacks += samples.m_callbacks;
            max_steps = std::max(max_steps, samples.m_max_steps_per_poll);
            timings.insert(timings.end(), samples.m_poll_us.begin(), samples.m_poll_us.begin() + samples.m_callbacks);
        }
        std::sort(timings.begin(), timings.end());
        output << names[index] << ',' << callbacks / 100.0 << ',' << timings[timings.size() / 2]
            << ',' << timings[(timings.size() - 1) * 99 / 100] << ',' << timings.back() << ',' << max_steps
            << ',' << memory.m_allocations << ',' << memory.m_bytes << ',' << memory.m_peak_bytes << ',' << memory.m_live_bytes << '\n';
    }
}
#else
#include <benchmark/benchmark.h>

namespace {

void native_workload(benchmark::State & state)
{
    const auto workload = static_cast<lcf::async::testing::native::Workload>(state.range(0));
    for (auto iteration : state) { lcf::async::testing::native::run(workload); }
    state.SetItemsProcessed(state.iterations() * 3);
}

BENCHMARK(native_workload)->ArgName("workload")->Arg(0)->Arg(1)->Arg(2)->UseRealTime();

}

BENCHMARK_MAIN();
#endif
