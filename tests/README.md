# async 测试与测量

更新：2026-10-20。公共测试归 async 所有，三种后端分别编译同一套执行契约；后端专属测试保留底层模型与运行设施验证。旧 Chain/Junction 测试已按语义迁移，旧 task 代码已删除；不再保留 compile test 模块，sandbox 验证实际消费。

## 测试组织

| 区域 | 内容 |
| --- | --- |
| tests/verify | execution、protocol、recovery、sharing、core 公共契约 |
| tests/common | NativeApi、完成观察和原生小负载；不改写执行算法 |
| tests/sandbox | 工厂创建独立图、共享与合流的应用用例 |
| asio/tests/verify | 自有 continuation 的概念约束及可选模块探针 |
| stdexec/tests/verify | 协程帧、停止注册、timer/signal 竞争、协作预算、纯机制隔离及可选模块探针 |
| standalone/tests/verify | 公共契约、概念约束、协作预算、自有协程状态/恢复/停止/帧生命周期；模块跨翻译单元与内部名称边界 |
| 各后端 tests/common/Runtime.h | 仅统一测试推进入口，不隐藏协议完成通道 |
| tests/benchmark | 共享负载、分配探针、并排报告 |

async_verify / async_stdexec_verify / async_standalone_verify 构建并运行 verify；对应 sandbox 目标运行应用用例与各自示例。开发 preset 在主构建发现全部测试；普通消费仍可按选项选择主构建或延迟子构建。

公共契约包括：惰性 connect/start、值/错误/stopped、void/多参数、仅可移动值与函数、线程恢复、两个 worker 实际时间重叠、显式调度边界、异步恢复、重试上限/局部性/取消、大量同步重试、合流等待所有分支、共享缓存与晚订阅、取消单个及最后消费者、PMR 释放，以及完成时立即销毁 operation state。

## 当前验证结果

| 配置 | 结果 |
| --- | --- |
| MSVC 19.51 header Debug | 全部构建，203/203 项通过 |
| Clang 22.1 header Debug | 全部构建，203/203 项通过 |
| WinLibs GCC 16.1 header Debug | 全部构建，197/203 项通过；6 项默认 TLS 缓存退出失败保留，失败集合存在运行差异 |
| Clang ASan RelWithDebInfo，standalone | 72/72 项通过；使用匹配的 Clang 22 runtime 与非 Debug CRT |
| MSVC 19.51 Debug / Release benchmark | 三后端独立 benchmark、async_compare 的时间/poll/分配探针、原生 stdexec benchmark 全部运行成功；独立与比较各 10 项计时无错误，各 9 项 poll/分配记录完整 |
| MSVC 19.44 benchmark | 原生 stdexec 的 16 步 continues_on + then 图在 connect 实例化时触发 C1202；相同负载在 19.51 通过，开发 preset 明确选择 MSVC 14.51 工具集 |
| 独立 Asio/stdexec 消费 | 源码和安装迁移消费回归；Asio 不引入 stdexec |
| 独立 standalone 消费 | 无第三方配置、直接编译头文件、源码/PROVIDED/FetchContent、安装及迁移前缀消费通过 |
| 开发选项 | standalone 延迟 verify 72 项通过，仅 benchmark 无 GoogleTest；stdexec+standalone 比较配置无 Asio |
| MSVC 19.51 standalone module Debug / Release | 各 78/78 项通过；包含纯 import 示例、跨翻译单元 Scheduler、帧辅助函数与 Queue 不可按内部名称访问 |
| standalone module benchmark | Debug/Release 各 10 项计时负载运行成功，JSON 无错误项 |
| standalone module 安装 | Debug/Release 共用安装前缀、迁移目录后的双翻译单元纯 import 消费通过；生成的 BMI 编译使用迁移后的模块源码 |
| Clang 22.1.1 Asio module Debug / Release | 各 55/55 项通过，公共 10 项 benchmark 及双配置安装迁移消费通过 |
| Clang 22.1.1 stdexec module Debug / Release | 各 81/81 项通过，公共及原生 benchmark、双配置安装迁移消费通过 |
| MSVC 19.51 Asio/stdexec module | 原生薄模块仍失败；无第三方推导指引亦复现 C2574，保留独立探针 |

本轮未改外层集合或引擎示例；它们若还使用 task，需要另行迁移。

工具集切换后重新运行 MSVC 19.51 Debug 完整构建与 203 项 CTest，全部通过。Asio 获取脚本同时回归重复配置：Windows Git 把 `asio/include` 检出为符号链接占位文件时，忽略该候选路径，使用实际的 `include` 目录，避免再次创建同名目录失败。

模块开发预设为 module-asio-clang、module-stdexec-clang、module-standalone-msvc，使用 CMake 4.3.0-rc2 验收。standalone 的 78 项由 72 项原有 verify、一个跨翻译单元用例、两个内部名称探针和三个 sandbox/示例组成。Asio 与 stdexec 同样覆盖公共契约、跨翻译单元、内部名称边界及真实消费者；stdexec 保留原生 task 双向互操作。

[stdexec/standalone 直接导入示例](sandbox/module_example.cpp)及 [Asio 示例](../asio/tests/sandbox/module_example.cpp)不经过 NativeApi.h，也不包含 async/dependencies.h。NativeApi.h 的模块分支仅包含标准头。新修复后，三后端 MSVC header Debug 回归为 203/203，standalone 模块 Debug 回归为 78/78。编译器归因对照见 [module_probes](sandbox/module_probes/README.md)，完整边界见[模块说明](../docs/cpp-modules-transition.md)。

### GCC 的 TLS 限制

本轮最终一次失败为 stdexec.Core.CoroutineNonDefaultValue、stdexec.Awaitable.DefaultFrameOutlivesCreatorThread、stdexec.Awaitable.DefaultFrameReleasesAfterCacheRetirement、standalone.Coroutine.NestedRescheduleRestoresParent，以及 standalone 的两个同名默认帧退出用例，均为 0xc0000374。另一次同轮运行为 198/203；失败数量不能作为稳定结果，未跳过这些用例。

此前 GDB 观察到 FrameCache 析构时其 TLS 对象存储已被释放，内容呈 0xfeeefeee，随后访问失效链表。为检查新的调度恢复用例，另建不使用 GoogleTest 的小程序，重复创建 worker、切换并恢复父 scheduler：默认帧缓存 4 次均失败；在主线程和全部 worker 上用 FrameAllocatorScope 注入 new_delete_resource、绕过 TLS 缓存后，4 次均通过。这支持已有 TLS 路径判断，但不等于整个 GCC 测试矩阵通过，也没有为工具链改写库的默认分配行为。

下列程序不包含 async、stdexec 或 Asio，在本机同一 GCC 工具链也复现 0xc0000374；-static-libgcc 及同时静态链接 libstdc++ 均未消除：

```cpp
#include <array>
#include <thread>

struct Cache {
    std::array<void *, 127> heads {};
    ~Cache() { for (auto p : heads) { if (p) { ::operator delete(p); } } }
};
Cache & cache() { thread_local Cache value; return value; }
int main() {
    cache();
    auto * p = ::operator new(192);
    std::jthread worker {[p] { cache().heads[1] = p; }};
}
```

观察与 [MinGW TLS 析构顺序问题](https://sourceforge.net/p/mingw-w64/bugs/727/)所述情形一致；[GCC 的 emulated TLS 实现](https://github.com/gcc-mirror/gcc/blob/master/libgcc/emutls.c)与[MinGW TLS 析构回调](https://github.com/mirror/mingw-w64/blob/master/mingw-w64-crt/crt/tls_atexit.c)是进一步定位入口。这里记录的是本机复现，不推断所有 GCC 平台都有问题。测试未跳过，库内未加入为此隐藏行为的平台特判。

## 运行比较基准

```powershell
cmake --build --preset header-msvc-release --target async_compare
cmake --build --preset header-msvc-release --target async_stdexec_native_benchmark
```

async_compare 生成 timing、diagnostics、memory 三类独立程序结果，再生成 summary.md、timing.csv、diagnostics.csv、memory.csv 和各后端原始 JSON/日志。结果位于 build/<preset>/tests/benchmark/results/<配置>。每项测试为一行，两实现用时为相邻列，Winner 只标记该项用时较低者；相等为 tie，缺失和失败不选优胜者。

| 当前负载 | 内容 |
| --- | --- |
| dispatch_ready | 预先 post 256 回调，只测排空 |
| post | 创建 context、post 256 次、排空并销毁 |
| scheduled_chain | 16 次 continues_on + then，包含图连接、执行和释放 |
| shared_fanout | 一个 split 源供 256 分支，每分支显式迁移至 control |
| cached_observers | 先完成共享源，再启动 256 个晚订阅分支 |
| timer_ready / timer_wait | 256 个 schedule_after，延迟为零 / 1ms |
| inline_await | 一个协程等待 256 个立即子协程，通过 core::as_sender 接入 |
| yield_await | 256 个显式 yield |
| worker_scheduled_chain | 16 次 worker 调度，最后交付回 control |

三后端编译同一份负载，根 operation 都持有到完成。scheduled_chain 使用独立的 Increment 函数类型，避免递归工厂的 lambda 类型携带整个上游图。协程创建、strand/恢复、图连接和销毁等成本属于实际对应实现，不能把完整负载结果直接解释成底层队列或 timer 容器复杂度。

原生小负载另外比较三次内联 then、三次显式迁移、三分支 when_all。它们不包含旧 Chain 的编排成本。当前负载与下面的历史 Chain 测量建立不同基线，不直接作前后优化百分比比较。

poll 诊断只采样成功出队，报告分位数、最大值、回调数及每次最多业务步骤。worker 场景中的步骤差包含并行执行，不能当成 control 回调直接调用次数。时间预算不抢占当前 handler，有限次最大值也不是硬上限。

内存探针在独立进程替换 C++ new/delete，记录次数、请求字节、峰值与 remaining；不用于速度比较。它不覆盖全部 malloc、内核资源、RSS 或 allocator 容量；缓存和异步收尾也影响 remaining。Asio 有自己的帧回收路径，不能根据 new 计数断言完整资源占用比例。

## 历史测量与迁移记录

以下保留原始日期、旧接口语义和当时的验证结论。其中 Chain/Junction、InvocationSender、Delivery 等已在第二阶段删除；历史链接和叙述用于理解当时的优化，不代表当前入口或实现。

## 基准驱动的 stdexec 优化

当前实现与最新结果见[第二轮](#第二轮)。第一轮保留为基线记录；两轮均为本机短时间样本，不是跨平台结论或回归阈值。

### 第一轮

2026-10-19，本机 MSVC 19.44、Release，Asio pin 对应 1.38.2，stdexec pin 见其 deps.json。先保存优化前的可执行文件，再用相同负载构建优化后程序；停止编译和测试后，前后程序交错运行各 3 轮，取完整负载耗时的中位数，单位 µs。Asio 对照程序 SHA256 前后一致。以下是 stdexec 本轮前后对比，跨后端优胜者仍由公共报告逐项独立标记。

| 场景 | stdexec 本轮前 | stdexec 本轮后 | 耗时变化 |
| --- | ---: | ---: | ---: |
| 排空 256 个 ready callback | 5.015 | 5.420 | +8.1% |
| 创建、post 256 次并排空 | 21.626 | 22.270 | +3.0% |
| 64 节点 chain | 253.384 | 160.426 | -36.7% |
| 256 路同 junction 扇出 | 1731.668 | 1276.512 | -26.3% |
| 256 个独立 merge 观察者 | 1827.000 | 1386.958 | -24.1% |
| 256 个零延迟 timer chain | 2935.822 | 2346.263 | -20.1% |
| 256 个 1ms timer chain | 15539.983 | 15440.471 | -0.6% |
| 256 次立即完成子任务 | 101.492 | 84.052 | -17.2% |
| 256 次显式 yield | 338.179 | 277.621 | -17.9% |
| 64 节点 worker chain | 448.704 | 288.412 | -35.7% |

主要变化是同步任务使用独立 sender、内置队列绕过第二层 scheduler 擦除、延迟构造取消异常、根 holder 内嵌。同步 sender 借用正在等待的协程所持有的输入，只擦除调用入口，兼顾稳定存储与模板实例化规模。实现说明见 [stdexec README](../stdexec/README.md#调度与分配成本)。

原始队列没有修改；纯出队和 post 场景本次分别增加约 0.4 和 0.6 µs，不把这两项列作收益。1ms timer 场景主要受等待和 OS 调度影响，0.6% 的差异不足以支持明确的性能结论。所有数字都是本机短时间样本，不是跨平台结论或回归阈值。

可观察 C++ 分配的变化：

| 指标 | 本轮前 | 本轮后 |
| --- | ---: | ---: |
| 64 节点 chain：new 次数 | 715 | 458 |
| 64 节点 chain：累计请求字节 | 340472 | 205384 |
| 64 节点 chain：峰值存活字节 | 239352 | 177736 |
| 256 路扇出：new 次数 | 4630 | 3345 |
| 256 路扇出：累计请求字节 | 1563880 | 1128104 |
| 256 个 timer chain：new 次数 | 6150 | 5382 |
| 256 个 timer chain：累计请求字节 | 2984608 | 3017392 |
| 256 次 yield：累计请求字节 | 662888 | 695736 |

分配次数下降并不保证每项累计字节都下降：timer 和 await 场景的一些 operation / 协程帧变大，表中保留了相应代价。timer 峰值从 2570944 到 2570960 字节，基本持平；本轮所有负载的 remaining 均为零。它们仍不代表 RSS 或完整内存占用。

64 节点链的成功出队次数由 642 降到 578。源发布、等待者通知、节点进入、实际调用及失败传播的协作边界仍保留；诊断负载的 `max_steps_per_poll` 仍为 1。Asio 同一 chain 约为 65 次出队、68 µs，仍比本轮 stdexec 快，恢复粒度也不同。有限次 p99 / max 采样不作为硬时间上限。

本轮头文件 Debug 116 项、module Debug 118 项、两种 sandbox 和报告回归测试通过；两类原生取消竞争测试在两个模式下各重复 100 次。三轮原始数据、固定程序与汇总保存在本机 `build/optimization/`，属于忽略的构建产物。公共比较报告已用无编译负载的第三轮数据刷新。

目前队列是 mutex 保护的 intrusive FIFO，可由多个 producer/consumer 使用。尚未引入第三方队列。候选方案必须先增加相同 producer/consumer 数、批量和唤醒方式的竞争负载；[ReaderWriterQueue](https://github.com/cameron314/readerwriterqueue) 的 SPSC 约束不能直接替换这里的多线程队列，[ConcurrentQueue](https://github.com/cameron314/concurrentqueue) 可作为另一类候选，但仍需测量分配、内存占用、延迟和公平性。

单次 poll 的实测最大值受 OS 抢占影响，有限次采样无法证明硬上限。协作预算测试验证的是停止继续取出、长链分步和通知分步等语义；benchmark 测量这些语义的实际成本，两者各自保留。

### 第二轮

重点消除立即子协程、显式 yield 和链控制中的重复成本。MSVC 19.44 Release，保存第一轮后的固定程序作为本轮前值；停止编译和测试后，前后程序交错运行各 3 轮，取中位数，单位 µs。Asio 对照程序 SHA256 相同。

| 场景 | stdexec 本轮前 | stdexec 当前 | 耗时变化 |
| --- | ---: | ---: | ---: |
| 排空 256 个 ready callback | 5.586 | 5.529 | -1.0% |
| 创建、post 256 次并排空 | 21.545 | 22.363 | +3.8% |
| 64 节点 chain | 152.353 | 54.008 | -64.6% |
| 256 路同 junction 扇出 | 1191.632 | 752.984 | -36.8% |
| 256 个独立 merge 观察者 | 1303.550 | 838.210 | -35.7% |
| 256 个零延迟 timer chain | 2271.910 | 1510.843 | -33.5% |
| 256 个 1ms timer chain | 15083.630 | 15186.525 | +0.7% |
| 256 次立即完成子任务 | 82.193 | 20.626 | -74.9% |
| 256 次显式 yield | 270.323 | 63.139 | -76.6% |
| 64 节点 worker chain | 266.359 | 189.407 | -28.9% |

同期 Asio 的 chain 为 63.073 µs、立即子任务为 9.008 µs、yield 为 97.451 µs。chain 与 yield 的原有倍数差距已消除。纯出队几乎不变，post 增加约 0.8 µs；timer wait 的 0.7% 差异不作明确性能结论。

变化分为三层：

- 用 `Awaitable` 门面传递默认 sticky task 已具备的完成亲和性，避免立即子任务外再加恢复投递。Clang 22 / MS ABI 当时使用返回对象转换兼容分支，不启用这项；其完整头文件契约仍通过。第三轮已用自有返回对象和 promise 替换该门面。
- 当前 scheduler 的显式 yield 只调度一次；实际调用前的 worker 投递也归到一处。测试适配 `testing::yield()` 改成直接返回公共 yield，消除测试额外添加的协程帧；第 9 项收益包含这项适配修正。
- 节点 entry 使用稳定的 `ExecutionSender::Operation`，去掉逐节点 `execute()` 控制协程及相关适配。入口调度失败仍在 control 上发布源结果并通知合流观察者，连接异常也不会留下未完成的源节点。

| 指标 | 本轮前 | 当前 |
| --- | ---: | ---: |
| chain：new 次数 | 458 | 200 |
| chain：累计请求字节 | 205384 | 118344 |
| chain：峰值存活字节 | 177736 | 100056 |
| fanout：new 次数 | 3345 | 2059 |
| timer chain：累计请求字节 | 3017392 | 1890992 |
| inline await：new 次数 | 534 | 270 |
| yield：new 次数 | 1558 | 526 |
| yield：累计请求字节 | 695736 | 143064 |

成功出队次数：chain 578 → 257，inline await 269 → 5，yield 1037 → 261。节点、失败传播、通知、显式 yield 的协作边界保留；inline await 现在可在一次 poll 中推进 256 次内部计数，与任意用户循环本来就需要自行协作的契约一致。

第 8 项剩余差距另做了规模扫描，MSVC Release 三轮中位数：

| 立即子任务数 | Asio (µs) | stdexec (µs) | stdexec new 次数 | stdexec 累计请求字节 |
| --- | ---: | ---: | ---: | ---: |
| 0 | 3.597 | 1.884 | 14 | 7896 |
| 16 | 3.782 | 2.888 | 30 | 10200 |
| 64 | 4.734 | 6.425 | 78 | 17112 |
| 256 | 8.716 | 20.405 | 270 | 44760 |
| 1024 | 24.480 | 76.082 | 1038 | 155352 |

第二轮中，每增加一个只做 `co_return` 的子协程仍新增一次 144 字节的可观察分配；已没有逐子任务的排队。剩余成本包含当时所选 `exec::task` 的帧、默认上下文、创建与销毁。缺少可注入 allocator 是该具体 task 实现与本库复用方式的接口边界；第三轮通过自有 promise 继续优化。尚未用 profiler 精确分摊每个部分的耗时。Asio 在等待期间复用帧，其全局 new 计数不随子任务数量增加；这不代表整个过程没有分配活动。

最终 MSVC 头文件 Debug 123 项、module Debug 125 项、Clang MS ABI 头文件 Debug 123 项及各 sandbox 通过。15 个原生场景在两个 MSVC 模式下各重复 100 次；GCC 头文件编译和 0～1024 子任务扫描通过。Release 比较的全部负载与公共报告回归通过；Release 全量契约套件未重跑。

源码提交依次为 `1192f2a`（第一轮基线）、`e18a832`（亲和性）、`ca42ad9`（单次投递）、`fc76ee0`（Clang 返回对象兼容）、`78c851a`（节点控制状态）。提交只含 stdexec 实现头文件；测试、测试适配、报告和文档继续留在工作区。原始程序、三轮数据、扫描与汇总在本机忽略的 `build/optimization2/`。公共报告已用最终第三轮数据刷新，仍逐项标记双方优胜者。

### 第三轮：自有 Awaitable

`Awaitable` 现在拥有返回对象、promise、帧生命周期及分配钩子，继续通过 stdexec 的公开适配等待 sender、传播 stopped 和连接 receiver。原生 `exec::task` 与它双向互操作，不需要修改第三方源码。

默认帧缓存每线程保留至多 64 KiB 空闲块，另有约 1 KiB 桶元数据。帧头在本机为 16 字节，分桶步长 64 字节，缓存满时淘汰较大的空闲块。自定义 PMR 通过 `FrameAllocatorScope` 在同步创建阶段选择，资源记录在帧头，释放可以发生在别的线程。资源的寿命和线程要求见 [实现说明](../stdexec/README.md#awaitable-与帧分配)。

分配减少后，立即子任务仍有明显的 context 复制成本。同类父子 promise 改成借用父 context；父协程等待期间保持它的寿命。子任务切换 scheduler 时才复制自己的 context，避免修改父 context。外部 promise 接入仍构造本地 context，因为其环境不一定是稳定引用。

MSVC 19.44 Release，基线为 `78c851a`，前后固定程序交错运行各三轮，取中位数，单位 µs。Asio 对照程序 SHA256 相同。

| 场景 | Asio | stdexec 本轮前 | stdexec 本轮后 |
| --- | ---: | ---: | ---: |
| 排空 256 个 ready callback | 47.229 | 5.050 | 5.257 |
| 创建、post 256 次并排空 | 102.278 | 21.653 | 21.542 |
| 64 节点 chain | 64.387 | 53.001 | 52.831 |
| 256 路同 junction 扇出 | 1857.468 | 776.146 | 779.855 |
| 256 个独立 merge 观察者 | 1901.758 | 865.728 | 879.301 |
| 256 个零延迟 timer chain | 1444.440 | 1446.127 | 1478.758 |
| 256 个 1ms timer chain | 15271.370 | 15232.962 | 15213.570 |
| 256 次立即完成子任务 | 8.871 | 20.246 | 8.138 |
| 256 次显式 yield | 95.074 | 62.408 | 50.945 |
| 64 节点 worker chain | 235.190 | 191.138 | 194.398 |

立即子任务耗时下降约 59.8%，yield 下降约 18.4%；chain 接近持平。其余场景没有明确收益，部分略慢；纯出队代码没有变化，其约 0.2 µs 差异也提示了测量波动。公共比较报告仍使用同一项为行、Asio/stdexec 相邻列及该项 winner；该报告展示最终第三次运行，以上表格展示三轮中位数。

| 场景 | 可观察 new 次数：前 → 后 | 累计请求字节：前 → 后 |
| --- | ---: | ---: |
| chain | 200 → 161 | 118344 → 62600 |
| fanout / observers | 2059 → 1932 | 679256 → 683752 |
| timer chain | 3078 → 2956 | 1890992 → 1967408 |
| inline await | 270 → 13 | 44760 → 7352 |
| yield | 526 → 269 | 143064 → 44216 |
| worker chain | 202 → 200 | 118392 → 124216 |

分配次数减少不代表每类负载的申请字节都下降。fanout 约增加 0.7%，timer 约增加 4.0%，worker 约增加 4.9%。chain 的可观察峰值由 100056 降至 44312 字节，测量结束仍有 43264 字节来自空闲缓存。`new_remaining_bytes` 可以包含缓存原始存储；对象和参数已经析构。统计只观察 C++ new 请求，不是 RSS 或全部分配，预热阶段留下的块在后续复用时不会再次计入。

立即子任务另做相同规模扫描，三轮中位数：

| 子任务数 | Asio (µs) | stdexec (µs) | stdexec new 次数 | stdexec 累计请求字节 |
| --- | ---: | ---: | ---: | ---: |
| 0 | 3.696 | 1.722 | 10 | 1336 |
| 16 | 3.726 | 2.070 | 10 | 1336 |
| 64 | 4.833 | 3.303 | 10 | 1336 |
| 256 | 9.027 | 8.386 | 10 | 1336 |
| 1024 | 25.065 | 28.049 | 10 | 1336 |

预热后，增加子任务不再增加可观察的 new 请求；每个协程的创建、返回及帧回收仍有成本。独立扫描和整个负载集合之间的缓存起点不同，因此它的固定次数 10 与集合中的 inline await 13 有差异。1024 个子任务时 stdexec 仍略慢，不能把 256 个子任务的结果外推到所有规模。

本轮没有减少协作式出队边界。chain / inline await / yield 出队次数分别保持 257 / 5 / 261；节点、通知、失败传播及显式 yield 的 `max_steps_per_poll` 仍为 1，立即子任务内部计数仍可一次推进 256。

生命周期回归还发现并修复了通知与 timer 的取消 callback 注销顺序：最终 receiver 可以立即销毁 stop source，callback 必须先注销。预取消 token 的构造期间回调由注册 gate 延后处理。四种直接 receiver 场景使这个问题可以确定性复现，不依赖调度碰巧触发断言。

最终验证为 MSVC 头文件 Debug 137 / Release 133、模块 Debug 139、Clang 22 MS ABI ASan 137 及各自 sandbox；30 个聚焦场景在模块模式重复 100 次，ASan 重复 20 次。GCC 16 完成头文件编译与 0～1024 子任务扫描，未执行其完整契约套件。报告回归通过，本轮未重跑安装消费矩阵。

源码提交为 `727d472`（自有 promise）、`4c387c9`（终结前注销 callback）、`df172f8`（帧缓存和 PMR 入口）、`7b4523e`（紧凑帧头与分桶）、`492dddc`（缓存满时淘汰）、`17fb189`（借用父 context）。提交只含 stdexec 实现头文件；测试、CMake、报告和文档未提交。原始程序、三轮测量、上下文借用前后扫描及汇总在忽略的 `build/optimization3/`。

### execution 分层验证

stdexec 的实际类型与执行设施现位于 `execution`，`core` 提供类型别名和操作引入。公共接口套件及 sandbox 继续使用 `core` / `task`；原生 sender 场景直接通过 `<async/execution.h>` 使用执行基础层，验证两组入口在同一套任务编排中互通。

本次分层及 core 补齐通过 MSVC 19.44 头文件 Debug：Asio 108 项、stdexec 145 项；VS 2026 stdexec 模块 Debug 149 项，以及各自 sandbox。新增八个公共 core 场景，覆盖 post/yield 的排队边界、绑定及未绑定 executor 的通知广播、并发注册、单个等待者取消及 delay 取消。测试 Runtime 直接使用库的公共操作，不再自行实现 Asio 的 yield、delay 或 Gate。模块套件还增加了帧分配辅助名称和内部队列未导出的两个检查；跨翻译单元函数分别以原生 Executor 和 core 别名声明，验证类型身份。

另从本地依赖包配置并安装两种头文件库，通过干净安装前缀的 `find_package(LCFAsync)` 构建三个 Release 程序：Asio、stdexec 的 core / task 消费样例，以及只包含 `<async/execution.h>`、使用原生 Context、Awaitable、yield 和 delay 的程序。三者均得到结果 42，执行基础层不依赖 core 或 task 头文件。验证脚本与日志位于忽略的 `build/check_execution*.ps1`、`build/core-*.log`。

Asio 模块接口自身能生成，但完整消费测试仍被已有的 MSVC C1116 阻断，诊断涉及 `asio::config_service` 的 IFC 导入；本次没有宣称 Asio 模块完整通过。其默认头文件配置已完成上述公共验证。

本次只拆分源码职责，没有调整调度边界、分配策略或错误传播，也没有重新采样性能；上面的性能表保留其原测量范围。

## 2026-10-20 原生执行与结构整理（第一阶段历史）

以下记录第一阶段，当时仍保留旧编排接口；第二阶段已经删除 task，当前状态见本文开头和解耦说明。

测试仍保留旧 Chain/Junction 契约；新增 execution_test.cpp 使用 NativeApi.h，仅经 async.h 接入新能力，不靠 task。覆盖算法对象身份、惰性启动、多 worker/control 线程归属、值/错误/stopped 通道、内联 then 与显式队列边界，以及 split 的一次执行、不可复制结果、共享身份和晚订阅缓存。

```powershell
cmake --preset header-msvc
cmake --build --preset header-msvc-debug
ctest --preset header-msvc-debug
cmake --build --preset header-msvc-release --target async_stdexec_native_benchmark
cmake --build --preset header-msvc-release --target async_compare
```

开发 preset 在主构建开启全部测试与 benchmark，配置可获取依赖；消费仍可选择延迟子构建。sandbox 还运行独立 native_example，utility target 会先构建该程序。

原生测量输出位于 stdexec/tests/benchmark/results/native/<配置>：timing.json 测量包含 Context 建立、图连接和运行的完整小负载；diagnostics.csv 独立记录 poll 分位数/最大值、回调数、每次最大步骤以及 new 次数、字节和峰值。内联三步与三次显式调度分开报告，测量使用原生算法，不把它们作为与旧 Chain 等价的后端胜负项。分配统计只覆盖 C++ new 请求，包含该次 Context 建立和销毁，不表示 RSS。

MSVC 19.51、Clang 22.1 的 header 公共及后端测试通过；GCC 16.1 的现有协程编排测试有堆损坏，相同内存资源用例在重构前备份也复现。原生 sender 路径单独验证，不能把其成功解释为旧接口全部通过。

还验证了 benchmark-only 不引入 GoogleTest、父选项关闭后恢复局部选择、延迟 sandbox 与 CTest JSON 发现、两后端安装/迁移前缀消费、源码/FETCH/PROVIDED 及集合入口。旧性能表保留原来的测量日期与语义，本轮未据新结构宣称性能优化。

模块测试改用显式 import 和同模式跨翻译单元交换。stdexec 的 MSVC 单后端模块验证通过；完整双后端模块矩阵有编译器问题，本轮按用户决定暂停，不提供公开 module preset。

## 本轮新增边界测试

standalone 专属测试包括 sender 同步值/错误、多参数和 move-only、异步完成与 await_suspend 竞争、跨 worker 后恢复 scheduler、嵌套显式调度恢复、停止释放嵌套帧、终态 receiver 立即销毁根操作，以及 receiver 构造抛出时的帧所有权。公共 timer 用例还验证等待时长从 start 计时；本轮修正了自有 timer 先前从 connect 计时的问题。

mechanisms 测试目标的编译命令不包含 stdexec/Asio include 路径。额外检查头文件依赖闭包和各后端显式源清单；standalone 自有协议/算法与本轮 Asio 源码相同，但独立存放。实现边界见[解耦说明](../docs/backend-decoupling.md)。

报告 fixture 同时覆盖两个/三个后端、任意已选后端对、第三实现获胜、最小值并列、缺失与失败数据，以及指标相邻列和大整数分配记录。报告不预设哪个后端更优。
