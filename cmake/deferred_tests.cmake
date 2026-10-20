include_guard(GLOBAL)
include(ExternalProject)

# Keep nested builds on the same toolchain and dependency policy as their parent.
# ExternalProject's list separator preserves multi-config and search-path lists.
function(async_deferred_cmake_args output)
    set(arguments)
    get_cmake_property(variables VARIABLES)
    foreach(variable IN LISTS variables)
        if(variable MATCHES "^(LCF_ASYNC_.*PROVIDER|LCF_ASYNC_USE_MODULES|FETCHCONTENT_SOURCE_DIR_.*|FETCHCONTENT_(FULLY|UPDATES)_DISCONNECTED|CMAKE_(CXX_COMPILER|CXX_COMPILER_LAUNCHER|MAKE_PROGRAM|TOOLCHAIN_FILE|BUILD_TYPE|CONFIGURATION_TYPES|MSVC_RUNTIME_LIBRARY|CXX_FLAGS.*|EXE_LINKER_FLAGS.*|PREFIX_PATH|FIND_ROOT_PATH|SYSROOT|OSX_.*|EXPORT_COMPILE_COMMANDS))$")
            string(REPLACE ";" "|" value "${${variable}}")
            list(APPEND arguments "-D${variable}:STRING=${value}")
        endif()
    endforeach()
    set(${output} "${arguments}" PARENT_SCOPE)
endfunction()

# Expose entry targets without loading test frameworks in the outer build.
# Each nested project enables only the component requested by its entry target.
function(async_add_deferred_tests backend prefix)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "DISCOVER_TESTS" "BENCHMARK_RESULTS_DIRECTORY" "KINDS;CMAKE_ARGS;DEPENDS")
    if(ARG_UNPARSED_ARGUMENTS OR ARG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Invalid async_add_deferred_tests arguments")
    endif()
    string(TOUPPER "${backend}" key)
    get_cmake_property(variables VARIABLES)
    foreach(variable IN LISTS variables)
        if(variable MATCHES "^(LCF_ASYNC_DEPENDENCY_PROVIDER|LCF_ASYNC_${key}_.*PROVIDER)$"
            AND "${${variable}}" STREQUAL "PROVIDED")
            message(FATAL_ERROR "PROVIDED targets cannot cross build trees; set LCF_ASYNC_${key}_TESTS_IN_MAIN_BUILD=ON")
        endif()
    endforeach()
    async_deferred_cmake_args(arguments)
    list(APPEND arguments -DLCF_ASYNC_BUILD_TESTS:BOOL=ON -DLCF_ASYNC_INSTALL:BOOL=OFF
        -DLCF_ASYNC_${key}_TESTS_IN_MAIN_BUILD:BOOL=ON ${ARG_CMAKE_ARGS})
    set(components)
    foreach(kind IN LISTS ARG_KINDS)
        string(TOUPPER "${kind}" component)
        if(kind STREQUAL "benchmark")
            set(component BENCHMARKS)
        endif()
        list(APPEND components ${component})
    endforeach()
    foreach(kind component IN ZIP_LISTS ARG_KINDS components)
        if(NOT LCF_ASYNC_${key}_BUILD_${component})
            continue()
        endif()
        set(component_args)
        foreach(candidate IN LISTS components)
            if(candidate STREQUAL component)
                set(enabled ON)
            else()
                set(enabled OFF)
            endif()
            list(APPEND component_args -DLCF_ASYNC_${key}_BUILD_${candidate}:BOOL=${enabled})
        endforeach()
        set(target ${prefix}_${kind})
        if(kind STREQUAL "benchmark" AND ARG_BENCHMARK_RESULTS_DIRECTORY)
            list(APPEND component_args
                "-DLCF_ASYNC_${key}_BENCHMARK_RESULTS_DIRECTORY:PATH=${ARG_BENCHMARK_RESULTS_DIRECTORY}")
        endif()
        ExternalProject_Add(${target}_project
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.."
            BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/${kind}-build"
            PREFIX "${CMAKE_CURRENT_BINARY_DIR}/${kind}-project"
            DOWNLOAD_COMMAND "" UPDATE_COMMAND "" INSTALL_COMMAND ""
            CMAKE_GENERATOR "${CMAKE_GENERATOR}"
            CMAKE_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}"
            CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}"
            CMAKE_GENERATOR_INSTANCE "${CMAKE_GENERATOR_INSTANCE}"
            LIST_SEPARATOR |
            CMAKE_ARGS ${arguments} ${component_args}
            DEPENDS ${ARG_DEPENDS}
            CONFIGURE_HANDLED_BY_BUILD TRUE BUILD_ALWAYS TRUE EXCLUDE_FROM_ALL TRUE
            BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config "$<CONFIG>" --target ${target}
            USES_TERMINAL_CONFIGURE TRUE USES_TERMINAL_BUILD TRUE)
        add_custom_target(${target} DEPENDS ${target}_project)
        target_sources(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${kind}/CMakeLists.txt")
        set_target_properties(${target} PROPERTIES FOLDER "async/${backend}/tests/${kind}")
        set_target_properties(${target}_project PROPERTIES FOLDER "async/${backend}/tests/${kind}/internal")
        if(NOT kind STREQUAL "benchmark" AND NOT ARG_DISCOVER_TESTS)
            add_test(NAME ${backend}.${kind}
                COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config "$<CONFIG>" --target ${target})
            set_tests_properties(${backend}.${kind} PROPERTIES LABELS "async;${backend};deferred" RUN_SERIAL TRUE)
        endif()
    endforeach()
endfunction()
