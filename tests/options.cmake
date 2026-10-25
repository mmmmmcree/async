include_guard(DIRECTORY)

# Developer-only components; this file is loaded only when BUILD_TESTS is enabled.
option(${lib_option_prefix}_BUILD_VERIFY "Build ${lib_name} GoogleTest verification" ON)
option(${lib_option_prefix}_BUILD_SANDBOX "Build the ${lib_name} sandbox" ON)
option(${lib_option_prefix}_BUILD_BENCHMARKS "Build ${lib_name} Google Benchmark programs" ON)

# Also available to parent projects whose targets cannot cross a nested build boundary.
option(${lib_option_prefix}_TESTS_IN_MAIN_BUILD "Build development programs in this CMake tree" OFF)
mark_as_advanced(${lib_option_prefix}_TESTS_IN_MAIN_BUILD)
