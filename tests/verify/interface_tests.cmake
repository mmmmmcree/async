include_guard(GLOBAL)
function(async_add_interface_tests target)
    set(async_interface_sources
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/execution_test.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/protocol_test.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/recovery_test.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/sharing_test.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/core_test.cpp")
    target_sources(${target} PRIVATE ${async_interface_sources})
    source_group(TREE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" PREFIX interface FILES ${async_interface_sources})
endfunction()
