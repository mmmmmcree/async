include(../../../cmake/development.cmake)
include(../../../tests/verify/interface_tests.cmake)
async_add_test_suite(async_standalone_verify standalone verify PREFIX "standalone."
    SOURCES constraints_test.cpp poll_test.cpp awaitable_test.cpp coroutine_test.cpp)
async_add_interface_tests(async_standalone_verify_tests)

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
                -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/check_module_privacy.cmake")
        set_tests_properties("standalone.Modules.${probe_definition}InternalsAreNotExported"
            PROPERTIES LABELS "standalone_verify;modules" RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()
endif()
