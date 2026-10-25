include(../../../cmake/development.cmake)
include(../../../tests/verify/interface_tests.cmake)
async_add_test_suite(async_standalone_verify standalone verify PREFIX "standalone."
    SOURCES constraints_test.cpp poll_test.cpp awaitable_test.cpp coroutine_test.cpp timer_test.cpp expected_test.cpp)
async_add_interface_tests(async_standalone_verify_tests)

# Exercise the pump without linking/importing the backend or including timer headers.
add_executable(async_standalone_event_source_tests event_source_test.cpp)
target_compile_features(async_standalone_event_source_tests PRIVATE cxx_std_23)
target_compile_options(async_standalone_event_source_tests PRIVATE
    "$<TARGET_PROPERTY:async_standalone,INTERFACE_COMPILE_OPTIONS>")
target_include_directories(async_standalone_event_source_tests PRIVATE ../../include)
target_link_libraries(async_standalone_event_source_tests PRIVATE GTest::gtest_main Threads::Threads)
set_target_properties(async_standalone_event_source_tests PROPERTIES CXX_EXTENSIONS OFF
    CXX_SCAN_FOR_MODULES OFF FOLDER "async/standalone/tests/verify")
gtest_discover_tests(async_standalone_event_source_tests DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "standalone." PROPERTIES LABELS standalone_verify TIMEOUT 30)
add_dependencies(async_standalone_verify_tests async_standalone_event_source_tests)

if(LCF_ASYNC_USE_MODULES)
    target_sources(async_standalone_verify_tests PRIVATE module_bridge.cpp module_test.cpp)
    foreach(probe IN ITEMS frame queue)
        set(probe_target "async_standalone_module_${probe}_probe")
        string(TOUPPER "${probe}" probe_definition)
        if(probe STREQUAL "frame")
            set(private_name allocate_frame)
        else()
            set(private_name Queue)
        endif()
        add_library(${probe_target} OBJECT EXCLUDE_FROM_ALL module_private_probe.cpp)
        async_configure_test_target(${probe_target} standalone verify/internal)
        target_compile_definitions(${probe_target} PRIVATE "LCF_ASYNC_PROBE_${probe_definition}=1")
        add_test(NAME "standalone.Modules.${probe_definition}InternalsAreNotExported"
            COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DCONFIG=$<CONFIG>"
                "-DPROBE_TARGET=${probe_target}" "-DPRIVATE_NAME=${private_name}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/check_module_privacy.cmake")
        set_tests_properties("standalone.Modules.${probe_definition}InternalsAreNotExported"
            PROPERTIES LABELS "standalone_verify;modules" RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()
endif()
