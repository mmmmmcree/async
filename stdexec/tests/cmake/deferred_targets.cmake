include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deferred_tests.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../tests/verify/interface_tests.cmake")
async_add_deferred_tests(stdexec async_stdexec KINDS verify sandbox benchmark)

foreach(async_operation verify sandbox benchmark)
    if(NOT TARGET async_stdexec_${async_operation})
        continue()
    endif()
    if(async_operation STREQUAL "verify")
        async_add_interface_tests(async_stdexec_${async_operation})
        target_sources(async_stdexec_${async_operation} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/verify/native_test.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/verify/poll_test.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/verify/mechanisms_test.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/verify/awaitable_test.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/verify/stop_registration_test.cpp")
    elseif(async_operation STREQUAL "sandbox")
        target_sources(async_stdexec_${async_operation} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/sandbox/sandbox.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/native_example.cpp")
        if(LCF_ASYNC_USE_MODULES)
            target_sources(async_stdexec_${async_operation} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/sandbox/module_example.cpp")
        endif()
    else()
        target_sources(async_stdexec_${async_operation} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/benchmark/benchmark.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/native_benchmark.cpp")
    endif()
endforeach()
