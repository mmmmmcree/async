include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/backend.cmake")
async_backend_options(standalone OFF
    LCF_ASYNC_STANDALONE_GTEST_PROVIDER
    LCF_ASYNC_STANDALONE_BENCHMARK_PROVIDER)
cmake_dependent_option(LCF_ASYNC_STANDALONE_BUILD_LESSONS "Provide standalone coroutine lessons" ON
    "LCF_ASYNC_STANDALONE_BUILD_TESTS" OFF)
