include_guard(GLOBAL)

# Common compile settings for development programs, independent of library identity.
function(lcf_test_target target)
    target_link_libraries(${target} PRIVATE ${lib_namespace}::${lib_target} ${ARGN})
    target_include_directories(${target} PRIVATE "${lib_test_include_dir}")
    target_compile_definitions(${target} PRIVATE
        LCF_TEST_USE_MODULES=$<BOOL:${${lib_option_prefix}_USE_MODULES}>)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(WIN32 AND lib_build_type STREQUAL "SHARED")
        # Keep local programs beside the library DLL for normal launch and CTest.
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:${lib_target}>")
    endif()
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" "-DFILES=$<TARGET_RUNTIME_DLLS:${target}>"
                "-DDESTINATION=$<TARGET_FILE_DIR:${target}>"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/copy_runtime.cmake"
            VERBATIM)
    endif()
endfunction()
