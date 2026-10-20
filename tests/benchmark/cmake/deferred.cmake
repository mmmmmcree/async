include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deferred_tests.cmake")
if(LCF_ASYNC_DEPENDENCY_PROVIDER STREQUAL "PROVIDED" OR LCF_ASYNC_BACKEND_BENCHMARK_PROVIDER STREQUAL "PROVIDED")
    message(FATAL_ERROR "PROVIDED targets cannot cross build trees; enable LCF_ASYNC_BACKEND_BENCHMARKS_IN_MAIN_BUILD")
endif()
set(async_compare_args -DLCF_ASYNC_BUILD_TESTS:BOOL=ON -DLCF_ASYNC_BUILD_BACKEND_BENCHMARKS:BOOL=ON
    -DLCF_ASYNC_BACKEND_BENCHMARKS_IN_MAIN_BUILD:BOOL=ON
    -DLCF_ASYNC_INSTALL:BOOL=OFF "-DLCF_ASYNC_BACKEND_BENCHMARK_RESULTS_DIRECTORY:PATH=${LCF_ASYNC_BACKEND_BENCHMARK_RESULTS_DIRECTORY}")
foreach(backend ASIO STDEXEC STANDALONE)
    list(APPEND async_compare_args -DLCF_ASYNC_ENABLE_${backend}:BOOL=${LCF_ASYNC_ENABLE_${backend}}
        -DLCF_ASYNC_${backend}_BUILD_TESTS:BOOL=OFF)
endforeach()
async_deferred_cmake_args(async_forwarded_args)
list(APPEND async_compare_args ${async_forwarded_args})
set(async_compare_dependencies)
if(TARGET async_asio_download)
    list(APPEND async_compare_dependencies async_asio_download)
endif()
if(asio_SOURCE_DIR)
    list(APPEND async_compare_args "-DFETCHCONTENT_SOURCE_DIR_ASIO:PATH=${asio_SOURCE_DIR}")
endif()
ExternalProject_Add(async_compare_project
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/compare-build"
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/compare-project"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" INSTALL_COMMAND ""
    CMAKE_GENERATOR "${CMAKE_GENERATOR}" CMAKE_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}"
    CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}"
    CMAKE_GENERATOR_INSTANCE "${CMAKE_GENERATOR_INSTANCE}"
    LIST_SEPARATOR | CMAKE_ARGS ${async_compare_args}
    DEPENDS ${async_compare_dependencies}
    CONFIGURE_HANDLED_BY_BUILD TRUE BUILD_ALWAYS TRUE EXCLUDE_FROM_ALL TRUE
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config "$<CONFIG>" --target async_compare
    USES_TERMINAL_CONFIGURE TRUE USES_TERMINAL_BUILD TRUE)
add_custom_target(async_compare DEPENDS async_compare_project)
target_sources(async_compare PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" "${CMAKE_CURRENT_SOURCE_DIR}/Workloads.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmark.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/probe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/report.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/allocation_probe.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/allocation_probe.h")
set_target_properties(async_compare PROPERTIES FOLDER "async/tests/benchmark")
set_target_properties(async_compare_project PROPERTIES FOLDER "async/tests/benchmark/internal")
