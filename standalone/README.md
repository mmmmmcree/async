# standalone async

该后端提供当前 async 公共接口子集的无第三方依赖实现，运行库只使用 C++23 标准库和系统线程支持。完整 include 树位于本目录，不包含另两个后端的文件；与它们使用相同的公开命名空间，一个程序选择一种实现。

入口为 `<async.h>`，基础设施位于 `lcf::async`，协议与算法位于 `lcf::async::exec`。提供 Context/Scheduler/Executor、协作预算、Awaitable、计时器、Signal、停止，以及 just/then/let/when_all/split/sync_wait 和局部恢复/重试。

## 开发

从 async 根目录选择 header-msvc、header-clang 或 header-gcc，会启用三个后端及开发功能。该后端的目标使用 async_standalone_ 前缀：

```powershell
cmake --build --preset header-msvc-debug --target async_standalone_verify
cmake --build --preset header-msvc-debug --target async_standalone_sandbox
cmake --build --preset header-msvc-release --target async_standalone_benchmark
cmake --build --preset header-msvc-release --target async_compare
```

也可以单独配置：

```powershell
cmake -S standalone -B build/standalone-dev -G "Ninja Multi-Config" `
    -DLCF_ASYNC_BUILD_TESTS=ON -DLCF_ASYNC_DEPENDENCY_PROVIDER=FETCH `
    -DLCF_ASYNC_STANDALONE_TESTS_IN_MAIN_BUILD=ON
cmake --build build/standalone-dev --config Debug --target async_standalone_verify
```

库消费默认不启用测试。GoogleTest/benchmark 仅用于相应开发目标；本后端不获取 Asio/stdexec。关闭 TESTS_IN_MAIN_BUILD 时，verify/sandbox/benchmark 在构建具体目标时配置子构建。

## 消费

```cmake
add_subdirectory(path/to/async/standalone)
target_link_libraries(application PRIVATE lcf::async_standalone)
```

安装消费使用 `find_package(LCFAsync CONFIG REQUIRED COMPONENTS standalone)`，仍链接 lcf::async_standalone。完整 async 根项目中可用 LCF_ASYNC_ENABLE_STANDALONE 选择本后端，同时按需关闭另外两种实现。

示例见 [native_example.cpp](tests/sandbox/native_example.cpp)，分层、协程行为和验证边界见[解耦说明](../docs/backend-decoupling.md)。本机 GCC 的默认 TLS 帧缓存退出问题仍保留在测试中。

## 模块开发

MSVC 14.51 工具集（编译器 19.51）下使用独立开发预设：

```powershell
cmake --preset module-standalone-msvc
cmake --build --preset module-standalone-msvc-debug
ctest --preset module-standalone-msvc-debug
cmake --build --preset module-standalone-msvc-release --target async_standalone_benchmark
```

该预设只启用 standalone，verify、sandbox、benchmark 均开启。直接导入示例目标为 async_standalone_module_example；[源文件](../tests/sandbox/module_example.cpp) 仅包含自身使用的标准库头并 `import lcf.async;`，覆盖 worker 调度、协程 yield、合流、共享结果、timer 和 Signal。

模块接口 [async.cppm](modules/async.cppm) 把标准库放在全局模块片段，再用导出注解复用现有实现。公开头文件始终支持 include，应用自行选择 include/import。Debug/Release 各 78 项测试通过，两个配置的 10 项 benchmark 无错误；安装和迁移前缀后的双翻译单元导入消费通过。安装时静态库分别位于 lib/Debug、lib/Release，CMake 导出目标负责选择对应配置。详见[模块说明](../docs/cpp-modules-transition.md)。
