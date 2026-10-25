set(sandbox_target ${lib_target}_sandbox)
set(sandbox_test ${lib_target}.sandbox)

add_executable(${sandbox_target} sandbox.cpp)
lcf_test_target(${sandbox_target})

add_test(NAME ${sandbox_test}
    COMMAND ${sandbox_target}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(${sandbox_test} PROPERTIES LABELS sandbox TIMEOUT 15)
