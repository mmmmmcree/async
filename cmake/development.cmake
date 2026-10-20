include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/test_tools.cmake")

function(async_configure_test_target target backend kind)
    target_link_libraries(${target} PRIVATE lcf::async_${backend})
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/common"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../${backend}/tests/common")
    target_compile_definitions(${target} PRIVATE
        LCF_ASYNC_TEST_USE_MODULES=$<BOOL:${LCF_ASYNC_USE_MODULES}>)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF
        CXX_SCAN_FOR_MODULES "${LCF_ASYNC_USE_MODULES}" FOLDER "async/${backend}/tests/${kind}")
endfunction()

function(async_add_test_suite target backend kind)
    cmake_parse_arguments(PARSE_ARGV 3 ARG "" "PREFIX" "SOURCES")
    if(ARG_UNPARSED_ARGUMENTS OR ARG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Invalid async_add_test_suite arguments")
    endif()
    string(TOUPPER "${backend}" key)
    async_test_dependency(${key} GTEST GTest::gtest_main GTest googletest
        https://github.com/google/googletest.git 52eb8108c5bdec04579160ae17225d66034bd723)
    add_executable(${target}_tests ${ARG_SOURCES})
    async_configure_test_target(${target}_tests ${backend} ${kind})
    target_link_libraries(${target}_tests PRIVATE GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(${target}_tests DISCOVERY_MODE PRE_TEST NO_PRETTY_VALUES
        TEST_PREFIX "${ARG_PREFIX}" PROPERTIES LABELS "${backend}_${kind}" TIMEOUT 30)
    add_custom_target(${target}
        COMMAND "${CMAKE_CTEST_COMMAND}" -C "$<CONFIG>" --output-on-failure
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS ${target}_tests USES_TERMINAL)
    set_target_properties(${target} PROPERTIES FOLDER "async/${backend}/tests/${kind}")
endfunction()
