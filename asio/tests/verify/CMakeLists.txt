include(../../../cmake/development.cmake)
include(../../../tests/verify/interface_tests.cmake)
async_add_test_suite(async_verify asio verify SOURCES constraints_test.cpp)
async_add_interface_tests(async_verify_tests)

if(LCF_ASYNC_USE_MODULES)
    target_sources(async_verify_tests PRIVATE module_bridge.cpp module_test.cpp)
    add_library(async_module_private_probe OBJECT EXCLUDE_FROM_ALL module_private_probe.cpp)
    async_configure_test_target(async_module_private_probe asio verify/internal)
    add_test(NAME Modules.InternalNamesAreNotExported
        COMMAND "${CMAKE_COMMAND}"
            "-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DCONFIG=$<CONFIG>"
            -DPROBE_TARGET=async_module_private_probe -DPRIVATE_NAME=details
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/check_module_privacy.cmake")
    set_tests_properties(Modules.InternalNamesAreNotExported PROPERTIES
        LABELS "async;module" TIMEOUT 60 RUN_SERIAL TRUE)
endif()
