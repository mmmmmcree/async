include(../../../cmake/development.cmake)
async_add_test_suite(async_stdexec_sandbox stdexec sandbox
    PREFIX "stdexec."
    SOURCES ../../../tests/sandbox/sandbox.cpp)
add_executable(async_stdexec_native_example native_example.cpp)
async_configure_test_target(async_stdexec_native_example stdexec sandbox)
add_dependencies(async_stdexec_sandbox async_stdexec_native_example)
add_test(NAME stdexec.NativeExample COMMAND async_stdexec_native_example)
set_tests_properties(stdexec.NativeExample PROPERTIES LABELS stdexec_sandbox TIMEOUT 15)

if(LCF_ASYNC_USE_MODULES)
    add_executable(async_stdexec_module_example ../../../tests/sandbox/module_example.cpp)
    async_configure_test_target(async_stdexec_module_example stdexec sandbox)
    add_dependencies(async_stdexec_sandbox async_stdexec_module_example)
    add_test(NAME stdexec.ModuleExample COMMAND async_stdexec_module_example)
    set_tests_properties(stdexec.ModuleExample PROPERTIES LABELS "stdexec_sandbox;modules" TIMEOUT 15)
endif()
