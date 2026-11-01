# Per-library identity and file selection. Consumers select BUILD_SHARED_LIBS.
set(lib_name async)
set(lib_package LCFAsync)
set(lib_version 0.1.0)
set(lib_cxx_standard 20)
set(lib_header_only ON)
set(lib_namespace "")
set(lib_option_prefix "")
file(GLOB_RECURSE lib_headers CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/${lib_name}/*.h")
set(lib_sources )
set(lib_module_files modules/async.cppm)
set(lib_module_name lcf.async)
set(lib_test_header async/async.h)
