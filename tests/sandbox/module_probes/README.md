# 模块最小复现

这些探针用来区分构建配置、编译器与 async 包装层的问题，单独打开本目录即可配置。不自动获取依赖，也不进入 async 默认构建。

| ASYNC_MODULE_PROBE | 检查什么 | 是否需要依赖 |
| --- | --- | --- |
| deduction | GMF 中的嵌套类模板推导指引，经导出变量在消费者实例化 | 无 |
| asio | 原生 io_context 与 awaitable，经别名进入模块消费者 | 原生 Asio include 目录 |
| stdexec | 原生 just/then/sync_wait 与 exec::task | 原生 stdexec include 目录 |

ASYNC_MODULE_PROBE_USE_MODULES=OFF 是同源码的头文件对照。消费者只编译为对象文件，不链接；运行行为由 async 的常规 sandbox 覆盖。

## 使用 CMake

从 async 根目录，在对应编译器开发环境运行：

```powershell
cmake -S tests/sandbox/module_probes -B build/probe-deduction-clang -G "Ninja Multi-Config" -DCMAKE_CXX_COMPILER=clang++ -DASYNC_MODULE_PROBE=deduction
cmake --build build/probe-deduction-clang --config Debug
```

把 clang++ 换为 cl、使用不同构建目录，即可检查 MSVC。加上 -DASYNC_MODULE_PROBE_USE_MODULES=OFF 可检查头文件模式。切换编译器时创建新目录或使用 --fresh。

选择 asio/stdexec 时，额外设置 ASYNC_MODULE_PROBE_INCLUDE_DIRECTORY，例如：

```powershell
cmake -S tests/sandbox/module_probes -B build/probe-stdexec-clang -G "Ninja Multi-Config" -DCMAKE_CXX_COMPILER=clang++ -DASYNC_MODULE_PROBE=stdexec -DASYNC_MODULE_PROBE_INCLUDE_DIRECTORY=E:/path/to/stdexec/include
cmake --build build/probe-stdexec-clang --config Debug
```

这些路径指向原生依赖，探针完全不包含 async 的头文件。模块与消费者都关闭 C++ 扩展，保持语言配置一致。

## 完全绕过 CMake

从 async 根目录，在 MSVC x64 开发者终端运行：

```powershell
New-Item -ItemType Directory -Force build | Out-Null
cl /nologo /std:c++latest /EHsc /MD /permissive- /Zc:__cplusplus /Zc:preprocessor /interface /TP /c tests/sandbox/module_probes/deduction.cppm /ifcOutput build/deduction.ifc /Fobuild/deduction.obj
cl /nologo /std:c++latest /EHsc /MD /permissive- /Zc:__cplusplus /Zc:preprocessor /DPROBE_USE_MODULES=1 /c tests/sandbox/module_probes/deduction_consumer.cpp /reference probe.deduction=build/deduction.ifc /Fobuild/deduction-consumer.obj
cl /nologo /std:c++latest /EHsc /MD /permissive- /Zc:__cplusplus /Zc:preprocessor /DPROBE_USE_MODULES=0 /c tests/sandbox/module_probes/deduction_consumer.cpp /Fobuild/deduction-header.obj
```

接口编译应成功；本机 MSVC 19.51 的第二条命令报 C2574，头文件对照成功。这段程序没有第三方依赖，也没有 static 构造函数；错误发生在导入后的推导指引处理。

## 本机结果

2026-10-20，Windows x64、CMake 4.3.0-rc2、MSVC 19.51.36248、Clang 22.1.1，原生依赖版本取自 [asio/deps.json](../../../asio/deps.json) 与 [stdexec/deps.json](../../../stdexec/deps.json)：

| 探针 | MSVC header | MSVC module | Clang header | Clang module |
| --- | --- | --- | --- | --- |
| deduction | 通过 | C2574 | 通过 | 通过 |
| asio | 通过 | promise_type / service 查找失败 | 通过 | 通过 |
| stdexec | 通过 | any_scheduler 模板错误 | 通过 | 通过 |

完整库的结果、修复方式与剩余限制见[模块说明](../../../docs/cpp-modules-transition.md)。构建输出、IFC/PCM、编译器崩溃日志保存在忽略的 build/ 下。
