include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/backend.cmake")
async_backend_options(asio ON
    LCF_ASYNC_ASIO_PROVIDER
    LCF_ASYNC_ASIO_GTEST_PROVIDER
    LCF_ASYNC_ASIO_BENCHMARK_PROVIDER)
