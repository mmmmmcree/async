include_guard(GLOBAL)
include(ExternalProject)

# Shared resolver for declarative manifests and project-specific dependency selection.
function(lcf_dependency)
    cmake_parse_arguments(PARSE_ARGV 0 dependency ""
        "NAME;TARGET;PACKAGE;SCOPE;GIT;GIT_TAG"
        "INCLUDE_DIRECTORIES;FIND_PACKAGES;LINK_LIBRARIES;COMPILE_FEATURES;COMPILE_DEFINITIONS;COMPILE_OPTIONS")
    foreach(field NAME TARGET PACKAGE)
        if(NOT dependency_${field})
            message(FATAL_ERROR "lcf_dependency requires ${field}")
        endif()
    endforeach()
    if(dependency_UNPARSED_ARGUMENTS OR dependency_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Invalid dependency declaration: ${dependency_UNPARSED_ARGUMENTS};${dependency_KEYWORDS_MISSING_VALUES}")
    endif()
    if(NOT dependency_SCOPE)
        set(dependency_SCOPE ${lib_scope})
    endif()
    if(NOT dependency_SCOPE MATCHES "^(PUBLIC|PRIVATE|INTERFACE)$")
        message(FATAL_ERROR "${lib_name}: dependency ${dependency_NAME} scope must be PUBLIC, PRIVATE or INTERFACE")
    endif()
    if(lib_build_type STREQUAL "INTERFACE" AND NOT dependency_SCOPE STREQUAL "INTERFACE")
        message(FATAL_ERROR "${lib_name}: an INTERFACE library requires INTERFACE dependencies")
    endif()

    set(target ${dependency_TARGET})
    set(package ${dependency_PACKAGE})
    list(APPEND lib_dependency_targets ${target})
    list(APPEND lib_dependency_packages ${package})
    list(APPEND lib_required_dependencies ${dependency_NAME})
    if(NOT (dependency_SCOPE STREQUAL "PRIVATE" AND lib_build_type STREQUAL "SHARED"))
        string(APPEND lib_package_dependencies
            "if(NOT TARGET ${target})\n    find_dependency(${package} CONFIG)\nendif()\n")
    endif()
    if(NOT TARGET ${target})
        find_package(${package} CONFIG QUIET)
    endif()
    if(TARGET ${target})
        list(APPEND lib_external_dependency_targets ${target})
        message(STATUS "${lib_name}: reusing dependency target ${target}")
    else()
        if(target IN_LIST ${lib_option_prefix}_PARENT_DEPENDENCY_TARGETS)
            message(FATAL_ERROR
                "The outer build supplies ${target}, but CMake targets cannot cross build trees. "
                "Make ${package} discoverable with ${package}_DIR or CMAKE_PREFIX_PATH, "
                "or supply the enabled test framework targets (GTest::gtest_main / "
                "benchmark::benchmark) before add_subdirectory(${lib_name}) to build tests in the same tree. "
                "${lib_option_prefix}_TESTS_IN_MAIN_BUILD=ON also keeps parent targets, but resolves "
                "missing test frameworks during configuration.")
        endif()
        if(NOT dependency_INCLUDE_DIRECTORIES)
            message(FATAL_ERROR
                "${lib_name}: source staging currently supports header-only dependencies. "
                "Provide ${target} or an installed ${package} package for compiled dependencies.")
        endif()
        string(TOUPPER "${dependency_NAME}" key)
        if(FETCHCONTENT_SOURCE_DIR_${key})
            set(source SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_${key}}"
                DOWNLOAD_COMMAND "" UPDATE_COMMAND "")
        elseif(dependency_GIT AND dependency_GIT_TAG)
            set(source SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/${dependency_NAME}-src"
                GIT_REPOSITORY "${dependency_GIT}" GIT_TAG "${dependency_GIT_TAG}"
                UPDATE_DISCONNECTED TRUE)
        else()
            message(FATAL_ERROR "${lib_name}: missing source repository/revision for ${dependency_NAME}")
        endif()

        set(metadata "")
        foreach(field TARGET PACKAGE INCLUDE_DIRECTORIES FIND_PACKAGES
                LINK_LIBRARIES COMPILE_FEATURES COMPILE_DEFINITIONS COMPILE_OPTIONS)
            string(APPEND metadata "set(dependency_${field} [==[${dependency_${field}}]==])\n")
        endforeach()
        set(metadata_file "${CMAKE_CURRENT_BINARY_DIR}/_deps/${dependency_NAME}-metadata.cmake")
        file(CONFIGURE OUTPUT "${metadata_file}" CONTENT "${metadata}" @ONLY)
        set(dependency_target ${lib_target}_${dependency_NAME}_dependency)
        ExternalProject_Add(${dependency_target}
            ${source}
            PREFIX "${CMAKE_CURRENT_BINARY_DIR}/_deps/${dependency_NAME}-project"
            CONFIGURE_COMMAND "" BUILD_COMMAND ""
            INSTALL_COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_DIR=<SOURCE_DIR>" "-DINSTALL_DIR=${lib_dependency_prefix}"
                "-DDEPENDENCY_FILE=${metadata_file}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/install_headers.cmake"
            EXCLUDE_FROM_ALL TRUE
            USES_TERMINAL_DOWNLOAD TRUE USES_TERMINAL_INSTALL TRUE)
        ExternalProject_Add_StepDependencies(${dependency_target} install
            "${metadata_file}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/install_headers.cmake")
        file(MAKE_DIRECTORY "${lib_dependency_prefix}/include")
        add_library(${target} INTERFACE IMPORTED GLOBAL)
        set_target_properties(${target} PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${lib_dependency_prefix}/include")
        foreach(package IN LISTS dependency_FIND_PACKAGES)
            find_package(${package} REQUIRED)
        endforeach()
        foreach(field LINK_LIBRARIES COMPILE_FEATURES COMPILE_DEFINITIONS COMPILE_OPTIONS)
            if(dependency_${field})
                set_property(TARGET ${target} PROPERTY INTERFACE_${field} "${dependency_${field}}")
            endif()
        endforeach()
        add_dependencies(${target} ${dependency_target})
        add_dependencies(${lib_target}_install_dependencies ${dependency_target})
    endif()
    # Preserve the public dependency name rather than a parent's private alias target.
    target_link_libraries(${lib_target} ${dependency_SCOPE}
        "$<BUILD_INTERFACE:${target}>$<INSTALL_INTERFACE:$<1:${target}>>")
    return(PROPAGATE lib_dependency_targets lib_dependency_packages lib_required_dependencies
        lib_external_dependency_targets lib_package_dependencies)
endfunction()

