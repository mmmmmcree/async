cmake_minimum_required(VERSION 3.28)

include(cmake/project/config.cmake)
project(${lib_package} VERSION ${lib_version} LANGUAGES CXX)
include(cmake/utilities/library.cmake)

if(EXISTS "${lib_root}/cmake/project/configure.cmake")
    include(cmake/project/configure.cmake)
endif()

if(${lib_option_prefix}_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(${lib_option_prefix}_INSTALL)
    include(cmake/utilities/install.cmake)
endif()
