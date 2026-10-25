if(NOT TARGET benchmark::benchmark)
    find_package(benchmark CONFIG QUIET)
endif()
if(NOT TARGET benchmark::benchmark AND NOT ${lib_option_prefix}_TESTS_IN_MAIN_BUILD)
    include("${lib_root}/tests/cmake/deferred_tests.cmake")
    lcf_defer_test(benchmark BENCHMARKS)
    return()
endif()
if(NOT TARGET benchmark::benchmark)
    include(FetchContent)
    block()
    set(BUILD_SHARED_LIBS OFF)
    set(BENCHMARK_ENABLE_TESTING OFF)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
    set(BENCHMARK_ENABLE_INSTALL OFF)
    set(BENCHMARK_ENABLE_WERROR OFF)
    FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG 192ef10025eb2c4cdd392bc502f0c852196baa48)
    FetchContent_MakeAvailable(googlebenchmark)
    endblock()
endif()

set(benchmark_target ${lib_target}_benchmark)
set(benchmark_results "${CMAKE_CURRENT_BINARY_DIR}/results/$<CONFIG>")

add_executable(${benchmark_target} benchmark.cpp)
lcf_test_target(${benchmark_target} benchmark::benchmark)

# Performance runs are explicit, never part of the default build or CTest.
add_custom_target(${benchmark_target}_run
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${benchmark_results}"
    COMMAND "$<TARGET_FILE:${benchmark_target}>"
        "--benchmark_out=${benchmark_results}/latest.json"
        --benchmark_out_format=json
    DEPENDS ${benchmark_target}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL VERBATIM)
