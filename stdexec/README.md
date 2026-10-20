# stdexec async

该后端使用自有 Context/Scheduler 运行原生 stdexec 算法，不依赖 Asio。入口为 async.h，基础设施位于 lcf::async，协议与执行算法位于 lcf::async::exec；stde 保留对原生 stdexec 的别名。旧 task/、task.h、Chain/Junction 已删除。

execution.h 集中 using 原生 just、then、let_value、when_all、on、starts_on、continues_on、sync_wait、split 及恢复算法。execution/ 仅放本库的 from_result/retry 与环境注入扩展；没有复制原生算法。公共用法和边界见[执行模型](../docs/asio-execution.md)，运行示例见[native_example.cpp](tests/sandbox/native_example.cpp)。

## 运行设施

core/scheduling 提供 Context、Scheduler 和普通回调 Executor。Context::getScheduler() 用于执行图；getExecutor() 保留普通回调和协程适配能力。

| 操作 | 含义 |
| --- | --- |
| pollOne() | 非阻塞执行至多一个队列回调 |
| poll(PollBudget) | 在下一次取出前检查数量/时间预算 |
| getReadyCount() | 当前就绪队列数量快照 |
| run() | 阻塞推进直到 stop，适合 worker |
| runUntilIdle() | 排空队列并考虑显式登记工作，不自动追踪任意原生图 |
| stop()/restart() | 暂停/恢复出队，不代替停止令牌 |

连续 then 可以在一次队列回调内完成；需要步骤边界时显式 continues_on/schedule。原生 split 的一批订阅通知不保证逐个排队。Signal(executor) 保留自身的逐个通知实现；无 executor 的 Signal 可在发布线程直接完成。

async::as_sender(scheduler, source) 在指定 scheduler 启动原生 sender 或本库 Awaitable。schedule_after 在 timer 完成后把交付迁移至指定 scheduler。timer 与 Signal 的主动取消现在走 stopped 通道，不再折叠成 operation_canceled 错误。

## 调度与分配成本

普通执行图直接使用原生 operation state，旧 InvocationSender、ExecutionSender、Delivery 等 task 辅助已删除。现有 Executor 的内置队列快路径和原生 scheduler 适配保留；没有引入队列依赖。

Awaitable 拥有本库的返回对象、promise 和帧生命周期，仍复用 stdexec 的 sender 等待、上下文和退出恢复辅助。普通父子协程借用父上下文，切换 scheduler 时复制；co_return 保存结果，final_suspend 转移 continuation，await_resume 移出结果或抛出异常并销毁子帧。与原生 exec::task 的双向互操作测试保留。

默认帧缓存按 64 字节分桶，每线程最多保留 64 KiB 空闲块；大帧和过对齐块直接分配。FrameAllocatorScope 选择自定义 PMR 资源，资源须活到帧销毁。当前 WinLibs GCC 的 TLS 析构顺序有独立复现的运行库问题，不能认为创建线程退出等默认缓存契约在该环境已经通过。

历史 Chain 性能优化记录仍在[测试说明](../tests/README.md)，与当前 sender 负载分开阅读，不用新报告反推旧链的性能变化。

## 开发与独立实现

从 async 根目录选择 header-msvc/header-clang/header-gcc，专属 verify/sandbox/benchmark 目标使用 async_stdexec_ 前缀；所有 provider 和延迟构建选项保留。

模块开发选择 module-stdexec-clang。Clang 22.1.1 的 Debug/Release 各 81 项测试、公共及原生 benchmark、安装迁移后的独立纯 import 消费均通过。直接导入程序为 async_stdexec_module_example。

模块接口只声明非模板协程 yield，由 src/yield.cpp 包含唯一一份定义并生成代码；头文件模式继续包含同一份 inline 定义。这样绕过 Clang 在消费者重复生成该协程相关代码时的崩溃，调用形式和行为不变。MSVC 19.51 的原生薄模块及无第三方的推导指引复现仍失败，暂不提供 stdexec/MSVC 模块 preset，详见[模块说明](../docs/cpp-modules-transition.md)。

本轮在本目录内部拆出了纯标准库机制：queue、CallbackExecutor、TimerService、notification_state、ReturnStorage、frame 与 StopRegistrationGate。独立 mechanisms 测试目标不链接 async_stdexec，也不获得 stdexec 的 include 路径。Scheduler、timer/signal 注册以及 Awaitable 的原生互操作仍明确属于 stdexec 适配。

standalone/ 已独立接合这些机制与自有协议/算法，并补齐自有 sender 等待、continuation、上下文和停止传播。两边源码各自维护，没有 common 运行实现；本后端继续直接导出原生组合算法，详见[解耦说明](../docs/backend-decoupling.md)。
