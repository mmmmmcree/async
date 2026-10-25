# Per-library identity and explicit file lists. Consumers select BUILD_SHARED_LIBS.
set(lib_name async)
set(lib_package LCFAsync)
set(lib_version 0.1.0)
set(lib_cxx_standard 20)
set(lib_header_only ON)
set(lib_namespace "")
set(lib_option_prefix "")
set(lib_headers
    include/async/async.h
    include/async/config.h
    include/async/core.h
    include/async/execution.h
    include/async/execution/details/provider.h)
set(lib_sources )
set(lib_module_files modules/async.cppm)
set(lib_module_name lcf.async)
set(lib_test_header async/async.h)
