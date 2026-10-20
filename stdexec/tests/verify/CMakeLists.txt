include(../../../cmake/development.cmake)
include(../../../tests/verify/interface_tests.cmake)
async_add_test_suite(async_stdexec_verify stdexec verify
    PREFIX "stdexec."
    SOURCES awaitable_test.cpp stop_registration_test.cpp native_test.cpp poll_test.cpp
)
async_add_interface_tests(async_stdexec_verify_tests)
target_compile_definitions(async_stdexec_verify_tests PRIVATE LCF_ASYNC_STDEXEC=1)

if(LCF_ASYNC_USE_MODULES)
    target_sources(async_stdexec_verify_tests PRIVATE module_bridge.cpp module_test.cpp)
    add_library(async_stdexec_module_private_probe OBJECT EXCLUDE_FROM_ALL module_private_probe.cpp)
    async_configure_test_target(async_stdexec_module_private_probe stdexec verify/internal)
    add_test(NAME stdexec.Modules.InternalNamesAreNotExported
        COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DCONFIG=$<CONFIG>"
            -DPROBE_TARGET=async_stdexec_module_private_probe -DPRIVATE_NAME=allocate_frame
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/check_module_privacy.cmake")
    set_tests_properties(stdexec.Modules.InternalNamesAreNotExported PROPERTIES LABELS stdexec_verify RUN_SERIAL TRUE TIMEOUT 60)
    foreach(probe IN ITEMS frame queue)
        set(probe_target "async_stdexec_module_${probe}_probe")
        string(TOUPPER "${probe}" probe_definition)
        if(probe STREQUAL "frame")
            set(private_name allocate_frame)
        else()
            set(private_name Queue)
        endif()
        add_library(${probe_target} OBJECT EXCLUDE_FROM_ALL module_private_probe.cpp)
        async_configure_test_target(${probe_target} stdexec verify/internal)
        target_compile_definitions(${probe_target} PRIVATE "LCF_ASYNC_PROBE_${probe_definition}=1")
        add_test(NAME "stdexec.Modules.${probe_definition}InternalsAreNotExported"
            COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DCONFIG=$<CONFIG>"
                "-DPROBE_TARGET=${probe_target}" "-DPRIVATE_NAME=${private_name}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/check_module_privacy.cmake")
        set_tests_properties("stdexec.Modules.${probe_definition}InternalsAreNotExported"
            PROPERTIES LABELS stdexec_verify RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()
endif()

# Deliberately omit async_stdexec: native dependency include paths must not leak here.
add_executable(async_stdexec_mechanisms_tests mechanisms_test.cpp)
target_include_directories(async_stdexec_mechanisms_tests PRIVATE ../../include)
target_compile_features(async_stdexec_mechanisms_tests PRIVATE cxx_std_23)
target_link_libraries(async_stdexec_mechanisms_tests PRIVATE GTest::gtest_main Threads::Threads)
set_target_properties(async_stdexec_mechanisms_tests PROPERTIES FOLDER "async/stdexec/tests/verify/mechanisms" CXX_SCAN_FOR_MODULES OFF)
gtest_discover_tests(async_stdexec_mechanisms_tests DISCOVERY_MODE PRE_TEST TEST_PREFIX "stdexec."
    PROPERTIES LABELS stdexec_verify TIMEOUT 30)
add_dependencies(async_stdexec_verify async_stdexec_mechanisms_tests)
