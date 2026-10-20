include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deferred_tests.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../tests/verify/interface_tests.cmake")
async_add_deferred_tests(standalone async_standalone KINDS verify sandbox lessons benchmark)

foreach(async_operation verify sandbox lessons benchmark)
    if(NOT TARGET async_standalone_${async_operation})
        continue()
    endif()
    if(async_operation STREQUAL "verify")
        async_add_interface_tests(async_standalone_${async_operation})
        target_sources(async_standalone_${async_operation} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/verify/constraints_test.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/verify/poll_test.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/verify/awaitable_test.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/verify/coroutine_test.cpp")
    elseif(async_operation STREQUAL "sandbox")
        target_sources(async_standalone_${async_operation} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/sandbox/sandbox.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/native_example.cpp")
        if(LCF_ASYNC_USE_MODULES)
            target_sources(async_standalone_${async_operation} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/sandbox/module_example.cpp")
        endif()
    elseif(async_operation STREQUAL "lessons")
        include("${CMAKE_CURRENT_SOURCE_DIR}/lessons/sources.cmake")
        foreach(async_lesson_source IN LISTS async_standalone_lesson_sources)
            target_sources(async_standalone_${async_operation} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/lessons/${async_lesson_source}")
        endforeach()
    else()
        target_sources(async_standalone_${async_operation} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/benchmark/benchmark.cpp")
    endif()
endforeach()
