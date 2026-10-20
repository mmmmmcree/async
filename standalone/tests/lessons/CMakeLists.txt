include(../../../cmake/development.cmake)
async_add_test_suite(async_standalone_lessons standalone lessons PREFIX "standalone."
    SOURCES lesson01_frame_lifetime.cpp
        lesson02_await_protocol.cpp
        lesson03_nested_coroutines.cpp
        lesson04_ownership.cpp)
