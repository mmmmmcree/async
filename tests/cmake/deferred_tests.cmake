include_guard(DIRECTORY)
include(ExternalProject)

function(lcf_defer_test kind option_suffix)
    set(target ${lib_target}_${kind})
    set(build_dir "${CMAKE_CURRENT_BINARY_DIR}/build")
    set(arguments -DBUILD_SHARED_LIBS:BOOL=${BUILD_SHARED_LIBS})
    get_cmake_property(variables VARIABLES)
    foreach(variable IN LISTS variables)
        if(variable MATCHES "^(${lib_option_prefix}_USE_MODULES|FETCHCONTENT_SOURCE_DIR_.*|FETCHCONTENT_(FULLY|UPDATES)_DISCONNECTED|CMAKE_(CXX_COMPILER|CXX_COMPILER_LAUNCHER|MAKE_PROGRAM|TOOLCHAIN_FILE|BUILD_TYPE|CONFIGURATION_TYPES|MSVC_RUNTIME_LIBRARY|CXX_STANDARD|CXX_FLAGS.*|EXE_LINKER_FLAGS.*|PREFIX_PATH|FIND_ROOT_PATH|SYSROOT|OSX_.*))$")
            string(REPLACE ";" "|" value "${${variable}}")
            list(APPEND arguments "-D${variable}:STRING=${value}")
        endif()
    endforeach()
    # Package hints survive a nested configure; parent-only targets do not.
    set(packages ${lib_dependency_packages})
    if(kind STREQUAL "verify")
        list(APPEND packages GTest)
    elseif(kind STREQUAL "benchmark")
        list(APPEND packages benchmark)
    endif()
    foreach(package IN LISTS packages)
        foreach(suffix DIR ROOT)
            set(variable ${package}_${suffix})
            if(DEFINED ${variable} AND NOT "${${variable}}" MATCHES "-NOTFOUND$")
                string(REPLACE ";" "|" value "${${variable}}")
                list(APPEND arguments "-D${variable}:PATH=${value}")
            endif()
        endforeach()
    endforeach()
    string(REPLACE ";" "|" parent_targets "${lib_external_dependency_targets}")
    if(parent_targets)
        list(APPEND arguments
            "-D${lib_option_prefix}_PARENT_DEPENDENCY_TARGETS:STRING=${parent_targets}")
    endif()
    set(prefix_path "${lib_dependency_prefix};${CMAKE_PREFIX_PATH}")
    string(REPLACE ";" "|" prefix_path "${prefix_path}")
    list(APPEND arguments
        "-DCMAKE_PREFIX_PATH:STRING=${prefix_path}"
        -D${lib_option_prefix}_BUILD_TESTS:BOOL=ON
        -D${lib_option_prefix}_INSTALL:BOOL=OFF
        -D${lib_option_prefix}_TESTS_IN_MAIN_BUILD:BOOL=ON
        -D${lib_option_prefix}_BUILD_VERIFY:BOOL=OFF
        -D${lib_option_prefix}_BUILD_SANDBOX:BOOL=OFF
        -D${lib_option_prefix}_BUILD_BENCHMARKS:BOOL=OFF
        -D${lib_option_prefix}_BUILD_${option_suffix}:BOOL=ON)

    set(dependency_target)
    if(TARGET ${lib_target}_install_dependencies)
        set(dependency_target ${lib_target}_install_dependencies)
    endif()
    ExternalProject_Add(${target}_project
        SOURCE_DIR "${lib_root}"
        BINARY_DIR "${build_dir}"
        PREFIX "${CMAKE_CURRENT_BINARY_DIR}/project"
        DOWNLOAD_COMMAND "" UPDATE_COMMAND "" INSTALL_COMMAND ""
        CMAKE_GENERATOR "${CMAKE_GENERATOR}"
        CMAKE_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}"
        CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}"
        CMAKE_GENERATOR_INSTANCE "${CMAKE_GENERATOR_INSTANCE}"
        LIST_SEPARATOR |
        CMAKE_ARGS ${arguments}
        DEPENDS ${dependency_target}
        CONFIGURE_HANDLED_BY_BUILD TRUE BUILD_ALWAYS TRUE
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
            --config "$<CONFIG>" --target ${target}
        USES_TERMINAL_CONFIGURE TRUE USES_TERMINAL_BUILD TRUE)
    add_custom_target(${target} DEPENDS ${target}_project)
    add_custom_target(${target}_run
        COMMAND "${CMAKE_COMMAND}" --build "${build_dir}"
            --config "$<CONFIG>" --target ${target}_run
        DEPENDS ${target}
        USES_TERMINAL VERBATIM)
    if(kind STREQUAL "verify")
        add_test(NAME ${lib_target}.verify
            COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                --config "$<CONFIG>" --target ${target}_run)
        set_tests_properties(${lib_target}.verify PROPERTIES
            LABELS verify RUN_SERIAL TRUE)
    endif()
endfunction()
