if(NOT TARGET GTest::gtest_main)
    find_package(GTest CONFIG QUIET)
endif()
if(NOT TARGET GTest::gtest_main AND NOT ${lib_option_prefix}_TESTS_IN_MAIN_BUILD)
    include("${lib_root}/tests/cmake/deferred_tests.cmake")
    lcf_defer_test(verify VERIFY)
    return()
endif()
if(NOT TARGET GTest::gtest_main)
    include(FetchContent)
    block()
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_GMOCK OFF)
    set(INSTALL_GTEST OFF)
    set(gtest_force_shared_crt ON)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723)
    FetchContent_MakeAvailable(googletest)
    endblock()
endif()

set(verify_target ${lib_target}_verify)
add_executable(${verify_target} execution_test.cpp)
lcf_test_target(${verify_target} GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(${verify_target}
    TEST_PREFIX ${lib_target}.verify.
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS verify TIMEOUT 30)

# Build first, then execute every discovered test in the verification subtree.
add_custom_target(${verify_target}_run
    COMMAND "${CMAKE_CTEST_COMMAND}"
        --test-dir "${CMAKE_CURRENT_BINARY_DIR}" -C "$<CONFIG>"
        --output-on-failure --no-tests=error
    DEPENDS ${verify_target}
    USES_TERMINAL VERBATIM)
