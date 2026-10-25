# async

[English](README.md) | [简体中文](README.zh-CN.md)

独立的 C++20 库。目标：LCFAsync::async。

## 构建

    cmake --preset clang
    cmake --build build/clang --config Debug
    cmake --build build/clang --config Debug --target async_verify_run
    ctest --test-dir build/clang -C Debug --output-on-failure

预设还提供 msvc 和 gcc。MSVC 需要开发者环境。

编译型库由调用者在配置时通过 -DBUILD_SHARED_LIBS=ON 或 OFF 选择类型。纯头文件目标忽略此选项。预编译包保留构建该包时选定的类型。

项目配置位于 cmake/project/config.cmake。测试仅在独立开发时默认开启，作为消费者使用时默认关闭。测试框架会延迟到测试目标执行时获取。

没有第三方依赖的纯头文件库可直接复制 include/async 使用，不需要生成头文件或运行 CMake 配置。编译型库使用生成的导出头文件，并将其与公共头文件一同安装，因此 Windows 上也支持静态和动态库构建。

## 使用

    add_subdirectory(path/to/async)
    target_link_libraries(app PRIVATE LCFAsync::async)

也可以先安装本库，再使用 find_package(LCFAsync CONFIG REQUIRED)。第三方依赖包和运行时 DLL 部署由消费者提供和负责。

## Execution 实现选择

本库由 cmake/project/execution.cmake 探测并选择 execution 实现。优先使用标准库提供的 execution；不可用时，复用外层提供的 stdexec 目标或安装包，否则在构建时获取。此选择无需普通的 deps.json 清单。
