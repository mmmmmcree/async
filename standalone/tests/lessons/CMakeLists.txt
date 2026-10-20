include(../../../cmake/development.cmake)
include(sources.cmake)
async_add_test_suite(async_standalone_lessons standalone lessons PREFIX "standalone."
    SOURCES ${async_standalone_lesson_sources})
