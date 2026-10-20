include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/test_tools.cmake")
set(LCF_ASYNC_BACKEND_BENCHMARK_PROVIDER "" CACHE STRING "Google Benchmark provider; empty inherits library policy")
set_property(CACHE LCF_ASYNC_BACKEND_BENCHMARK_PROVIDER PROPERTY STRINGS "" PACKAGE PROVIDED FETCH)
mark_as_advanced(LCF_ASYNC_BACKEND_BENCHMARK_PROVIDER)
async_test_dependency(BACKEND BENCHMARK benchmark::benchmark benchmark googlebenchmark
    https://github.com/google/benchmark.git 192ef10025eb2c4cdd392bc502f0c852196baa48)
