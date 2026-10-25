# async

[English](README.md) | [简体中文](README.zh-CN.md)

Independent C++20 library. Target: LCFAsync::async.

## Build

    cmake --preset clang
    cmake --build build/clang --config Debug
    cmake --build build/clang --config Debug --target async_verify_run
    ctest --test-dir build/clang -C Debug --output-on-failure

Presets also provide msvc and gcc. MSVC requires its developer environment.

For compiled libraries, the caller selects -DBUILD_SHARED_LIBS=ON or OFF at configure time. Header-only targets ignore that choice. A prebuilt package retains its producer's type.

Project configuration lives in cmake/project/config.cmake. Tests are enabled by default for standalone development and disabled for consumers. Test framework acquisition is deferred until test targets run.

Header-only libraries without third-party dependencies can be used by copying include/async; no generated headers or CMake configuration are required. Compiled libraries use a generated export header, installed with their public headers, so static/shared builds work on Windows as well.

## Consume

    add_subdirectory(path/to/async)
    target_link_libraries(app PRIVATE LCFAsync::async)

Alternatively install the library and use find_package(LCFAsync CONFIG REQUIRED). Consumers provide third-party packages and runtime DLL deployment.

The execution provider is selected by cmake/project/execution.cmake. Native standard execution is used when available; otherwise stdexec is reused from a supplied target/package or acquired at build time. No regular deps.json manifest is required.
