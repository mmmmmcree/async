include(../../../cmake/development.cmake)
async_stdexec_test_dependency(BENCHMARK benchmark::benchmark benchmark googlebenchmark
    https://github.com/google/benchmark.git 192ef10025eb2c4cdd392bc502f0c852196baa48)
add_executable(async_stdexec_benchmark_tests ../../../tests/benchmark/benchmark.cpp)
async_configure_test_target(async_stdexec_benchmark_tests stdexec benchmark)
target_link_libraries(async_stdexec_benchmark_tests PRIVATE lcf::async_stdexec benchmark::benchmark)
set(LCF_ASYNC_STDEXEC_BENCHMARK_RESULTS_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results" CACHE PATH "Benchmark JSON output directory")
add_custom_target(async_stdexec_benchmark
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LCF_ASYNC_STDEXEC_BENCHMARK_RESULTS_DIRECTORY}"
    COMMAND $<TARGET_FILE:async_stdexec_benchmark_tests> --benchmark_min_time=0.1s
        "--benchmark_out=${LCF_ASYNC_STDEXEC_BENCHMARK_RESULTS_DIRECTORY}/latest.json" --benchmark_out_format=json
    DEPENDS async_stdexec_benchmark_tests USES_TERMINAL)
set_target_properties(async_stdexec_benchmark async_stdexec_benchmark_tests PROPERTIES FOLDER "async/stdexec/tests/benchmark")

add_executable(async_stdexec_native_benchmark_runner EXCLUDE_FROM_ALL native_benchmark.cpp)
async_configure_test_target(async_stdexec_native_benchmark_runner stdexec benchmark/native)
target_link_libraries(async_stdexec_native_benchmark_runner PRIVATE benchmark::benchmark)
add_executable(async_stdexec_native_diagnostics EXCLUDE_FROM_ALL native_benchmark.cpp
    ../../../tests/benchmark/allocation_probe.cpp)
async_configure_test_target(async_stdexec_native_diagnostics stdexec benchmark/native)
target_compile_definitions(async_stdexec_native_diagnostics PRIVATE LCF_ASYNC_NATIVE_DIAGNOSTICS=1)
set(native_results "${LCF_ASYNC_STDEXEC_BENCHMARK_RESULTS_DIRECTORY}/native/$<CONFIG>")
add_custom_target(async_stdexec_native_benchmark
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${native_results}"
    COMMAND "$<TARGET_FILE:async_stdexec_native_benchmark_runner>" --benchmark_min_time=0.05s
        "--benchmark_out=${native_results}/timing.json" --benchmark_out_format=json
    COMMAND "$<TARGET_FILE:async_stdexec_native_diagnostics>" "${native_results}/diagnostics.csv"
    DEPENDS async_stdexec_native_benchmark_runner async_stdexec_native_diagnostics USES_TERMINAL VERBATIM)
set_target_properties(async_stdexec_native_benchmark PROPERTIES FOLDER "async/stdexec/tests/benchmark/native")
