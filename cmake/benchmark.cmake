include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/development.cmake")

function(async_add_benchmark target backend)
    string(TOUPPER "${backend}" key)
    async_test_dependency(${key} BENCHMARK benchmark::benchmark benchmark googlebenchmark
        https://github.com/google/benchmark.git 192ef10025eb2c4cdd392bc502f0c852196baa48)
    add_executable(${target}_runner "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/benchmark/benchmark.cpp")
    async_configure_test_target(${target}_runner ${backend} benchmark)
    target_link_libraries(${target}_runner PRIVATE benchmark::benchmark)

    set(fingerprints "")
    foreach(source IN LISTS async_implementation_sources)
        file(SHA256 "${source}" hash)
        string(APPEND fingerprints "${hash}")
    endforeach()
    string(SHA256 implementation_hash "${fingerprints}")
    if(LCF_ASYNC_USE_MODULES)
        set(representation module)
    else()
        set(representation header)
    endif()
    target_compile_definitions(${target}_runner PRIVATE
        LCF_ASYNC_BENCHMARK_CONFIGURATION="$<CONFIG>"
        LCF_ASYNC_IMPLEMENTATION_HASH="${implementation_hash}"
        LCF_ASYNC_IMPLEMENTATION="${representation}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${async_implementation_sources})

    set(results_directory "${LCF_ASYNC_${key}_BENCHMARK_RESULTS_DIRECTORY}")
    if(NOT results_directory)
        set(results_directory "${CMAKE_CURRENT_BINARY_DIR}/results")
    endif()
    set(results "${results_directory}/$<CONFIG>")
    add_custom_target(${target}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${results}"
        COMMAND "$<TARGET_FILE:${target}_runner>"
            --benchmark_min_time=0.05s
            --benchmark_repetitions=3
            --benchmark_report_aggregates_only=true
            "--benchmark_out=${results}/latest.json" --benchmark_out_format=json
        DEPENDS ${target}_runner USES_TERMINAL VERBATIM
        COMMENT "Running async benchmarks and writing ${results}/latest.json")
    set_target_properties(${target} PROPERTIES FOLDER "async/${backend}/tests/benchmark")
endfunction()
