include_guard(GLOBAL)

macro(async_initialize_project)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR AND NOT EXISTS "${CMAKE_BINARY_DIR}/.gitignore")
        file(WRITE "${CMAKE_BINARY_DIR}/.gitignore" "*\n")
    endif()
endmacro()
