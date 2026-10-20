include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deferred_tests.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../tests/verify/interface_tests.cmake")

set(async_test_options)
if(asio_SOURCE_DIR)
    list(APPEND async_test_options CMAKE_ARGS "-DFETCHCONTENT_SOURCE_DIR_ASIO:PATH=${asio_SOURCE_DIR}")
endif()
if(TARGET async_asio_download)
    list(APPEND async_test_options DEPENDS async_asio_download)
endif()
async_add_deferred_tests(asio async DISCOVER_TESTS
    KINDS verify sandbox benchmark
    BENCHMARK_RESULTS_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/benchmark/results"
    ${async_test_options})

foreach(async_operation verify sandbox benchmark)
    if(NOT TARGET async_${async_operation})
        continue()
    endif()
    if(async_operation STREQUAL "verify")
        async_add_interface_tests(async_${async_operation})
        target_sources(async_${async_operation} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/verify/constraints_test.cpp")
    elseif(async_operation STREQUAL "sandbox")
        target_sources(async_${async_operation} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/sandbox/sandbox.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/native_example.cpp")
        if(LCF_ASYNC_USE_MODULES)
            target_sources(async_${async_operation} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/module_example.cpp")
        endif()
    else()
        target_sources(async_${async_operation} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/benchmark/benchmark.cpp")
    endif()
endforeach()

set(async_ctest_operations "")
if(LCF_ASYNC_ASIO_BUILD_VERIFY)
    list(APPEND async_ctest_operations verify)
endif()
if(LCF_ASYNC_ASIO_BUILD_SANDBOX)
    list(APPEND async_ctest_operations sandbox)
endif()
if(async_ctest_operations)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/deferred_ctest.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/deferred_ctest.cmake" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
        "${CMAKE_CURRENT_BINARY_DIR}/deferred_ctest.cmake")
endif()
