include(../../../cmake/benchmark.cmake)
async_add_benchmark(async_standalone_benchmark standalone)
target_sources(async_standalone_benchmark_runner PRIVATE timer_benchmark.cpp)

add_executable(async_standalone_timer_benchmark_runner timer_benchmark.cpp)
async_configure_test_target(async_standalone_timer_benchmark_runner standalone benchmark)
target_link_libraries(async_standalone_timer_benchmark_runner PRIVATE benchmark::benchmark)
target_compile_definitions(async_standalone_timer_benchmark_runner PRIVATE LCF_ASYNC_TIMER_BENCHMARK_MAIN=1)
add_custom_target(async_standalone_timer_benchmark
    COMMAND "$<TARGET_FILE:async_standalone_timer_benchmark_runner>"
        --benchmark_min_time=0.1s --benchmark_repetitions=7
        --benchmark_report_aggregates_only=true
    DEPENDS async_standalone_timer_benchmark_runner USES_TERMINAL VERBATIM)
set_target_properties(async_standalone_timer_benchmark PROPERTIES FOLDER "async/standalone/tests/benchmark")
