# Async Core MVP 开发计划

下面的完整示例是本轮已确认的验收目标；后文按它拆分协议和实现。本目录的 `include/async/` 和 [example.cpp](example.cpp) 保存参考实现与主示例，文中实现文件路径以本目录为基准。项目根目录下的正式 `include/async/` 仍是声明骨架，参考代码须接入项目后再作为公共 API 使用。`.mvp` 是纳入版本控制的临时开发材料，完成正式 MVP 后可整体删除。

## 1. 最终验收示例

主线程驱动 control Executor；调用方子线程作为唯一 CPU producer，将两个逻辑队列上的独立 sender 分支提交给共享 SPMC CPU Executor，由多个 Worker 执行并经 `when_all` 汇合；最终 Receiver 返回调用方线程。Worker 数是 Context 的资源预算，业务代码只选择 LogicalQueue，不指定 Worker，也不需要同步原语协调任务。并行度 gate 只用于 verify 测试。此 MVP 不使用 C++ coroutine。

```cpp
#include <async/async.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace async = lcf::async;
namespace ex = async::exec;
namespace proto = async::protocols;
using namespace std::chrono_literals;

// Only the caller thread starts CPU operations in this example.
using ExampleContext = async::BasicContext<async::BasicExecutor<>,
    async::BasicExecutor<async::FastProducerSpmcInbox>>;

struct BranchResult
{
    int m_value;
    std::thread::id m_worker_thread;
};

struct GraphResult
{
    int m_value;
    std::thread::id m_left_thread;
    std::thread::id m_right_thread;
    std::thread::id m_control_thread;
};

enum class Completion
{
    ePending,
    eValue,
    eError,
    eStopped,
};

struct Outcome
{
    Completion m_completion = Completion::ePending;
    std::optional<GraphResult> m_result;
    std::thread::id m_caller_thread;
    std::thread::id m_receiver_thread;
};

struct Receiver
{
    using receiver_concept = proto::receiver_t;

    void set_value(GraphResult result) && noexcept
    {
        this->finish(Completion::eValue, std::move(result));
    }

    template <typename Error>
    void set_error(Error &&) && noexcept
    {
        this->finish(Completion::eError);
    }

    void set_stopped() && noexcept
    {
        this->finish(Completion::eStopped);
    }

    ExampleContext * m_context_p;
    Outcome * m_outcome_p;
    bool * m_caller_completed_p;
    std::thread::id m_caller_thread;

private:
    void finish(
        Completion completion,
        std::optional<GraphResult> result = std::nullopt) noexcept
    {
        // All three completion channels must run on the caller thread.
        if (std::this_thread::get_id() != m_caller_thread) {
            std::terminate();
        }

        m_outcome_p->m_completion = completion;
        m_outcome_p->m_result = std::move(result);
        m_outcome_p->m_receiver_thread = std::this_thread::get_id();

        *m_caller_completed_p = true;
        m_context_p->requestStop();
    }
};

int main()
{
    constexpr async::WorkerCount k_worker_count = 2;
    constexpr std::size_t k_queue_count = 3;
    const auto main_thread = std::this_thread::get_id();

    async::ContextInfo info{k_worker_count};

    const auto control_token = info.addControlQueue();
    const auto left_token = info.addCpuQueue();
    const auto right_token = info.addCpuQueue();

    constexpr auto k_storage = ExampleContext::storageRequirements(k_worker_count, k_queue_count);
    alignas(k_storage.m_workers.m_alignment)
        std::array<std::byte, k_storage.m_workers.m_bytes> worker_bytes;
    alignas(k_storage.m_routes.m_alignment)
        std::array<std::byte, k_storage.m_routes.m_bytes> route_bytes;
    std::pmr::monotonic_buffer_resource worker_arena{
        worker_bytes.data(), worker_bytes.size(), std::pmr::null_memory_resource()};
    std::pmr::monotonic_buffer_resource route_arena{
        route_bytes.data(), route_bytes.size(), std::pmr::null_memory_resource()};

    ExampleContext context{std::move(info), async::ContextAllocators{
        std::pmr::polymorphic_allocator<std::byte>{&worker_arena},
        std::pmr::polymorphic_allocator<std::byte>{&route_arena}}};

    auto control_scheduler = context.getSchedulerHandle(control_token);
    auto left_scheduler = context.getSchedulerHandle(left_token);
    auto right_scheduler = context.getSchedulerHandle(right_token);

    Outcome outcome;

    std::thread caller;
    try {
        caller = std::thread{[&] {
            const auto caller_thread = std::this_thread::get_id();
            outcome.m_caller_thread = caller_thread;

            // The caller owns and drives this return executor independently of Context.
            async::BasicExecutor<> caller_executor;
            async::LogicalQueue caller_queue{caller_executor};

            auto caller_scheduler = async::SchedulerHandle{caller_queue};
            auto left = ex::schedule(left_scheduler)
                | ex::then([] {
                    std::this_thread::sleep_for(1s);
                    return BranchResult{20, std::this_thread::get_id()};
                });

            auto right = ex::schedule(right_scheduler)
                | ex::then([] {
                    std::this_thread::sleep_for(1s);
                    return BranchResult{22, std::this_thread::get_id()};
                });

            auto graph = ex::when_all(std::move(left), std::move(right))
                | ex::continues_on(control_scheduler)
                | ex::then([](BranchResult lhs, BranchResult rhs) noexcept {
                    // Context's owner thread consumes the merged values.
                    return GraphResult{
                        lhs.m_value + rhs.m_value,
                        lhs.m_worker_thread,
                        rhs.m_worker_thread,
                        std::this_thread::get_id()};
                })
                | ex::continues_on(caller_scheduler);

            bool caller_completed = false;
            auto operation = ex::connect(
                std::move(graph),
                Receiver{
                    &context,
                    &outcome,
                    &caller_completed,
                    caller_thread});

            ex::start(operation);

            std::size_t heartbeat_count = 0;

            while (not caller_completed) {
                std::cout << "caller heartbeat: "
                          << ++heartbeat_count << '\n';

                caller_executor.pollOne();

                if (not caller_completed) {
                    std::this_thread::sleep_for(100ms);
                }
            }

            caller_executor.close();
        }};
    } catch (...) {
        // No root was submitted if launching the caller thread failed.
        context.close();
        context.joinWorkers();
        throw;
    }

    // Wait for work internally; the final Receiver requests this run to stop.
    context.run();

    caller.join();
    context.close();
    context.joinWorkers();

    if (outcome.m_completion != Completion::eValue
        || not outcome.m_result) {
        std::cerr << "work completed with error or stopped\n";
        return 1;
    }

    const auto & result = *outcome.m_result;
    const auto caller_thread = outcome.m_caller_thread;

    const bool passed =
        result.m_value == 42
        && caller_thread != main_thread
        && outcome.m_receiver_thread == caller_thread
        && result.m_control_thread == main_thread
        && result.m_left_thread != result.m_right_thread
        && result.m_left_thread != main_thread
        && result.m_right_thread != main_thread
        && result.m_left_thread != caller_thread
        && result.m_right_thread != caller_thread;

    std::cout << "result: " << result.m_value << '\n';
    std::cout << "acceptance: " << (passed ? "passed" : "failed") << '\n';
    return passed ? 0 : 1;
}
```

```text
调用方子线程 connect/start
    ├─ Worker 0：sleep 1s → 20
    └─ Worker 1：sleep 1s → 22
                    ↓ when_all
主线程 context.run() → then：20 + 22
                    ↓ continues_on(caller_scheduler)
调用方心跳循环 pollOne() → Receiver → context.requestStop()
                    ↓
主线程 run() 返回 → join 调用方 → close/join Context
```

`caller_completed` 只在调用方线程读写，因此是普通 bool；主线程通过 `requestStop` 的线程安全唤醒协议退出，通过 join 后的同步关系读取 Outcome。两个 sleep 只模拟耗时，不是协程挂起，也不能用总耗时推断并行正确性。调用方 100ms 的 sleep 是宿主循环选择，会增加结果处理延迟；主线程的 run 不使用固定 sleep/yield 循环。

## 2. 范围、执行域与关闭契约

### 2.1 本轮包含的能力

| MVP 包含 | 后续能力 |
| --- | --- |
| 宿主驱动的 control Executor、N 个 Worker 共享 CPU Executor | Worker 动态增删、work stealing、异构执行器与扩展映射 |
| 调用方直接拥有和驱动返回 Executor；复用同一 scheduler adapter | 多种外部 render/gfx executor 的通用注册与 Context 借用 API |
| control 使用 MutexInbox MPMC FIFO；CPU 使用 FastProducerSpmcInbox 单 producer/多 Worker consumer | 多 producer 的独立 SPMC lane、公平汇合、可配置等待策略和其他 Queue 后端 |
| 稳定逻辑身份，route 只选择 control/CPU Executor，不保存 Worker 下标 | QueuePolicy 的优先级、公平性、串行/并发上限、独立容量和隔离 |
| Core record/executor concepts、CPO；execution scheduler/sender/operation adapter | Task、timer、取消 registration、expected 扩展与 coroutine frame 策略 |
| 显式 PMR、内嵌 operation/node、构造回滚、关闭/drain/join | receiver allocator 覆盖、无异常 typed storage 和资源失败交付 |

### 2.2 Executors 与 Workers 的关系

| 执行域 | 所有者及驱动者 | 工作 |
| --- | --- | --- |
| control Executor | Context 拥有；宿主 owner 调用 run/poll | 消费合流值并执行 control queue 上的工作 |
| CPU Executor | Context 拥有；N 个 BasicWorker 共享并发消费 | 从共享队列取 CPU 工作；任意可用 Worker 可执行任一 CPU LogicalQueue 的工作 |
| 调用方返回 Executor | 调用方子线程创建、驱动、销毁 | 通过独立 MutexInbox 执行最终 Receiver |

Worker 数量与逻辑队列数量彼此独立。一个 CPU Executor 可以由多个 Worker 同时调用 waitOne；Queue 必须支持多个并发消费者，Executor 通过 `supports_multiple_workers` 声明该能力。control 与调用方返回 Executor 各由唯一 owner 驱动。Worker 不选择逻辑队列、不承载优先级，也不将任务绑定到自己的线程 ID；任务选择和可执行性由 Executor/QueuePolicy 决定。

默认 Context 的 control/CPU Queue 都使用 MutexInbox，支持多 producer。本例显式定义 ExampleContext，以 MutexInbox 接收 control 工作、FastProducerSpmcInbox 接收 CPU 工作，caller 是唯一 CPU producer。SPMC Queue 是侵入式 mailbox，节点由稳定 operation 提供；enqueue 不分配、不获取 consumer mutex，producer 以固定次数原子操作交换 head 并链接前驱。多个 Worker 在 consumer mutex 下领取，彼此可能等待。发布缺口使 consumer 返回空，发布完成时的 atomic wait 序号唤醒 Worker。close 阻止新提交并等待在途提交完成；Worker 必须等 producer 不再发布且队列为空才退出。调用方返回 Executor 使用默认 MutexInbox，由 control owner 投递。

这是 producer 路径固定次数原子操作、不等待 Worker 队列锁的契约，不是通用硬实时墙钟上界：atomic notification、平台调度、中断、cache/TLB 与被执行 record 都影响实际延迟。consumer 之间领取时会使用 mutex。验收分开测量 enqueue、consumer 争用、no-op dispatch 和 receiver 执行，不把用户代码耗时算作 queue 成本。

Context 不创建、识别或接管进程主线程。主线程归属由宿主指定且保持；poll/run/requestStop 只驱动或停止 control Executor；close/empty 覆盖自有 control/CPU Executor，不统计或关闭调用方的独立返回 Executor。SchedulerHandle 不拥有其引用的 LogicalQueue 或 Executor。

### 2.3 run、requestStop、close、join 的区别

| 操作 | 契约 |
| --- | --- |
| `Context::run()` | 当前线程驱动控制 executor；暂时空队列时阻塞等待；收到停止请求后在 record 边界返回；没有停止请求时，关闭且排空后返回 |
| `Context::requestStop()` | 可由任意线程调用，记录持久停止请求并唤醒控制 executor；不关闭准入、不取消 root、不丢弃排队节点，不停止 Worker |
| `Context::pollOne()` / `poll(PollBudget)` | 控制 executor 的非等待驱动；停止 run 后仍可用来显式排空 |
| `Context::close()` | 关闭自有 executors 的新准入并唤醒等待者；不执行 callback，不关闭外部返回 executor |
| `Context::joinWorkers()` | 等待自有 Worker 退出；调用前应 close，且调用线程不能是其中的 Worker |

停止请求在 run 开始前发出也有效；与入睡/入队竞争不能丢失。请求在 callback 执行中到达时，run 等该 record 返回后退出，不抢占函数。默认 queue 以锁内取出节点作为 record 开始的线性化点：停止请求先于取出则不取下一条；已经取出的 record 仍执行至返回。剩余工作仍在 queue 中，必须显式 poll/drain 或由仍在工作的 owner 推进。MVP 的停止状态不重置，再次 run 立即返回；restart 后置。没有停止请求时，close 后的 wait 驱动继续取出已接纳节点，关闭且为空才返回。

默认等待使用与 queue 状态相同 mutex 保护的 predicate 和 condition variable；仅 notify 而不记录状态会丢唤醒。将来可替换等待策略，timer 到来后再加入最近 deadline；具体自旋/等待算法由 benchmark 决定，不把隐藏的固定 sleep 当作普遍最优选择。

### 2.4 完成线程及存活顺序

`connect` 构造稳定 operation，`start` 启动它；只有本例的 schedule operation 在 start 时投递，`just` 等 sender 可以同步完成。Receiver 自身不选择线程。`continues_on` 存储上游 completion 并调度目标操作，目标 owner 执行该操作时再调用下游；`get_completion_scheduler` 描述保证，查询本身不迁移。

正常验收保持目标 executors 准入，使用不抛异常的 value/error 存储和移动，因此最后的 `continues_on(caller_scheduler)` 把上游 value/error/stopped 都交给调用方线程。返回 scheduler 拒收或 completion 存储/转移抛异常是独立失败边界，不能假定失败仍在目标线程交付；另测恰好一次完成，不在来源线程访问 owner 专有状态。

本例只有一个 root。所有 executors 保持有效并被驱动 → 最终 Receiver 写入 Outcome/调用方 bool 并请求停止 → 主线程 run 返回后 join 调用方 → 调用方 pollOne 返回，operation/route/executor/resource 可销毁 → Context close/join workers → 释放 Context 和资源。run 返回不等于 Receiver 已经返回，不能省掉 join。多个 root 时由 root owner 计数，最后一个 root 才请求停止；空队列、一个 Receiver 完成或 requestStop 本身都不能代替 root drain。

析构不运行用户 callback，也不偷偷阻塞 drain/join。Context/Worker 要求宿主事先完成显式关闭、join 和最后访问；线程创建中途失败则由构造函数回滚已创建线程。进程超时用于检测死锁，不是销毁仍被异步访问的 operation 的许可。

## 3. 文件、依赖与命名

Core 公共类、concept、CPO、配置值直接在 `lcf::async`，`core/` 只是目录。标准 execution 的标记类型、概念和完成签名在 `lcf::async::protocols`；操作 CPO、查询接口和组合算法在 `lcf::async::exec`。标准概念的 `_c` 转发别名属于 Core，位于 `core/concepts.h`，命名空间仍为 `lcf::async`；execution 不依赖这些别名。普通类型采用 PascalCase，成员函数 camelCase，自由函数/参数 snake_case，concept 为 snake_case_c，CPO 标签/对象沿用 snake_case_t / snake_case。仅实现辅助类型使用局部 details；不依赖继承建立组件关系；friend 仅用于限制 token 签发这一必要权限。

```text
include/async/
├── config.h                 # 配置聚合入口
├── config/
│   ├── macros.h             # LCF_ASYNC_EXPORT 等公共宏，不加载 provider
│   ├── features.h           # 能力检测和配置默认值，显式定义优先
│   └── execution_provider.h # 加载选定 provider 并定义 async::stde
├── protocol.h               # 协议聚合入口，只包含下列四个头
├── protocols/              # async::protocols：各角色需要满足的契约词汇
│   ├── sender.h
│   ├── receiver.h
│   ├── operation_state.h
│   └── scheduler.h
├── core.h
├── core/
│   ├── concepts.h           # Core 接口约束及标准 execution 概念的 _c 转发
│   ├── thread_types.h       # ThreadEntry 基础类型，供 concepts 与 scheduling 共用
│   ├── enums.h              # EnqueueResult
│   ├── operations.h         # get_schedule_node / try_enqueue CPO
│   ├── scheduling/
│   │   ├── ScheduleNode.h
│   │   ├── queue/MutexInbox.h
│   │   ├── queue/FastProducerSpmcInbox.h
│   │   ├── details/ContextExecutorRoute.h
│   │   ├── BasicExecutor.h
│   │   ├── BasicWorker.h
│   │   ├── poll_structs.h
│   │   ├── LogicalQueue.h
│   │   ├── SchedulerHandle.h
│   │   └── details/exec_schedule_adapters.h
│   ├── details/containers/FixedCapacityVector.h
│   └── context/{ContextInfo.h,BasicContext.h}
├── execution.h              # 标准协议、操作、查询和组合算法的聚合入口
├── execution/              # async::exec
│   ├── operations.h        # connect/start/schedule、完成通知 CPO
│   ├── queries.h           # get_env/get_* 查询及其结果类型
│   ├── factories.h         # just/just_error/just_stopped
│   ├── continuations.h     # then/upon_*/let_* 完成接续
│   ├── composition.h       # when_all 合流
│   ├── scheduling.h        # starts_on/continues_on/on 调度切换
│   └── sync_wait.h         # async::this_thread::sync_wait
└── async.h                  # 完整聚合入口
```

| 层 | 允许的依赖 |
| --- | --- |
| config.h | 聚合 macros/features/execution_provider 子头；包含它仍加载 provider，不直接实现配置逻辑 |
| config/macros.h、config/features.h | 公共导出宏 / 能力检测和配置默认值；调用方或 CMake 的显式设置优先，不加载 provider |
| config/execution_provider.h | 加载 provider 并定义 async::stde；依赖 macros/features，不依赖 Core、protocols 或项目 execution 层 |
| protocol.h / protocols/ | 按 sender/receiver/operation_state/scheduler 拆分的标准标记、概念和完成签名；直接依赖 config/execution_provider.h，不依赖项目 execution 层或 Core |
| execution/operations.h、execution/queries.h | 分别重导出操作 CPO 和环境查询；直接依赖 config/execution_provider.h，不包含 Core 或组合算法聚合入口 |
| execution/factories.h、continuations.h、composition.h、scheduling.h | 按工厂、完成接续、合流、调度切换分类重导出算法；各自直接依赖 config/execution_provider.h，Core 不包含这些算法头 |
| core/concepts.h | 在 async 定义 Core 接口约束和标准概念的 _c 转发；依赖 enums.h、thread_types.h 与 protocol.h，因此加载 provider；不依赖 scheduling，execution 不反向包含它 |
| Core enums/operations | 分别保存接纳结果、运行时提交 CPO；operations 依赖 concepts；Queue 约束使用泛型 Node |
| core/details/containers | 管理元素生命周期、容量和访问；FixedCapacityVector 用显式 typed allocator，不依赖 Context 或 execution；资源策略属于 memory |
| PollBudget、PollReport | scheduling/poll_structs.h 中的批量驱动包装值类型；由 Context 直接包含，不属于协议入口或 concepts |
| ThreadEntry | core/thread_types.h 中的线程入口函数类型；Core concepts 与 scheduling 共用，不依赖 scheduling |
| ScheduleNode、Queue、Executor、Worker | 各自需要的基础协议；经 concepts.h 加载 provider；Worker 不包含具体 Executor，queue 不包含 Context 或 execution 算法 |
| LogicalQueue | Core 提交协议和稳定路由身份；经 operations/concepts 使用标准词汇，不依赖 execution 算法 |
| BasicContext | 组合 executors、workers、固定 routes 和资源；直接返回标准 SchedulerHandle |
| execution.h | 聚合标准协议、操作、查询和组合算法；不包含 Core，重导出保持 provider 身份 |
| Core SchedulerHandle、scheduling/details/exec_schedule_adapters.h | Core 基础提交接口、Core 概念别名及标准操作/查询头；句柄直接实现 scheduler，内部完成 sender/operation 适配 |

公共 config.h 只作配置聚合。config/features.h 负责能力检测及 `LCF_ASYNC_USE_STD_EXECUTION` 的默认值，config/execution_provider.h 加载选定 provider，并在 lcf::async 定义 stde 别名；protocols 和 execution 的基础接口分别直接依赖 provider 子头。Core 的运行时约束与标准概念转发统一位于 concepts.h，因此依赖它的组件也加载 provider；固定存储容器及仅需 macros/features 的头仍可独立使用。跨目录引用统一写成 `<async/...>`，同目录引用可保留相对文件名。定义按 concepts/enums/operations 分类，不增加没有消费需求的策略参数。

## 4. Concepts、枚举与提交入口

### 4.1 Record 和 executor

Record 是 adapter/宿主自己拥有的 operation，不能要求继承固定 ScheduleRecord。MVP 只支持成员定制：concept 直接检查成员接口，CPO 复用 concept 的约束并转发调用。提交能力由二元 `executor_c<Executor, Record>` 描述；单步驱动能力由 `work_executor_c` 描述；Worker pool 还要求 `multi_worker_executor_c`，即 Executor 明确声明可由多个 Worker 并发等待和取出工作。

`include/async/core/enums.h`：

```cpp
#pragma once

namespace lcf::async {

enum class EnqueueResult
{
    eAccepted,
    eClosed,
};

} // namespace lcf::async
```

`include/async/core/concepts.h`：

```cpp
#pragma once

#include "enums.h"
#include <async/protocol.h>
#include <async/core/thread_types.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace lcf::async {

template <typename Type>
concept sender_c = protocols::sender<Type>;

template <typename Type>
concept receiver_c = protocols::receiver<Type>;

template <typename Type>
concept scheduler_c = protocols::scheduler<Type>;

template <typename Type>
concept operation_state_c = protocols::operation_state<Type>;

template <typename Receiver, typename Completions>
concept receiver_of_c = protocols::receiver_of<Receiver, Completions>;

template <typename Node>
concept schedule_node_c = requires(Node & node) {
    { node.execute() } noexcept -> std::same_as<void>;
};

template <typename Record>
concept schedule_record_c = requires(Record & record) {
    { record.getScheduleNode() } noexcept;
    requires std::is_lvalue_reference_v<decltype(record.getScheduleNode())>;
    requires schedule_node_c<std::remove_reference_t<decltype(record.getScheduleNode())>>;
};

template <typename Executor, typename Record>
concept executor_c = schedule_record_c<Record> and
    requires(Executor & executor, Record & record) {
        { executor.tryEnqueue(record) } noexcept -> std::same_as<EnqueueResult>;
    };

template <typename Executor>
concept work_executor_c = requires(Executor & executor, const Executor & const_executor) {
    { executor.pollOne() } noexcept -> std::same_as<bool>;
    { executor.close() } noexcept -> std::same_as<void>;
    { const_executor.empty() } noexcept -> std::same_as<bool>;
};

template <typename Executor>
concept blocking_work_executor_c = work_executor_c<Executor> and
    requires(Executor & executor) {
        { executor.waitOne() } noexcept -> std::same_as<bool>;
    };

template <typename Executor>
concept multi_worker_executor_c = blocking_work_executor_c<Executor>
    && requires {
        requires Executor::supports_multiple_workers;
    };

template <typename Executor>
concept stoppable_work_executor_c = blocking_work_executor_c<Executor> and
    requires(Executor & executor) {
        { executor.requestStop() } noexcept -> std::same_as<void>;
    };

template <typename Factory, typename ControlExecutor, typename CpuExecutor>
concept work_executor_factory_c = work_executor_c<ControlExecutor>
    and work_executor_c<CpuExecutor> and
    requires(Factory & factory) {
        { factory.makeControlExecutor() } -> std::same_as<ControlExecutor>;
        { factory.makeCpuExecutor() } -> std::same_as<CpuExecutor>;
    };

template <typename Queue>
concept schedule_queue_c = requires { typename Queue::Node; } and
    schedule_node_c<typename Queue::Node> and
    requires(Queue & queue, const Queue & const_queue, typename Queue::Node & node) {
        { queue.tryEnqueue(node) } noexcept -> std::same_as<EnqueueResult>;
        { queue.tryPop() } noexcept -> std::same_as<typename Queue::Node *>;
        { queue.close() } noexcept -> std::same_as<void>;
        { const_queue.empty() } noexcept -> std::same_as<bool>;
    };

template <typename Queue>
concept blocking_schedule_queue_c = schedule_queue_c<Queue> and
    requires(Queue & queue) {
        { queue.waitPop() } noexcept -> std::same_as<typename Queue::Node *>;
    };

template <typename Queue>
concept stoppable_schedule_queue_c = blocking_schedule_queue_c<Queue> and
    requires(Queue & queue) {
        { queue.requestStop() } noexcept -> std::same_as<void>;
    };

template <typename Thread>
concept thread_c = std::destructible<Thread> and
    requires(Thread & thread, const Thread & const_thread) {
        { const_thread.joinable() } noexcept -> std::same_as<bool>;
        { thread.join() } -> std::same_as<void>;
    };

template <typename Factory, typename Thread>
concept thread_factory_c = thread_c<Thread> and
    requires(Factory & factory, ThreadEntry entry_f, void * state_p) {
        { factory(entry_f, state_p) } -> std::same_as<Thread>;
    };

} // namespace lcf::async
```

`include/async/core/operations.h`：

```cpp
#pragma once

#include "concepts.h"

namespace lcf::async {

struct get_schedule_node_t
{
    template <schedule_record_c Record>
    decltype(auto) operator()(Record & record) const noexcept
    {
        return record.getScheduleNode();
    }
};

inline constexpr get_schedule_node_t get_schedule_node{};

struct try_enqueue_t
{
    template <typename Executor, typename Record>
    requires executor_c<Executor, Record>
    EnqueueResult operator()(Executor & executor, Record & record) const noexcept
    {
        return executor.tryEnqueue(record);
    }
};

inline constexpr try_enqueue_t try_enqueue{};

} // namespace lcf::async
```

`schedule_node_c` 只要求 `execute() noexcept -> void`；`schedule_record_c` 要求 noexcept 返回这种节点的左值引用，不指定具体类。`BasicExecutor::tryEnqueue` 在实际消费点检查所选 Queue 能否接纳该 record 的节点。`executor_c<Executor, Record>` 保留实际 cv 限定；const handle 可经借用指针提交到非 const executor。eClosed 不保留 record，eAccepted 后 record 可能立即执行并销毁，提交方不得再读它。MVP 没有容量拒收，eFull 及其完成映射随有界入口一起加入。

### 4.2 驱动、阻塞等待与停止

上述 concepts 分别检查单步驱动、阻塞等待和多 Worker 声明。concept 只验证静态成员/表达式，不能证明线程安全；Queue 的实际多 consumer 安全由其契约及并发测试验证。

BasicContext 类级别要求 `multi_worker_executor_c`，因为它创建的 N 个 Worker 共享 CPU Executor；Context::run/requestStop 成员另要求 `stoppable_work_executor_c`。独立 BasicExecutor<Queue> 可只支持 poll/wait 单步执行，不必声明支持多个 Worker。

### 4.3 Queue

core/concepts.h 中公开的 `schedule_queue_c<Queue>` 通过 `Queue::Node` 检查节点和队列操作，不规定 ScheduleNode、继承关系、链表布局或默认构造。等待/停止能力分别由 blocking_schedule_queue_c、stoppable_schedule_queue_c 表达。

当一个 Queue 被多个 Worker 消费时，还需声明 `supports_multiple_consumers = true`；BasicExecutor 据此暴露 `supports_multiple_workers`，由 `multi_worker_executor_c` 检查。该声明是实现者对并发安全的承诺，不是表达式概念可以证明的性质。MutexInbox 以 mutex 保护多 producer、多 consumer 的入队/出队。

### 4.4 标准 execution 协议与 Core 命名

`protocol.h` 是协议聚合入口，内部按需要实现的角色拆分；操作和查询另有入口。CPO 是接口的实现形式，不能用“是否为 CPO”判断是否参与协议：connect/start/schedule 和完成通知也参与契约，但其调用对象与对应 `_t` 类型统一从 exec 使用。then/when_all 等组合算法同样位于 exec，Core 只包含所需的基础操作/查询头。

| 文件 | 命名空间 | 内容与消费方 |
| --- | --- | --- |
| protocols/sender.h | async::protocols | sender_t、sender/sender_to、completion_signatures/completion_signatures_of_t；供 sender 实现和协议检查使用 |
| protocols/receiver.h | async::protocols | receiver_t、receiver/receiver_of；供 receiver 实现和连接约束使用 |
| protocols/operation_state.h | async::protocols | operation_state_t、operation_state；供 operation 实现使用 |
| protocols/scheduler.h | async::protocols | scheduler_t、scheduler；供 SchedulerHandle 等实现使用 |
| execution/operations.h | async::exec | connect/start/schedule、set_value/set_error/set_stopped 及其 CPO 类型、操作结果类型；供启动和完成操作使用 |
| execution/queries.h | async::exec | get_env/get_stop_token/get_allocator/get_scheduler/get_completion_scheduler 及其 CPO 类型、env_of_t；供环境查询和 query 重载使用 |

sender_c 等项目命名概念服务于 Core，与运行时约束统一放在第 4.1 节的 core/concepts.h，直接定义于 lcf::async。execution 直接复用 protocols 中的标准概念，不包含这组别名；转发只复用原约束，不重新实现 sender/receiver 协议。内部适配实际消费 receiver_of_c；scheduler_c、sender_c、operation_state_c 用于公共调度接口的契约验证。

实现用 protocols::scheduler_t、protocols::receiver_t 声明角色；调用用 exec::schedule、exec::connect、exec::start。完成签名用 protocols::completion_signatures<exec::set_value_t(...)>；environment 的 query 重载接受 exec::get_completion_scheduler_t<exec::set_value_t>。每个名字只有一个项目重导出位置，不在 protocols 和 exec 重复提供同名入口。概念转发只统一 Core 的 _c 命名，不隔离对标准协议的依赖。

### 4.5 线程后端

线程入口类型是 Core concepts 与 scheduling 共用的基础词汇，定义在 Core 层；concepts 通过它检查线程工厂，不依赖 scheduling：

`include/async/core/thread_types.h`：

```cpp
#pragma once

#include <async/config/macros.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

using ThreadEntry = void (*)(void *) noexcept;

} // namespace lcf::async
```

`ThreadEntry` 与 `ScheduleNode::ExecuteFunction` 在 C++ 类型系统中是同一函数指针类型；前者表达线程入口，后者表达一次节点执行，分别保留其语义名称。

core/concepts.h 中的 thread_c 服务于 BasicWorker 的 join/析构；thread_factory_c 服务于创建实际线程，允许宿主独立决定线程栈、启动状态及系统句柄的管理。factory 返回前可以启动 entry，返回的线程对象必须持续代表该执行；若创建抛异常，factory 必须自行回收尚未交付的线程和存储。factory 仅在构造期使用，异步启动状态不能借用已经销毁的 factory。join 只允许在其他线程调用；BasicWorker::join 是 noexcept，后端 join 违反契约或报告无法完成时终止，不能继续释放仍被线程使用的存储。标准线程后端的内部申请不受 PMR 控制，不能由另一个“线程 resource 参数”伪装成已受控。

### 4.6 typename 与 concept 的使用位置

单个类型的能力约束优先写成 `template <concept Type>`；concept 的其他参数已确定时，也直接写在模板参数中。跨类型关系、构造参数包和成员的额外启用条件使用 requires；不为缩短写法另建同义 concept。

| 参数位置 | 约束或保留 typename 的理由 |
| --- | --- |
| concept 定义 | 接受任意候选类型，用 requires 直接检查成员表达式 |
| get_schedule_node_t / try_enqueue_t | 分别复用 schedule_record_c / executor_c；约束已保证成员调用有效且 noexcept |
| BasicExecutor<Queue> | 公开的 schedule_queue_c<Queue>；tryEnqueue 按实际 Record 检查 Queue 接纳关系；多 Worker 使用时 Queue 还声明 supports_multiple_consumers |
| BasicWorker<Executor, Thread> | blocking_work_executor_c、thread_c；多个 Worker 共用时由 Context 的 multi_worker_executor_c 检查 |
| BasicContext<ControlExecutor, CpuExecutor, Thread> | control work_executor_c、CPU multi_worker_executor_c、thread_c 及双 Executor/Thread factory；所有 Worker 借用同一个 CPU Executor |
| DefaultExecutorFactory<ControlExecutor, CpuExecutor> | 两种 work_executor_c；分别创建 control 和 CPU Executor，仅默认工厂要求对应类型默认构造 |
| FixedCapacityVector<Value> | std::destructible；emplaceBack 检查构造关系，emplaceWith 检查 factory 的精确返回类型以支持不可移动元素 |
| LogicalQueue<Executor>、SchedulerHandle<Executor> | 类只保存引用，尚不知道实际 Record，保留 typename；tryEnqueue<Record> 检查二元 executor_c；SchedulerHandle 直接在 Core 实现标准 scheduler 协议 |
| QueueArgs... | std::constructible_from<Queue, Args...>，不能用一个空泛 concept 限定所有配置值 |
| 内部 ScheduleSender/Operation 的 Handle / Receiver | Handle 用 std::copyable 约束实际值复制，实例为已完成定义的 SchedulerHandle；Receiver 使用转发到标准协议的 receiver_of_c<Receiver, ScheduleCompletions> |
| ScheduleSender::connect、ScheduleOperation::start | 实际 operation 完整后再检查 executor_c<Handle, Operation>；不在句柄定义期间递归检查 scheduler 概念 |
| 示例 Receiver::set_error<Error> | 记录通道，不读取 Error 专有接口，无需人为限定为单一错误类型 |

新增 concept 必须说明直接消费方、基础表达式和额外语义。concept 不能证明线程安全、地址稳定、恰好一次或资源寿命，下面另列这些契约。

## 5. 可组合的参考实现

### 5.1 ScheduleNode 与 Queue 后端

ScheduleNode 是 MutexInbox、FastProducerSpmcInbox 与标准 schedule operation 共用的具体调度 hook，位于 scheduling/ScheduleNode.h，直接声明在 lcf::async。它存 atomic next、入队状态、state 和函数指针，不是公共 operation 或基类。atomic next 以 release/acquire 发布关系支持侵入式链表；MutexInbox 也通过同一访问器操作它。异构 dispatch 在这里使用回调擦除；通用 `schedule_node_c` 只约束 execute，自定义 Queue 可以声明自己的 Node，无需继承 ScheduleNode。

MutexInbox 在接纳时调用 node.markQueued()，出队时调用 node.clearQueueState()；拒收不修改 node，出队后不再访问它。getNext/setNext 供链式存储实现使用，这些接口不属于通用 Queue/Node 协议。node 由 record 内嵌，不逐次分配，无 friend；Executor 直接调用 node.execute()，节点内部在 dispatch 前保存回调和状态指针。完成回调可能销毁 record，dispatch 后不再访问节点；状态和函数指针不通过 getter 暴露。

`queue/FastProducerSpmcInbox.h`：

```cpp
#pragma once

#include <async/core/enums.h>
#include <async/core/scheduling/ScheduleNode.h>

#include <atomic>
#include <cstddef>
#include <mutex>

namespace lcf::async {

class FastProducerSpmcInbox
{
public:
    using Node = ScheduleNode;
    static constexpr bool supports_multiple_producers = false;
    static constexpr bool supports_multiple_consumers = true;

    FastProducerSpmcInbox() noexcept
        : m_stub{nullptr, &execute_stub}, m_head_p{&m_stub}, m_tail_p{&m_stub} {}

    EnqueueResult tryEnqueue(ScheduleNode & node) noexcept;
    ScheduleNode * tryPop() noexcept;
    ScheduleNode * waitPop() noexcept;
    void requestStop() noexcept;
    void close() noexcept;
    bool empty() const noexcept;

private:
    std::mutex m_consumer_mutex;
    ScheduleNode m_stub;
    std::atomic<ScheduleNode *> m_head_p;
    ScheduleNode * m_tail_p;
    std::atomic<std::size_t> m_queued_count{0};
    std::atomic<std::uint64_t> m_wake_sequence{0};
    std::atomic<unsigned> m_lifecycle{0};
    std::atomic<bool> m_stop_requested{false};

    static void execute_stub(void *) noexcept {}
};

} // namespace lcf::async
```

`tryEnqueue` is restricted to one producer. It enters a short lifecycle state with one compare-exchange, marks the node, does `previous = head.exchange(&node)`, then release-stores the node into `previous.next`; it never acquires `m_consumer_mutex` and never allocates. A concurrent second producer is a contract violation and terminates. Multiple workers serialize `tryPop`/`waitPop` under `m_consumer_mutex`; a publication gap returns empty rather than spinning. `waitPop` uses a wake sequence to recheck the queue before waiting, preventing a lost wake when publication races with sleep. `close` marks the inbox closed and waits for the active producer state to clear, so accepted work is fully linked before close returns. Destruction still requires stopping the producer, draining accepted nodes, and finishing all consumer calls before destroying their operations. The producer has a fixed number of queue operations, but atomic notification and platform scheduling do not provide a portable wall-clock WCET guarantee.

Record 的节点类型与实际 Queue 是否兼容由 tryEnqueue 的 requires 表达式检查。自定义节点只需 execute 能力，但接入当前标准 schedule adapter 时，Queue 仍须能接纳该 adapter 内嵌的 ScheduleNode，或另行提供匹配的 operation。

`include/async/core/scheduling/ScheduleNode.h`：

```cpp
#pragma once

#include <atomic>
#include <cassert>

namespace lcf::async {

class ScheduleNode
{
public:
    using ExecuteFunction = void (*)(void *) noexcept;

    ScheduleNode(void * state_p, ExecuteFunction execute_f) noexcept
        : m_state_p(state_p), m_execute_f(execute_f) {}

    ScheduleNode(const ScheduleNode &) = delete;
    ScheduleNode & operator=(const ScheduleNode &) = delete;
    ScheduleNode(ScheduleNode &&) = delete;
    ScheduleNode & operator=(ScheduleNode &&) = delete;

    ~ScheduleNode() noexcept { assert(not m_queued); }
    ScheduleNode * getNext() const noexcept
    {
        return m_next_p.load(std::memory_order_acquire);
    }
    void setNext(ScheduleNode * next_p) noexcept
    {
        m_next_p.store(next_p, std::memory_order_release);
    }
    bool isQueued() const noexcept { return m_queued; }
    void markQueued() noexcept
    {
        assert(not m_queued);
        m_queued = true;
    }
    void clearQueueState() noexcept
    {
        m_next_p.store(nullptr, std::memory_order_relaxed);
        m_queued = false;
    }
    void execute() noexcept
    {
        auto * state_p = m_state_p;
        auto execute_f = m_execute_f;
        // Completion may destroy this node. Do not access members after dispatch.
        execute_f(state_p);
    }
private:
    std::atomic<ScheduleNode *> m_next_p{nullptr};
    void * m_state_p;
    ExecuteFunction m_execute_f;
    bool m_queued = false;
};

} // namespace lcf::async
```

`include/async/core/scheduling/queue/MutexInbox.h`：

```cpp
#pragma once

#include <async/core/concepts.h>
#include <async/core/scheduling/ScheduleNode.h>

#include <cassert>
#include <condition_variable>
#include <mutex>

namespace lcf::async {

class MutexInbox
{
public:
    using Node = ScheduleNode;
    static constexpr bool supports_multiple_consumers = true;

    EnqueueResult tryEnqueue(ScheduleNode & node) noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            if (m_closed) {
                return EnqueueResult::eClosed;
            }
            node.markQueued();
            if (m_tail_p) {
                m_tail_p->setNext(&node);
            } else {
                m_head_p = &node;
            }
            m_tail_p = &node;
        }
        m_condition.notify_one();
        return EnqueueResult::eAccepted;
    }

    ScheduleNode * tryPop() noexcept
    {
        std::lock_guard lock{m_mutex};
        return this->popFront();
    }

    ScheduleNode * waitPop() noexcept
    {
        std::unique_lock lock{m_mutex};
        m_condition.wait(lock, [this] {
            return m_stop_requested || m_closed || m_head_p != nullptr;
        });
        if (m_stop_requested) {
            return nullptr;
        }
        return this->popFront();
    }

    void requestStop() noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            m_stop_requested = true;
        }
        m_condition.notify_all();
    }

    void close() noexcept
    {
        {
            std::lock_guard lock{m_mutex};
            m_closed = true;
        }
        m_condition.notify_all();
    }

    bool empty() const noexcept
    {
        std::lock_guard lock{m_mutex};
        return m_head_p == nullptr;
    }

private:
    // The caller holds m_mutex. Never dispatch user code while holding it.
    ScheduleNode * popFront() noexcept
    {
        auto * node_p = m_head_p;
        if (not node_p) {
            return nullptr;
        }
        m_head_p = node_p->getNext();
        if (not m_head_p) {
            m_tail_p = nullptr;
        }
        node_p->clearQueueState();
        return node_p;
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    ScheduleNode * m_head_p = nullptr;
    ScheduleNode * m_tail_p = nullptr;
    bool m_closed = false;
    bool m_stop_requested = false;
};

} // namespace lcf::async
```

所有接纳、取出、关闭、停止和等待 predicate 都用同一 mutex 排序，因而没有“检查空 → notify → 入睡”的丢唤醒窗口。MutexInbox 支持多 producer、多 consumer；回调在出队解锁后执行，FIFO 只约束取出顺序，不承诺多 Worker 的开始或完成顺序；tryPop 不等待新工作，但获取 mutex 可能短暂阻塞，它是正确性基线，不是 wait-free。close 后仍可取出已接纳节点；stop 请求优先终止 waitPop，但 tryPop 仍可排空。拒收不修改 node，接纳后恰好出队一次；同一 operation/node 只能 start/提交一次，并保持地址稳定至最后访问。

### 5.2 Executor、Worker 与预算包装

PollBudget 和 PollReport 服务于 Context::poll 的批量驱动包装，不是 work_executor_c 的基础协议。它们放在 scheduling/poll_structs.h，由 BasicContext.h 直接包含；protocol.h、concepts.h 不包含它们。

`include/async/core/scheduling/poll_structs.h`：

```cpp
#pragma once

#include <chrono>
#include <cstddef>
#include <limits>

namespace lcf::async {

struct PollBudget
{
    std::size_t m_max_records = std::numeric_limits<std::size_t>::max();
    std::chrono::steady_clock::duration m_max_duration =
        std::chrono::steady_clock::duration::max();
};

struct PollReport
{
    std::size_t m_records = 0;
    std::chrono::steady_clock::duration m_elapsed{};
    bool m_count_limit_reached = false;
    bool m_time_limit_reached = false;
};

} // namespace lcf::async
```

pollOne 最多执行一条 record；一条 record 中可执行普通函数链或未来的协程段，预算不能抢占它。poll 在 record 边界检查数量与软时间限制，计入执行和必要维护开销；pollOne 本身不强制读取时钟。空队列立即返回，不证明全部 root 已结束。

`include/async/core/scheduling/BasicExecutor.h`：

```cpp
#pragma once

#include <async/core/operations.h>
#include <async/core/scheduling/queue/MutexInbox.h>

#include <cassert>
#include <concepts>
#include <utility>

namespace lcf::async {

template <schedule_queue_c Queue = MutexInbox>
class BasicExecutor
{
public:
    static constexpr bool supports_multiple_workers = [] {
        if constexpr (requires { Queue::supports_multiple_consumers; }) {
            return Queue::supports_multiple_consumers;
        } else {
            return false;
        }
    }();

    template <typename... QueueArgs>
        requires std::constructible_from<Queue, QueueArgs...>
    explicit BasicExecutor(QueueArgs &&... queue_args)
        : m_queue(std::forward<QueueArgs>(queue_args)...) {}

    BasicExecutor(const BasicExecutor &) = delete;
    BasicExecutor & operator=(const BasicExecutor &) = delete;

    ~BasicExecutor() noexcept { assert(m_queue.empty()); }

    template <schedule_record_c Record>
        requires requires(Queue & queue, Record & record) {
            { queue.tryEnqueue(get_schedule_node(record)) } noexcept
                -> std::same_as<EnqueueResult>;
        }
    EnqueueResult tryEnqueue(Record & record) noexcept
    {
        return m_queue.tryEnqueue(get_schedule_node(record));
    }

    bool pollOne() noexcept
    {
        if (auto * node_p = m_queue.tryPop()) {
            node_p->execute();
            return true;
        }
        return false;
    }

    bool waitOne() noexcept
        requires blocking_schedule_queue_c<Queue>
    {
        if (auto * node_p = m_queue.waitPop()) {
            node_p->execute();
            return true;
        }
        return false;
    }

    void requestStop() noexcept
        requires stoppable_schedule_queue_c<Queue>
    {
        m_queue.requestStop();
    }

    void close() noexcept { m_queue.close(); }
    bool empty() const noexcept { return m_queue.empty(); }

private:
    Queue m_queue;
};

template <work_executor_c ControlExecutor, work_executor_c CpuExecutor = ControlExecutor>
struct DefaultExecutorFactory
{
    ControlExecutor makeControlExecutor() const
        requires std::default_initializable<ControlExecutor>
    {
        return ControlExecutor{};
    }

    CpuExecutor makeCpuExecutor() const requires std::default_initializable<CpuExecutor>
    {
        return CpuExecutor{};
    }
};

} // namespace lcf::async
```

BasicExecutor 拥有 Queue，负责 tryEnqueue、pollOne/waitOne、close 和空状态查询；它不创建线程，也不等同于持续运行的 event loop。DefaultExecutorFactory 分别创建 control 与 CPU Executor；二者可以使用不同 Queue。CPU Executor 的 Queue 若声明 supports_multiple_consumers，BasicExecutor 才声明 supports_multiple_workers，Context 才能让多个 Worker 共享它。

`include/async/core/scheduling/BasicWorker.h`：

```cpp
#pragma once

#include <async/core/thread_types.h>
#include <async/core/concepts.h>

#include <cassert>
#include <thread>

namespace lcf::async {

struct StdThreadFactory
{
    std::thread operator()(ThreadEntry entry_f, void * state_p) const
    {
        return std::thread{entry_f, state_p};
    }
};

template <blocking_work_executor_c Executor, thread_c Thread = std::thread>
class BasicWorker
{
public:
    template <thread_factory_c<Thread> Factory = StdThreadFactory>
    explicit BasicWorker(Executor & executor, Factory factory = {})
        : m_executor_p(&executor), m_thread(factory(&run_executor, this)) {}

    BasicWorker(const BasicWorker &) = delete;
    BasicWorker & operator=(const BasicWorker &) = delete;
    BasicWorker(BasicWorker &&) = delete;
    BasicWorker & operator=(BasicWorker &&) = delete;

    ~BasicWorker() noexcept { assert(not m_thread.joinable()); }

    void join() noexcept
    {
        // Calling from this worker violates the backend's join precondition.
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    static void run_executor(void * state_p) noexcept
    {
        auto & worker = *static_cast<BasicWorker *>(state_p);
        while (worker.m_executor_p->waitOne()) {}
    }

    Executor * m_executor_p;
    Thread m_thread;
};

} // namespace lcf::async
```

每个 BasicWorker 拥有一个线程并借用一个 blocking Executor。Context 创建的 N 个 Worker 都持有同一个 CPU Executor；多 consumer Queue 在锁内领取一个节点，BasicExecutor 解锁后执行回调。run loop 与控制 Executor 分离，控制 requestStop 不停止 CPU Workers；close CPU Executor 后，Worker 排空已接纳工作并退出。调用方返回 Executor 由调用方线程自行 poll。

Worker 借用的 Executor 必须保持到 join 结束。Worker 析构要求线程已 join；宿主负责显式 close/join。

### 5.3 逻辑身份、配置与 Context

Context 的数量在构造前已经确定，不需要 deque 的分块表或运行时扩容。内部 FixedCapacityVector 用显式 typed allocator 一次申请容量，只构造实际元素，保留原 allocator 和元素数用于逆序销毁与释放。它只服务固定数组，不实现通用容器的 uses-allocator 传播；需要内部分配的元素由工厂显式配置。

名称中的 FixedCapacity 指容量在构造时确定、之后不再改变；Vector 指连续存储、区分 size/capacity，并通过 emplaceBack/emplaceWith 逐个构造元素。它属于 containers，分配来源仍由 allocator 决定。`FixedDynamicArray` 更容易让人理解为创建时已构造全部元素、长度固定的数组。与 C++26 [std::inplace_vector](https://eel.is/c++draft/inplace.vector.overview) 的区别是：后者容量 N 为编译期常量，元素内嵌于容器对象；本轮容器的容量在运行期指定，元素存储通过显式 allocator 申请。

`include/async/core/details/containers/FixedCapacityVector.h`：

```cpp
#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace lcf::async::details {

template <std::destructible Value>
class FixedCapacityVector
{
public:
    using Allocator = std::pmr::polymorphic_allocator<Value>;

    FixedCapacityVector(std::size_t capacity, Allocator allocator)
        : m_allocator(allocator), m_capacity(capacity)
    {
        if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
            throw std::bad_array_new_length{};
        }
        if (capacity != 0) {
            m_data_p = m_allocator.allocate(capacity);
        }
    }

    FixedCapacityVector(const FixedCapacityVector &) = delete;
    FixedCapacityVector & operator=(const FixedCapacityVector &) = delete;
    FixedCapacityVector(FixedCapacityVector && other) noexcept
        : m_allocator(other.m_allocator),
          m_data_p(std::exchange(other.m_data_p, nullptr)),
          m_capacity(std::exchange(other.m_capacity, 0)),
          m_size(std::exchange(other.m_size, 0)) {}
    FixedCapacityVector & operator=(FixedCapacityVector &&) = delete;

    ~FixedCapacityVector() noexcept
    {
        while (m_size != 0) {
            std::destroy_at(m_data_p + --m_size);
        }
        if (m_data_p) {
            m_allocator.deallocate(m_data_p, m_capacity);
        }
    }

    template <typename... Args>
        requires std::constructible_from<Value, Args...>
    Value & emplaceBack(Args &&... args)
    {
        this->checkCapacity();
        auto * value_p = std::construct_at(m_data_p + m_size, std::forward<Args>(args)...);
        ++m_size;
        return *value_p;
    }

    template <typename Factory, typename... Args>
        requires std::invocable<Factory, Args...>
            && std::same_as<std::invoke_result_t<Factory, Args...>, Value>
    Value & emplaceWith(Factory && factory, Args &&... args)
    {
        this->checkCapacity();
        // Placement new constructs in already allocated storage. Guaranteed
        // copy elision allows a factory to return a nonmovable Executor.
        auto * value_p = ::new (static_cast<void *>(m_data_p + m_size)) Value(
            std::invoke(std::forward<Factory>(factory), std::forward<Args>(args)...));
        ++m_size;
        return *value_p;
    }

    Value & operator[](std::size_t index) noexcept
    {
        assert(index < m_size);
        return m_data_p[index];
    }

    const Value & operator[](std::size_t index) const noexcept
    {
        assert(index < m_size);
        return m_data_p[index];
    }

    std::span<Value> values() noexcept { return {m_data_p, m_size}; }
    std::span<const Value> values() const noexcept { return {m_data_p, m_size}; }
    std::size_t size() const noexcept { return m_size; }
    std::size_t capacity() const noexcept { return m_capacity; }

private:
    void checkCapacity() const
    {
        if (m_size == m_capacity) {
            throw std::length_error{"FixedCapacityVector capacity exceeded"};
        }
    }

    Allocator m_allocator;
    Value * m_data_p = nullptr;
    std::size_t m_capacity;
    std::size_t m_size = 0;
};

} // namespace lcf::async::details
```


LogicalQueueToken 与 LogicalQueue 定义在同一个头文件。token 由 ContextInfo 成功登记后签发，保存配置域 ID 和 `std::size_t` route 下标；LogicalQueue 保存 control 或 CPU Executor 的借用引用，身份来自稳定 route 地址。WorkerCount、QueueTarget 和 QueueBinding 定义在 ContextInfo.h。CPU logical queue 不保存 worker index，也没有预设登记容量。

`include/async/core/scheduling/LogicalQueue.h`：

```cpp
#pragma once

#include <async/core/operations.h>

#include <cstddef>
#include <cstdint>

namespace lcf::async {

class ContextInfo;

class LogicalQueueToken
{
public:
    LogicalQueueToken(const LogicalQueueToken &) noexcept = default;
    LogicalQueueToken & operator=(const LogicalQueueToken &) noexcept = default;

    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::size_t getIndex() const noexcept { return m_index; }
    bool operator==(const LogicalQueueToken &) const noexcept = default;

private:
    // Issuance is restricted to successful ContextInfo registration.
    friend class ContextInfo;

    LogicalQueueToken(std::uint32_t domain_id, std::size_t index) noexcept
        : m_domain_id(domain_id), m_index(index) {}

    std::uint32_t m_domain_id;
    std::size_t m_index;
};

template <typename Executor>
class LogicalQueue
{
public:
    explicit LogicalQueue(Executor & executor) noexcept : m_executor_p(&executor) {}

    template <typename Record>
        requires executor_c<Executor, Record>
    EnqueueResult tryEnqueue(Record & record) const noexcept
    {
        return try_enqueue(*m_executor_p, record);
    }

private:
    Executor * m_executor_p;
};

} // namespace lcf::async
```

`include/async/core/context/ContextInfo.h`：

```cpp
#pragma once

#include <async/core/scheduling/LogicalQueue.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lcf::async {

using WorkerCount = std::uint32_t;

enum class QueueTarget
{
    eControl,
    eCpu,
};

struct QueueBinding
{
    QueueTarget m_target;
};

struct StorageRequirement
{
    std::size_t m_bytes;
    std::size_t m_alignment;
};

struct ContextStorageRequirements
{
    StorageRequirement m_workers;
    StorageRequirement m_routes;
};

struct ContextAllocators
{
    using Allocator = std::pmr::polymorphic_allocator<std::byte>;

    ContextAllocators(Allocator workers, Allocator routes) noexcept
        : m_workers(workers), m_routes(routes) {}

    Allocator m_workers;
    Allocator m_routes;
};

class ContextInfo
{
public:
    explicit ContextInfo(WorkerCount worker_count) noexcept
        : m_domain_id(acquire_domain_id()), m_worker_count(worker_count) {}

    ContextInfo(const ContextInfo &) = delete;
    ContextInfo & operator=(const ContextInfo &) = delete;
    ContextInfo(ContextInfo && other) noexcept
        : m_domain_id(std::exchange(other.m_domain_id, 0)),
          m_worker_count(std::exchange(other.m_worker_count, 0)),
          m_queues(std::move(other.m_queues)) {}
    ContextInfo & operator=(ContextInfo &&) = delete;

    LogicalQueueToken addControlQueue() noexcept
    {
        return this->addQueue(QueueTarget::eControl);
    }

    LogicalQueueToken addCpuQueue() noexcept
    {
        if (m_worker_count == 0) {
            std::terminate();
        }
        return this->addQueue(QueueTarget::eCpu);
    }

    WorkerCount getWorkerCount() const noexcept { return m_worker_count; }
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::span<const QueueBinding> getQueueBindings() const noexcept { return m_queues; }

private:
    LogicalQueueToken addQueue(QueueTarget target) noexcept
    {
        if (m_domain_id == 0) {
            std::terminate();
        }
        const auto index = m_queues.size();
        m_queues.push_back(QueueBinding{target});
        return LogicalQueueToken{m_domain_id, index};
    }

    static std::uint32_t acquire_domain_id() noexcept
    {
        auto next = s_next_domain_id.load(std::memory_order_relaxed);
        for (;;) {
            if (next == std::numeric_limits<std::uint32_t>::max()) {
                std::terminate();
            }
            if (s_next_domain_id.compare_exchange_weak(
                    next, next + 1, std::memory_order_relaxed)) {
                return next;
            }
        }
    }

    inline static std::atomic<std::uint32_t> s_next_domain_id{1};
    std::uint32_t m_domain_id;
    WorkerCount m_worker_count;
    std::vector<QueueBinding> m_queues;
};

} // namespace lcf::async
```

LogicalQueueToken 不能默认构造或通过数字构造；`addControlQueue()` / `addCpuQueue()` 均为 `noexcept`，成功时直接返回 token。窄范围 friend 只授权 ContextInfo 签发，不让 Context/queue/worker 相互访问私有布局。ContextInfo 使用 `std::vector<QueueBinding>`，没有应用层固定容量；扩容分配失败会终止。CPU target 没有 Worker、或配置已移走时属于契约违例并终止。域 ID 耗尽发生在 ContextInfo 构造期并终止，不回绕复用。ContextInfo 的配置操作单线程执行；WorkerCount 构造时固定，不在登记后修改。

每个 ContextInfo 取得不复用的域 ID，移动会转移域和 vector 存储并使源对象失效；Context 按值消费配置且不可复制。token 不能被拿去查询另一个 Context 或后来重建的 Context；`findLogicalQueue` 用 `assert` 检查域 ID，域一致性保证下标来自同一配置的登记结果。错误域 token 是调用方代码错误，不提供可恢复查询。它只保证签发来源和 route 有效性，不延长运行时/句柄寿命；不是持有资源的智能指针。域 ID 为 32 位；route 下标用 `std::size_t`，不额外限制逻辑队列数量，实际受 vector 最大容量、地址空间和可用内存约束。在常见 64 位 ABI 上 token 为 16 字节。域号从 1 开始，以固定静态 atomic 签发；0 留给已移走配置，最大值保留作耗尽哨兵。耗尽时终止，不回绕复用，不申请堆内存。跨独立运行时实例/动态模块传递 token 不属于该编号域契约。

独立调用方 executor 用 LogicalQueue{executor} 建立路由，不伪造 Context token，也不写入 ContextInfo。标准 SchedulerHandle 直接引用路由；相同路由对象得到相等 handle；不同路由即使映射到同一物理 executor 也不相等。LogicalQueue 不存 record、不提供 pop/poll，运行期不移动或重绑已发布路由。

`include/async/core/context/BasicContext.h`：

```cpp
#pragma once

#include "ContextInfo.h"
#include <async/core/operations.h>
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/scheduling/poll_structs.h>
#include <async/core/scheduling/BasicExecutor.h>
#include <async/core/scheduling/BasicWorker.h>
#include <async/core/scheduling/LogicalQueue.h>
#include <async/core/scheduling/SchedulerHandle.h>

#include <cassert>
#include <chrono>
#include <concepts>
#include <exception>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <stdexcept>
#include <thread>
#include <utility>

namespace lcf::async {

namespace details {

template <work_executor_c Executor>
PollReport poll_executor(Executor & executor, PollBudget budget = {}) noexcept
{
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const bool finite_time = budget.m_max_duration != Clock::duration::max();
    PollReport report;
    while (report.m_records < budget.m_max_records) {
        if (finite_time && Clock::now() - started >= budget.m_max_duration) { break; }
        if (not executor.pollOne()) { break; }
        ++report.m_records;
    }
    report.m_elapsed = Clock::now() - started;
    report.m_count_limit_reached = report.m_records == budget.m_max_records;
    report.m_time_limit_reached = finite_time && report.m_elapsed >= budget.m_max_duration;
    return report;
}

} // namespace details

template <work_executor_c ControlExecutor = BasicExecutor<>,
    multi_worker_executor_c CpuExecutor = BasicExecutor<>,
    thread_c Thread = std::thread>
class BasicContext
{
public:
    using Worker = BasicWorker<CpuExecutor, Thread>;
    using Route = details::ContextExecutorRoute<ControlExecutor, CpuExecutor>;
    using RouteBinding = details::ContextLogicalQueue<ControlExecutor, CpuExecutor>;

    template <typename ExecutorFactory = DefaultExecutorFactory<ControlExecutor, CpuExecutor>,
        thread_factory_c<Thread> ThreadFactory = StdThreadFactory>
        requires work_executor_factory_c<ExecutorFactory, ControlExecutor, CpuExecutor>
    explicit BasicContext(ContextInfo context_info, ContextAllocators allocators,
        ExecutorFactory executor_factory = {}, ThreadFactory thread_factory = {})
        : m_domain_id(validate_info(context_info)),
          m_control_executor(executor_factory.makeControlExecutor()),
          m_cpu_executor(executor_factory.makeCpuExecutor()),
          m_workers(context_info.getWorkerCount(),
              std::pmr::polymorphic_allocator<Worker>{allocators.m_workers}),
            m_queues(context_info.getQueueBindings().size(),
              std::pmr::polymorphic_allocator<RouteBinding>{allocators.m_routes})
    {
        for (const auto & binding : context_info.getQueueBindings()) {
            m_queues.emplaceBack(m_control_executor, m_cpu_executor,
                binding.m_target == QueueTarget::eCpu);
        }
        try {
            for (WorkerCount index = 0; index < context_info.getWorkerCount(); ++index) {
                m_workers.emplaceBack(m_cpu_executor, thread_factory);
            }
        } catch (...) {
            this->close();
            this->joinWorkers();
            throw;
        }
    }

    BasicContext(const BasicContext &) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    ~BasicContext() noexcept { assert(this->empty()); }

    static constexpr ContextStorageRequirements storageRequirements(
        WorkerCount worker_count, std::size_t queue_count) noexcept
    {
        return {storage_requirement<Worker>(worker_count),
            storage_requirement<RouteBinding>(queue_count)};
    }

    static ContextStorageRequirements storageRequirements(const ContextInfo & info) noexcept
    {
        return storageRequirements(info.getWorkerCount(), info.getQueueBindings().size());
    }

    SchedulerHandle<Route> getSchedulerHandle(LogicalQueueToken token) const noexcept
    {
        return SchedulerHandle<Route>{this->findLogicalQueue(token)};
    }

    const LogicalQueue<Route> & findLogicalQueue(LogicalQueueToken token) const noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return m_queues[token.getIndex()].getLogicalQueue();
    }

    bool pollOne() noexcept { return m_control_executor.pollOne(); }
    PollReport poll(PollBudget budget = {}) noexcept
    {
        return details::poll_executor(m_control_executor, budget);
    }
    void run() noexcept requires stoppable_work_executor_c<ControlExecutor>
    {
        while (m_control_executor.waitOne()) {}
    }
    void requestStop() noexcept requires stoppable_work_executor_c<ControlExecutor>
    {
        m_control_executor.requestStop();
    }
    void close() noexcept
    {
        m_control_executor.close();
        m_cpu_executor.close();
    }
    void joinWorkers() noexcept
    {
        for (auto & worker : m_workers.values()) { worker.join(); }
    }
    bool empty() const noexcept
    {
        return m_control_executor.empty() && m_cpu_executor.empty();
    }

private:
    static std::uint32_t validate_info(const ContextInfo & info)
    {
        if (info.getDomainId() == 0 || info.getQueueBindings().empty()) {
            throw std::invalid_argument{"Context requires an unconsumed configuration with routes"};
        }
        return info.getDomainId();
    }

    template <std::destructible Value>
    static constexpr StorageRequirement storage_requirement(std::size_t count) noexcept
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
            std::terminate();
        }
        return {count * sizeof(Value), alignof(Value)};
    }

    std::uint32_t m_domain_id;
    ControlExecutor m_control_executor;
    CpuExecutor m_cpu_executor;
    details::FixedCapacityVector<Worker> m_workers;
    details::FixedCapacityVector<RouteBinding> m_queues;
};

using Context = BasicContext<>;

} // namespace lcf::async
```

Context 可以内嵌不同类型的 control Executor 和 CPU Executor；默认两者使用 MutexInbox，第 1 节的 ExampleContext 显式将 CPU 侧换为 FastProducerSpmcInbox。固定数组保存 Worker 与 ContextLogicalQueue route binding，各使用 ContextAllocators 提供的 PMR allocator。每条 binding 持有轻量 ContextExecutorRoute，按 QueueTarget 转发到相应 Executor；storageRequirements 报告 Worker 与 route binding 数组的字节数/对齐。Executor 对象内嵌，不进入这些数组分配。

构造顺序为 control/CPU Executor、Worker 与 route 数组存储块、全部 route bindings，最后启动 N 个 Worker；不使用 deque/vector 增长。所有 Worker 借用同一个 CPU Executor。FixedCapacityVector 不移动元素，Context 不可移动。ExecutorFactory 为两种 Executor 的 Queue 显式提供 allocator、容量及配置，不把数组 allocator 自动传入 Queue。ThreadFactory 独立选择线程后端。

数组分配或 Executor 构造失败时，成员按已构造范围回滚；线程启动失败时先关闭 control/CPU Executor，再 join 已启动 Workers，最后由成员析构释放数组。已启动 Worker 共享 CPU Executor，因此关闭一次即可唤醒所有等待者并让它们排空。

pollOne/poll/run/requestStop 直接访问 control Executor；每条 CPU LogicalQueue 的 route 转发至共享 CPU Executor。close 和 empty 分别覆盖 control/CPU 两个执行域；token 查询只访问固定 route 表，不使用 Worker 下标。

run 复用控制 Executor 的 waitOne，不在 Context 另造 atomic+CV 状态。requestStop 只停止控制 wait loop；CPU Executor 的 Worker 继续运行。close 才关闭两个 Queue，唤醒 CPU Workers 并让它们处理所有已接纳 record。empty 只观察排队节点，不证明没有正在执行或未来投递的 root。

### 5.4 用户直接使用的 SchedulerHandle

用户句柄为 SchedulerHandle<Executor>；它直接借用 LogicalQueue，提供 scheduler_concept/schedule/相等比较，满足标准 execution scheduler 协议，同时保留 Core record 提交能力。一次调度只需要这一层句柄。

用户调用 context.getSchedulerHandle(token)，得到 SchedulerHandle，不使用 optional，也不经过 as_scheduler。独立返回 executor 用 SchedulerHandle{caller_queue}；两条路径使用同一类模板，具体模板实参可不同。Context 内的句柄借用 route；route 选择 control/CPU Executor，无每次投递分配。value completion scheduler 的查询返回句柄本身。

BasicContext<ControlExecutor, CpuExecutor, Thread> 返回统一的 SchedulerHandle<ContextExecutorRoute<...>>；route 按 ContextInfo 的 QueueTarget 转发，允许 control/CPU 使用不同 Queue。Context = BasicContext<> 保留 MutexInbox 多 producer 默认配置；单 producer 专用配置由宿主显式选择。SchedulerHandle 承担标准 scheduler adapter 职责；queue/executor/worker 只消费各自需要的基础接口。

getSchedulerHandle 与 findLogicalQueue 的前置条件是 token 已由构造此 Context 的配置成功签发；错误归属属于调用方契约违例，findLogicalQueue 在 debug 构建中 assert 域 ID，随后按 token 下标直接返回引用。域相同即保证下标来自该配置的已登记队列，不制造正常路径的“可能没有句柄”。token 不延长 Context/route 寿命，句柄仍须遵守存活约束。

SchedulerHandle 位于 core/scheduling，Context 常用别名随 BasicContext 定义；标准 sender/operation 适配位于 core/scheduling/details，用户只使用公开句柄和标准 CPO。Core 按需包含协议、operations 和 queries 头，不包含 execution.h，不调用 then/when_all 等组合算法，也不使用 stdexec 私有类型。protocols 和 execution 的基础接口不反向依赖 Core，因此没有循环。

core.h 聚合 Context、SchedulerHandle 和标准协议，因而需要选定的 execution provider。当前 Queue、Executor、Worker 经 core/concepts.h 传递加载 provider；ScheduleNode、固定存储与仅含配置宏的头可独立包含。组件不调用 execution 组合算法，与是否传递加载 provider 是两个不同维度。

`include/async/core/scheduling/details/exec_schedule_adapters.h`：

```cpp
#pragma once

#include <async/core/operations.h>
#include <async/core/scheduling/ScheduleNode.h>
#include <async/core/concepts.h>
#include <async/execution/operations.h>
#include <async/execution/queries.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace lcf::async::details {

using ScheduleCompletions = protocols::completion_signatures<
    exec::set_value_t(), exec::set_stopped_t()>;

template <std::copyable Handle, receiver_of_c<ScheduleCompletions> Receiver>
class ScheduleOperation
{
public:
    using operation_state_concept = protocols::operation_state_t;

    ScheduleOperation(Handle handle, Receiver receiver)
        noexcept(std::is_nothrow_copy_constructible_v<Handle>
            && std::is_nothrow_move_constructible_v<Receiver>)
        : m_handle(handle), m_receiver(std::move(receiver)), m_node(this, &execute) {}

    ScheduleOperation(const ScheduleOperation &) = delete;
    ScheduleOperation & operator=(const ScheduleOperation &) = delete;
    ScheduleOperation(ScheduleOperation &&) = delete;
    ScheduleOperation & operator=(ScheduleOperation &&) = delete;

    ScheduleNode & getScheduleNode() noexcept { return m_node; }

    void start() & noexcept
        requires executor_c<Handle, ScheduleOperation>
    {
        if (try_enqueue(m_handle, *this) == EnqueueResult::eAccepted) {
            return;
        }
        exec::set_stopped(std::move(m_receiver));
    }

private:
    static void execute(void * state_p) noexcept
    {
        auto & operation = *static_cast<ScheduleOperation *>(state_p);
        const auto & environment = exec::get_env(operation.m_receiver);
        if (exec::get_stop_token(environment).stop_requested()) {
            exec::set_stopped(std::move(operation.m_receiver));
            return;
        }
        exec::set_value(std::move(operation.m_receiver));
    }

    Handle m_handle;
    Receiver m_receiver;
    ScheduleNode m_node;
};

template <std::copyable Handle>
struct ScheduleSender
{
    using sender_concept = protocols::sender_t;
    using completion_signatures = ScheduleCompletions;

    struct Environment
    {
        Handle m_scheduler;

        Handle query(exec::get_completion_scheduler_t<exec::set_value_t>) const noexcept
        {
            return m_scheduler;
        }
    };

    Environment get_env() const noexcept { return {m_scheduler}; }

    template <receiver_of_c<ScheduleCompletions> Receiver>
        requires executor_c<Handle, ScheduleOperation<Handle, Receiver>>
    auto connect(Receiver receiver) const
    {
        return ScheduleOperation<Handle, Receiver>{m_scheduler, std::move(receiver)};
    }

    Handle m_scheduler;
};

} // namespace lcf::async::details
```

`include/async/core/scheduling/SchedulerHandle.h`：

```cpp
#pragma once

#include "LogicalQueue.h"
#include <async/core/scheduling/details/exec_schedule_adapters.h>

namespace lcf::async {

template <typename Executor>
class SchedulerHandle
{
public:
    using scheduler_concept = protocols::scheduler_t;

    explicit SchedulerHandle(const LogicalQueue<Executor> & queue) noexcept
        : m_queue_p(&queue) {}
    SchedulerHandle(const LogicalQueue<Executor> &&) = delete;

    auto schedule() const noexcept;

    template <typename Record>
        requires executor_c<const LogicalQueue<Executor>, Record>
    EnqueueResult tryEnqueue(Record & record) const noexcept
    {
        return try_enqueue(*m_queue_p, record);
    }

    bool operator==(const SchedulerHandle &) const noexcept = default;

private:
    const LogicalQueue<Executor> * m_queue_p;
};

template <typename Executor>
auto SchedulerHandle<Executor>::schedule() const noexcept
{
    return details::ScheduleSender<SchedulerHandle>{*this};
}

} // namespace lcf::async
```

connect 构造不可移动的稳定 operation，receiver_of_c 检查确切的 value()/stopped() 签名；实际 Record 完整后再检查 executor_c。start 成功发布后不能再访问可能已销毁的 operation，拒收则在 start 线程同步 stopped。执行时读取 receiver stop token，已接纳操作在 owner 驱动时交付 value 或 stopped。MVP 不注册主动取消 callback，请求 stop 后仍需 owner 推进。

sender 的 environment 只对 set_value 声明 completion scheduler；拒收的 stopped 可以在 start 线程发生，不能伪称无条件属于目标 executor。上层 continues_on 在目标接纳且 completion 存储不抛异常时，把上游三种完成一起送回目标；这与 schedule sender 自身的声明不是同一层保证。

### 5.5 聚合入口和 modules

`include/async/core.h`：

```cpp
#pragma once

#include <async/core/concepts.h>
#include <async/core/enums.h>
#include <async/core/operations.h>
#include <async/core/context/ContextInfo.h>
#include <async/core/context/BasicContext.h>
#include <async/core/scheduling/poll_structs.h>
#include <async/core/thread_types.h>
#include <async/core/scheduling/ScheduleNode.h>
#include <async/core/scheduling/LogicalQueue.h>
#include <async/core/scheduling/BasicExecutor.h>
#include <async/core/scheduling/BasicWorker.h>
#include <async/core/scheduling/SchedulerHandle.h>
#include <async/core/scheduling/queue/MutexInbox.h>
```

`include/async/async.h`：

```cpp
#pragma once

#include "core.h"
#include "execution.h"
```

`execution.h` 只聚合标准协议、操作 CPO、查询和按类别拆分的算法头，不直接定义算法重导出，也不包含 core/concepts.h。工厂 just/just_error/just_stopped 位于 factories.h；then/upon_*/let_* 位于 continuations.h；when_all 位于 composition.h；starts_on/continues_on/on 位于 scheduling.h。各类别直接依赖 config/execution_provider.h，不互相依赖算法聚合入口，公开名称仍在 exec。Core 通过 protocols 各角色头或 protocol.h 使用标准词汇，通过 execution/operations.h、execution/queries.h 使用基础接口；不包含这些算法头。能力检测与默认配置位于 config/features.h；provider 加载及 async::stde 位于 config/execution_provider.h，config.h 只聚合配置子头。modules 在 global module fragment 中先包含 config/execution_provider.h 加载外部 provider，再导出项目声明；namespace alias 不会让 provider 的全部声明随 import 可见，公共使用入口仍是 protocols/exec。接入正式源码时，cmake/project/config.cmake 自动收集 include/async 下的 .h 文件；同步更新 modules/async.cppm 的公共声明导出和对应标准头导入，分别验证 header 与 module 入口。这里的参考代码先用于普通头文件模式的组合验证，不表示完整运行时的 module 导出已经实现。

## 6. 内存行为和扩展点

### 6.1 allocator、resource 与容量各自负责什么

`std::pmr::polymorphic_allocator<T>` 是容器/typed storage 使用的类型化分配器；它把 `n` 个 T 转为字节数和对齐，交给 `memory_resource`。resource 决定字节从哪里来、是否缓存、何时归还上游；它不负责决定需要多少个元素，也不负责构造对象。标准 PMR 容器接收 resource 指针是合法的便捷构造，内部仍使用 polymorphic_allocator；问题在于是否声明了实际容量和生命周期，而不是有没有出现 resource 参数。

本轮用 FixedCapacityVector 保存固定运行时数组：配置给定 worker 数和实际 route 数，每个非空数组恰好一次 `allocator.allocate(count)`，无几何扩容或分块表。不能简单把不可移动的 Executor/Worker 改装进需要元素移动的 vector。factory 的 placement new 在已经分配好的地址构造对象，不再次申请堆内存。临时 ContextInfo 配置使用普通 `std::vector`，随登记自然增长，独立于运行时 typed-storage 资源。

`storageRequirement(s)` 报告 typed storage 的请求字节数/对齐，不是任意 resource 的上游保留量。第 1 节给每次固定申请提供正确对齐的缓冲区和 null upstream，容量不足立即抛 bad_alloc，不偷偷转到系统堆；真实数量来自 info 时用 `Context::storageRequirements(info)` 规划宿主 arena。若合并多个数组到一个 arena，还必须计算各段的对齐填充。pool 的 chunk、簿记和缓存空间另外计量，不能拿 count * sizeof(T) 宣称覆盖其全部成本。

### 6.2 MVP 的逐项申请审查

| 位置/所有者 | 申请与容量 | allocator/后端、释放与线程 |
| --- | --- | --- |
| ContextInfo bindings | 普通 `std::vector<QueueBinding>`；按实际登记数增长，无应用层预设容量 | `std::allocator`；构造期使用，配置 move 进 Context 后在构造结束释放；不属于 Context PMR 覆盖范围 |
| token 域 ID | 一个静态 atomic；签发只使用已分配的配置位置 | 无动态申请；不以堆对象地址作为可复用的身份 |
| Context 控制 executor | Context 内嵌成员 | 不为该对象单独申请；其 Queue 的内部行为单列 |
| CPU Executor Queue | Context 内嵌；与 N 个 Worker 共享，不随 worker_count 分配副本 | ExecutorFactory 配置该 Queue 的 allocator/容量；close 后 Worker 排空，join 后释放其内部资源 |
| Workers 对象数组 | worker_count 个 Worker，一个固定块；只存驱动者对象 | ContextAllocators::m_workers 重绑定到 Worker；不代表线程栈/启动状态也由它分配；销毁前须 join |
| LogicalQueue routes | 实际登记数个路由，一个固定块，O(1) token 查询 | ContextAllocators::m_routes 重绑定到路由类型；构造后无增长；最后一个 handle/operation 访问结束后才能释放 |
| 默认 MutexInbox | head/tail、锁、CV 和状态内嵌，不为每次投递分配 node | 不接收无用 allocator；节点由 operation 提供；mutex/CV 的系统实现边界单列 |
| 自定义 Queue backing storage | 由具体 Queue 的容量/结构决定，不能计入 Executor 对象大小 | ExecutorFactory 分别配置 control/CPU Queue 实际消费的 allocator、容量和回收策略；多个 Worker 共用 CPU Queue；构造失败回滚 |
| SchedulerHandle、内部 ScheduleSender/ScheduleOperation、Receiver、ScheduleNode | 本例栈/operation 内嵌存储；sender 与 operation 的组合类型决定大小 | adapter 不额外 new hook、record 或 owning callback；connect 的稳定 operation 活到最终完成返回，完成允许立即销毁自身 |
| when_all/continues_on 的完成存储 | 当前锁定 stdexec 的本例路径为 operation 内嵌 tuple/variant、子 operation 与 inplace stop 状态 | 这条路径的组合状态不另申请堆；值类型与异常运行库的申请另计，provider/算法或定制 domain 变更需重新审查，不承诺任意第三方算法均如此 |
| Worker 线程启动状态、栈和系统句柄 | 标准库/OS 管理；std::thread 没有标准 allocator 注入接口 | StdThreadFactory 明确是普通后端，PMR 不覆盖；严格控制时注入自定义 Thread/ThreadFactory，声明栈来源、启动存储、失败回收和 join；也可由宿主拥有线程、直接驱动 executor |
| mutex/condition_variable 的系统状态 | 平台实现可能使用额外系统资源 | 不属于 async 元数据 allocator；若要求连该部分也可控，则替换 Queue/等待后端并审查平台实现，不添加被忽略的 PMR 参数 |
| 示例调用方 std::thread、iostream、异常及用户值 | 应用/运行库自己负责 | 调用方线程创建失败先关闭/join Context；示例不以这些成本验证 async 元数据零堆申请；严格验收以无日志、受控线程和不分配的值类型隔离测量 |
| factory/allocator 的自定义状态 | 可以借用资源，也可以自己拥有动态存储 | 工厂复制、调用和销毁的行为须声明；传入 PMR allocator 不会自动改变自定义 policy 的内部分配 |

这份表覆盖本轮所有 typed allocate、placement construction、线程创建及可能申请系统资源的位置。timer、coroutine frame、owning post 不在 MVP；后续加入时必须增加各自的容量、失败和回收条目，不能沿用一句“使用 Context resource”。

当前项目锁定 stdexec `dfc8c95718e8c66ff5496bc3b185a9162c93821e`。已检查本例实际使用的实现：`__opstate` 直接保存 state/child operations，when_all 把计数、tuple/variant 和 inplace stop 状态存为成员，continues_on 直接保存 completion storage 和目标 schedule operation，variant 使用内嵌对齐字节数组。本例的 BranchResult/GraphResult 不拥有动态数据，因此这些组合状态不产生额外堆申请；throw/runtime_error/exception_ptr 的异常存储不因此被 PMR 接管。可按版本复核 [basic sender](https://github.com/NVIDIA/stdexec/blob/dfc8c95718e8c66ff5496bc3b185a9162c93821e/include/stdexec/__detail/__basic_sender.hpp)、[when_all](https://github.com/NVIDIA/stdexec/blob/dfc8c95718e8c66ff5496bc3b185a9162c93821e/include/stdexec/__detail/__when_all.hpp)、[continues_on](https://github.com/NVIDIA/stdexec/blob/dfc8c95718e8c66ff5496bc3b185a9162c93821e/include/stdexec/__detail/__continues_on.hpp) 和 [variant](https://github.com/NVIDIA/stdexec/blob/dfc8c95718e8c66ff5496bc3b185a9162c93821e/include/stdexec/__detail/__variant.hpp)。

### 6.3 正确使用与后续扩展

第 1 节将临时 ContextInfo 配置交给普通 `std::vector` 管理；Context 构造后该配置存储释放。Worker 与 route 数组分别绑定到有界 arena；control/CPU Executor 内嵌，Queue backing storage 由 ExecutorFactory 管理。生命周期、分配/释放线程和失败策略一致时，宿主可以显式让多个运行时 allocator 使用同一 resource。异构需求仍保留独立入口，尤其不把 Queue backing storage 和线程栈混入运行时元数据 arena。

这些数组的分配/释放只发生在宿主构造/销毁路径，所以不因 Worker 存在就必须用 synchronized_pool_resource。queue 可能在 producer/consumer 两端分配释放时，应使用线程安全后端，或把回收显式归并到 resource owner。resource 与初始缓冲区必须存活到最后一次 deallocate；arena.release 不是对象析构，也不能在消费者仍引用存储时调用。配置块在 Context 构造成功返回后已经消费完毕，可单独 release；运行时 Worker/route 两个 arena 需保持到 Context 销毁之后。

配置 vector 内部的分配失败和最大容量异常仍按标准容器契约抛出，但登记接口是 `noexcept`，所以异常越过接口时会终止；这不构成可恢复的错误码接口。运行时数组另验证资源不足、乘法溢出、对齐、零数量、部分元素构造失败及线程创建失败。统计区分配置期标准分配、PMR 请求量与上游量；用会失败的上游证明运行时数组无 fallback，而不只是数一次成功分配。若要从分配失败中恢复，需由不抛异常的实际后端提供显式错误结果，不能仅给标准 allocator 外层加 `noexcept`。

receiver allocator 覆盖适用于实际拥有额外动态状态的 operation，而不是反向改写已建好的 Context 存储；需保存原释放能力，不假定所有 allocator 都有 resource()。错误码分配能力 `allocation_resource_c`、typed storage 与 FramePolicy 按 [CORE_DESIGN.md](../CORE_DESIGN.md) 第 4/8 节扩展。队列结构、timer index、分配缓存、等待策略和默认算法选择由 benchmark 驱动。

## 7. 实现顺序

每步保留可构建状态，先用实际 provider 检查跨层签名。各阶段都保持第 1 节的线程归属：主线程消费合流值，调用方线程接收最终结果。

| 增量 | 实现与验证 | 完成条件 |
| --- | --- | --- |
| M1 协议与单步执行 | 第 4 节 concepts、enums、CPO 与标准概念转发；ScheduleNode、MutexInbox、FastProducerSpmcInbox、BasicExecutor、ContextExecutorRoute | 基础运行时头独立包含；control 与 CPU 可使用不同 Executor；合法/非法 Record/Executor 可区分；手动 poll 执行一条 record，拒收和完成中销毁正确；CPU producer 不等待 consumer mutex |
| M2 等待与停止 | blocking/stoppable queue；waitOne、持久停止 predicate、close/wake；基础 Worker | 等待不空转、不丢唤醒；停止先于 run、等待中停止、record 边界停止均正确；停止后排队工作仍可 poll |
| M3 运行时组合 | ContextInfo 用 std::vector 动态登记 QueueTarget、WorkerCount 和直接返回 token；FixedCapacityVector 保存运行时 Workers/routes；control/CPU ExecutorFactory 与共享 CPU Executor | 多于既有预估数的队列可登记；登记函数编译期验证为 noexcept，扩容分配失败终止；token 下标为 std::size_t 且不绑定 Worker；运行时 metadata 构造期分配、运行时不增长；run 只驱动 control；显式 close/join |
| M4 execution 与返回域 | provider 别名、Core SchedulerHandle、内部 sender/operation；独立调用方 route/executor | 先验证零 Worker 的 control-only Context 通过 pollOne/poll 推进 schedule→then→receiver，禁止依赖 run 或后台线程；再验证标准 scheduler 句柄和完整程序得到 42：两 Worker → 主线程 continuation → 调用方 Receiver；子线程持续心跳，最终 requestStop 唤醒主线程 |
| M5 生命周期和基线 | value/error/stopped、取消/拒收、关闭竞争、PMR/线程创建失败、完成中销毁；接入 verify/benchmark/modules | 第 8 节必测项通过；建立空 pollOne、no-op dispatch、run/wake、两 Worker 合流、调用方返回和内存基线；receiver 执行时间单列 |

上述步骤无需 C++ coroutine。Task、co_await、timer、expected 扩展在运行时骨架稳定后按设计文档继续，不作为本轮验收的隐藏前置依赖。

## 8. 验收矩阵

测试按可观察契约划分，接入现有 verify target；完整示例可放 sandbox 直接运行，并把 main 主体提取为 verify helper。CTest 给整个测试设超时，出现丢完成/死锁时进程失败；不用超时后释放活跃 operation 的方式恢复。并发竞态用 latch/gate 或测试 queue 控制，不用 sleep 猜测时序。

| 必测项 | 观察结果 |
| --- | --- |
| poll 驱动 execution | 零 Worker、仅 control queue；connect/start 后无完成；pollOne 完成一项工作，poll 按预算推进，value 恰好一次；关闭后排空 |
| SPMC CPU 提交 | 显式选择 FastProducerSpmcInbox，默认 Context 保持多 producer 能力；明确单 producer 与多个 consumers 的归属；consumer 持锁暂停时 producer 仍能 enqueue；close 与进入提交区但尚未发布的 producer 竞争时 Worker 不提前退出，接纳节点 exactly-once 领取；不得将 SPMC handle 当作多 producer 通用入口 |
| 完整成功链 | 两个 CPU LogicalQueue 的分支在同一 gate 同时就绪；共享 Executor 由两个 Worker 消费；主线程 then 得到 42；最终 Receiver 在调用方线程；随后 run 返回、join 完成 |
| error 返回 | 在最后 continues_on 前注入 just_error(std::error_code) 或抛异常的 then；跳过后续 value 函数；set_error 恰好一次、在调用方线程、唤醒主线程退出 |
| stopped 返回 | 在最后 continues_on 前注入 just_stopped；set_stopped 恰好一次且在调用方线程；不依赖成功分支才能退出 |
| owner 隔离 | Context run/poll 只消费 control Executor，不消费 CPU/调用方返回 Executor；CPU Workers 只消费 CPU Executor；仅调用方 pollOne 执行最终 Receiver |
| 真正并行 | 两个受控 gate 证明共享 CPU Executor 的两个 Worker 可同时领取并执行分支；不使用耗时阈值或 Worker 身份 作为业务 API 证明并行 |
| 等待与竞态 | 空队列等待、新投递唤醒、虚假唤醒重查 predicate、检查空与入睡竞争、requestStop 前于 run/等待中/有工作时均无丢唤醒 |
| 停止和关闭独立 | requestStop 后仍能接纳，剩余 record 可 poll；run 再次调用立即返回；close 拒收但不丢已接纳节点；Worker 不受控制 executor stop 影响 |
| 配置与逻辑身份 | control 目标可动态登记数千项且直接返回 token；登记函数为 noexcept；token 为 32 位 domain + std::size_t index；CPU queue 不指定 Worker；无 Worker、配置已移走或 vector 扩容分配失败时终止；跨 Context token 违反 lookup 前置条件，debug assert 检出；共用 CPU Executor 的不同 route handle 仍不相等 |
| 固定存储与资源 | 每个非空运行时数组恰好一次、准确 count/sizeof/alignment；零数量不申请；ContextInfo vector 按需增长并在构造后释放；add/start/poll 不增长运行时 metadata；null upstream 证实 PMR 无 fallback；runtime arenas 按各自 allocator 回收 |
| 构造失败与扩展 | 逐点注入数组分配、executor 构造、thread factory 失败；部分元素逆序销毁、已启动线程全部 close/join；自定义 queue 消费自己的 allocator；线程后端扩展实际被使用 |
| 存活与竞争 | eAccepted 后立即消费、完成中销毁、producer/close 竞争恰好一次；最后访问结束前不释放 route/executor/operation；跨线程回收遵守后端契约 |
| 失败边界 | 提前关闭返回 scheduler 可在来源线程 stopped；单独断言完成/存活，不在来源线程修改 owner 专有状态；completion 存储抛异常按 provider 契约测试 |
| 分层和配置 | 基础 queue/Executor/Worker 不调用 execution 算法；Queue 声明多 consumer 能力，BasicContext 拒绝不兼容 Executor；SchedulerHandle 满足标准 scheduler 接口，header/module 入口一致 |

普通 source 抛出的异常与最后 continues_on 存储 completion 时抛出的异常要区分：前者在本例的正常存储路径下转回调用方，后者可能在迁移前交付失败。基础 schedule 的 receiver stop token 检查也不等于取消所有已启动业务；sleep_for 的重工作不受 requestStop 抢占。

性能基线分别测量空 `pollOne`、空 poll、no-op dispatch、SPMC producer enqueue、consumer mutex 等待、run 入睡/唤醒、停止、批量软预算、两 Worker 合流、控制 continuation 和最终返回延迟；receiver 用户代码耗时单独统计。比较单 producer 入队延迟和多 consumer 领取/吞吐，不把 receiver 执行时间计入 executor 开销。基准不代替平台级 WCET 证明。benchmark 不保留演示用 1 秒 sleep、100ms 心跳或日志；其他 queue/timer/wake 优化先存基线，再以相同 workload 比较，不能从整轮均值推导 p99。

## 9. 接入与验证命令

正式实现头文件放入 include/async 后，由 cmake/project/config.cmake 自动收集；verify/benchmark 源文件加入各自 target，沿用延迟依赖和 provider 检测，不为文档代码另造一套正式构建。首次接入运行现有 preset：

```text
cmake --preset clang
cmake --build build/clang --config Debug --target async_verify_run
cmake --build build/clang --config Release --target async_benchmark_run
```

完成 module 导出后再运行对应 modules preset；平台支持的 MSVC/GCC 组合也需要验证。当前公共 Core 仍是骨架，现有 execution verify 通过不代表上述 MVP 已实现。参考源码保存在 `.mvp/include/async/`，主示例为 `.mvp/example.cpp`；编译参考示例时使用该目录的 include 路径与选定 provider，构建产物保存在项目 build 目录或 `.agent` 工作目录，不写入 `.mvp`。参考验证不代表正式库已交付。

benchmark 结果按现有 target 写入 results/Release/latest.json；记录 commit、provider、编译器、配置、平台、worker/producer 数、队列实现、资源与 workload，比较前另存基线。长期需求、参考资料和后续能力见 [CORE_DESIGN.md](../CORE_DESIGN.md)。
