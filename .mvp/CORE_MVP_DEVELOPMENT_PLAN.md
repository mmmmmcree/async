# Async Core MVP 开发计划

下面的完整示例是本轮已确认的验收目标；后文按它拆分协议和实现。本目录的 `include/async/` 和 [example.cpp](example.cpp) 保存参考实现与主示例，文中实现文件路径以本目录为基准。项目根目录下的正式 `include/async/` 仍是声明骨架，参考代码须接入项目后再作为公共 API 使用。`.mvp` 是纳入版本控制的临时开发材料，完成正式 MVP 后可整体删除。

## 1. 最终验收示例

调用方子线程是 Context 的唯一 producer，通过 addScheduler 注册的两个逻辑目标启动独立 sender，共享 SPMC Executor 由 N 个同类 Worker 与宿主 run 共同驱动。when_all 合流后继续计算，再通过 caller scheduler 返回调用方。业务不指定线程，不承担同步控制；gate 仅用于 verify 的并行性测试。本 MVP 不依赖 C++ coroutine。

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

// Only the caller thread submits operations to this Context.
using WorkQueue = async::queue::intrusive::spmc::Queue<>;
using ExampleContext = async::BasicContext<async::BasicExecutor<WorkQueue>>;

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
    std::thread::id m_continuation_thread;
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
public:
    ~Receiver() noexcept = default;
    Receiver() = delete;
    Receiver(ExampleContext * context_p, Outcome * outcome_p, bool * caller_completed_p,
        std::thread::id caller_thread) noexcept
        : m_context_p(context_p), m_outcome_p(outcome_p),
          m_caller_completed_p(caller_completed_p), m_caller_thread(caller_thread) {}
    Receiver(const Receiver &) noexcept = default;
    Receiver(Receiver &&) noexcept = default;
    Receiver & operator=(const Receiver &) noexcept = default;
    Receiver & operator=(Receiver &&) noexcept = default;
public:
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
    const auto main_thread = std::this_thread::get_id();

    async::ContextInfo info{k_worker_count};

    const auto left_token = info.addScheduler();
    const auto right_token = info.addScheduler();

    constexpr auto k_storage = ExampleContext::storageRequirements(k_worker_count);
    alignas(k_storage.m_workers.m_alignment)
        std::array<std::byte, k_storage.m_workers.m_bytes> worker_bytes;
    std::pmr::monotonic_buffer_resource worker_arena{
        worker_bytes.data(), worker_bytes.size(), std::pmr::null_memory_resource()};

    ExampleContext context{std::move(info), async::ContextAllocators{
        std::pmr::polymorphic_allocator<std::byte>{&worker_arena}}};

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
            auto caller_scheduler = async::SchedulerHandle{caller_executor};
            auto left = ex::schedule(left_scheduler) |
                ex::then([] {
                    std::this_thread::sleep_for(1s);
                    return BranchResult{20, std::this_thread::get_id()};
                });

            auto right = ex::schedule(right_scheduler) |
                ex::then([] {
                    std::this_thread::sleep_for(1s);
                    return BranchResult{22, std::this_thread::get_id()};
                });

            auto graph = ex::when_all(std::move(left), std::move(right)) |
                ex::then([](BranchResult lhs, BranchResult rhs) noexcept {
                    // The completing execution agent consumes the merged values.
                    return GraphResult{
                        lhs.m_value + rhs.m_value,
                        lhs.m_worker_thread,
                        rhs.m_worker_thread,
                        std::this_thread::get_id()};
                }) | ex::continues_on(caller_scheduler);

            bool caller_completed = false;
            auto operation = ex::connect(std::move(graph), Receiver{&context, &outcome, &caller_completed, caller_thread});

            ex::start(operation);

            std::size_t heartbeat_count = 0;
            while (not caller_completed) {
                std::cout << "caller heartbeat: " << ++heartbeat_count << '\n';
                caller_executor.executeOne();
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
        && result.m_continuation_thread != caller_thread
        && result.m_left_thread != caller_thread
        && result.m_right_thread != caller_thread;

    std::cout << "result: " << result.m_value << '\n';
    std::cout << "acceptance: " << (passed ? "passed" : "failed") << '\n';
    return passed ? 0 : 1;
}
```

```text
调用方 connect/start → 两个逻辑调度目标 → 共享 Executor
                                     ↓ 任意可用执行线程
                                两个独立分支 → when_all → then：20 + 22
                                     ↓ continues_on(caller_scheduler)
调用方心跳循环 executeOne → Receiver → Context requestStop
                                     ↓
宿主 run 返回 → join 调用方 → close/join Workers
```

caller_completed 只在调用方线程读写；宿主通过 requestStop 唤醒，并在 join 后读取 Outcome。两个 sleep 只模拟耗时，不构成异步等待或并行证明。调用方的 100ms sleep 是宿主循环选择，会增加结果处理延迟。Context run 与后台 Workers 可共同领取两个分支，不保证合流 continuation 命中某条线程。

## 2. 范围、执行域与关闭契约

### 2.1 本轮包含的能力

| MVP 包含 | 后续能力 |
| --- | --- |
| 一个 Executor、N 个同类 Worker 与宿主 poll/run 共同执行 | Worker 动态增删、work stealing、异构执行域映射 |
| 调用方直接拥有和驱动返回 Executor；复用同一 scheduler adapter | 多种外部 render/gfx executor 的通用注册与 Context 借用 API |
| 默认 queue::intrusive::mpmc::Queue<> MPMC；示例显式选择 queue::intrusive::spmc::Queue<> SPMC | non_intrusive 预分配有界指针队列及 record/hook 适配、eFull 完成语义、多 producer 独立 lane 和可配置等待策略 |
| addScheduler 登记稳定逻辑身份，全部共用 Executor | 可选 QueueConfig 的优先级、公平性、并发上限、容量与亲和性 |
| Core 可执行对象/record/submitter/executor/queue concepts；标准 scheduler adapter | Task、timer、取消 registration、expected 与 frame 策略 |
| 显式 PMR、内嵌 operation/node、构造回滚、关闭/drain/join | receiver allocator 覆盖、无异常 typed storage 和资源失败交付 |

### 2.2 Executor 与 Workers 的关系

Context 内嵌一个 Executor，N 个同类 BasicWorker 共享它；poll/run 的调用线程也可加入消费。SchedulerHandle 自带稳定逻辑身份并直接引用该 Executor，不区分 control/CPU，不保存 Worker 下标。零 Worker 配置通过同一个 addScheduler 登记，完全由宿主 poll/run 驱动。

默认 Context 使用 queue::intrusive::mpmc::Queue<>，支持多 producer。示例 Context 使用 queue::intrusive::spmc::Queue<>，caller 是整个共享 Queue 的唯一 producer；后台 Worker 和主线程 run 都是 consumer。独立的 caller_executor 使用 queue::intrusive::mpmc::Queue<>，只有调用方驱动它执行最终 Receiver。

SPMC 提交不分配、不获取 consumer mutex，原子步骤固定。consumer 在自身 mutex 下领取；发布缺口返回空，发布完成后通知等待者。close 等待在途提交完成后唤醒；关闭且 producer 不再发布、队列为空时才退出。通知、抢占与平台调度影响墙钟时间，不宣称通用硬实时上界。

run 不保证主线程亲和性；需要固定 owner 时，当前使用独立的零 Worker Context 或自行驱动的 Executor。后续按需求加入 addScheduler(QueueConfig)，表达优先级、并发度、亲和性和独立入口。SchedulerHandle 不拥有 Executor，Context 不关闭调用方独立返回域。

### 2.3 run、requestStop、close、join 的区别

| 操作 | 契约 |
| --- | --- |
| Context::run() | 当前线程等待并执行共享工作，直至停止或关闭且排空 |
| Context::requestStop() | 可跨线程请求所有等待驱动者在 record 边界退出，包括后台 Worker；不关闭准入、不取消 root、不丢弃节点 |
| Context::pollOne/poll | 驱动同一个 Executor，不等待新工作；停止后可显式排空 |
| Context::close() | 关闭新准入并唤醒等待者；未停止的驱动者继续排空；不直接执行 callback |
| Context::joinWorkers() | 等待自有 Worker 退出；宿主先 close 或 requestStop，禁止 join 自身 |

停止请求先于 run 也有效，MVP 不重置停止状态。已取出的 record 执行至返回，其余节点仍在 Queue；若已 requestStop，宿主须显式 poll/drain，不能依赖 close 重新启动 Worker。未停止时 close 后的等待驱动排空已接纳工作再退出。isEmpty 只表示排队为空，不证明 root 或正在执行的 callback 已完成。

默认 queue::intrusive::mpmc::Queue<> 在同一 mutex 下维护等待 predicate 与队列状态，SPMC 使用持久停止位及唤醒序号；虚假唤醒须重查状态。timer 与可配置等待策略后续加入，不在内部用固定 sleep/yield 代替唤醒协议。

### 2.4 完成线程及存活顺序

`connect` 构造稳定 operation，`start` 启动它；只有本例的 schedule operation 在 start 时投递，`just` 等 sender 可以同步完成。Receiver 自身不选择线程。`continues_on` 存储上游 completion 并调度目标操作，目标 owner 执行该操作时再调用下游；`get_completion_scheduler` 描述保证，查询本身不迁移。

正常验收保持目标 executors 准入，使用不抛异常的 value/error 存储和移动，因此最后的 `continues_on(caller_scheduler)` 把上游 value/error/stopped 都交给调用方线程。返回 scheduler 拒收或 completion 存储/转移抛异常是独立失败边界，不能假定失败仍在目标线程交付；另测恰好一次完成，不在来源线程访问 owner 专有状态。

本例只有一个 root。所有 executors 保持有效并被驱动 → 最终 Receiver 写入 Outcome/调用方 bool 并请求停止 → 主线程 run 返回后 join 调用方 → 调用方 executeOne 返回，operation/executor/resource 可销毁 → Context close/join workers → 释放 Context 和资源。run 返回不等于 Receiver 已经返回，不能省掉 join。多个 root 时由 root owner 计数，最后一个 root 才请求停止；空队列、一个 Receiver 完成或 requestStop 本身都不能代替 root drain。

析构不运行用户 callback，也不偷偷阻塞 drain/join。Context/Worker 要求宿主事先完成显式关闭、join 和最后访问；线程创建中途失败则由构造函数回滚已创建线程。进程超时用于检测死锁，不是销毁仍被异步访问的 operation 的许可。

## 3. 文件、依赖与命名

Core 的运行时组合类型、concept、配置值位于 lcf::async。core/queue 与 scheduling 并列，公开命名空间按 queue::intrusive/non_intrusive、mpmc/spmc/mpsc/spsc 分层，拓扑下的类型名统一为 Queue；算法目录只组织特化。MVP 实现 intrusive::mpmc 与 intrusive::spmc，非侵入式后端后续加入。标准角色词汇位于 protocols，操作/查询/算法位于 exec。普通类型 PascalCase，成员函数 camelCase，自由函数/参数 snake_case，concept 为 snake_case_c，普通常量使用 k_，枚举值使用 ePascalCase。标准协议定制名保持原样；只有实现辅助类型使用局部 details。

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
│   ├── queue/                # scheduling 的底层同级组件
│   │   └── intrusive/        # async::queue::intrusive
│   │       ├── ExecutionNode.h
│   │       ├── mpmc/
│   │       │   ├── AlgorithmTag.h
│   │       │   ├── queue_declarations.h
│   │       │   ├── Queue.h
│   │       │   └── mutex/Queue.h
│   │       └── spmc/
│   │           ├── AlgorithmTag.h
│   │           ├── queue_declarations.h
│   │           ├── Queue.h
│   │           └── exchange_linked/Queue.h
│   ├── scheduling/
│   │   ├── BasicExecutor.h
│   │   ├── BasicWorker.h
│   │   ├── SchedulerHandle.h
│   │   └── details/exec_schedule_adapters.h
│   ├── details/containers/FixedCapacityVector.h
│   └── context/
│       ├── ContextInfo.h
│       ├── BasicContext.h
│       └── poll_structs.h
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
| Core concepts/enums | 分别定义能力约束与接纳结果；成员接口直接调用，Queue 的 Executable 不要求节点布局 |
| core/details/containers | 管理元素生命周期、容量和访问；FixedCapacityVector 用显式 typed allocator，不依赖 Context 或 execution；资源策略属于 memory |
| PollBudget、PollReport | context/poll_structs.h 中的批量驱动包装值类型；由 Context 直接包含，不属于协议入口或 concepts |
| ThreadEntry | core/thread_types.h 中的线程入口函数类型；Core concepts 与 scheduling 共用，不依赖 scheduling |
| core/queue 的 ExecutionNode、具体 Queue | 只依赖 core/enums.h、节点和标准同步设施；不包含 Core concepts、scheduling 或 execution provider |
| Executor、Worker | 各自需要的基础协议，经 concepts.h 加载 provider；Executor 依赖 Queue，Worker 不包含具体 Executor |
| SchedulerToken | 配置阶段的域 ID 与目标下标；和 SchedulerHandle 同在 SchedulerHandle.h，无独立代理对象 |
| BasicContext | 组合共享 Executor、Workers、域 ID 和资源；按 token 直接返回标准 SchedulerHandle |
| execution.h | 聚合标准协议、操作、查询和组合算法；不包含 Core，重导出保持 provider 身份 |
| Core SchedulerHandle、scheduling/details/exec_schedule_adapters.h | Core 基础提交接口、Core 概念别名及标准操作/查询头；句柄直接实现 scheduler，内部完成 sender/operation 适配 |

公共 config.h 只作配置聚合。config/features.h 负责能力检测及 `LCF_ASYNC_USE_STD_EXECUTION` 的默认值，config/execution_provider.h 加载选定 provider，并在 lcf::async 定义 stde 别名；protocols 和 execution 的基础接口分别直接依赖 provider 子头。Core 的运行时约束与标准概念转发统一位于 concepts.h，因此依赖它的 Executor/Worker 等组件也加载 provider；具体 Queue、固定存储容器及仅需 macros/features 的头仍可独立使用。跨目录引用统一写成 `<async/...>`，同目录引用可保留相对文件名。定义按 concepts/enums/operations 分类，不增加没有消费需求的策略参数。

## 4. Concepts、枚举与提交入口

### 4.1 Record 和 executor

Record 是 adapter/宿主拥有的 operation，不要求继承固定基类。concept 直接检查成员接口，运行时直接调用成员，不增加同义 CPO。提交由二元 record_submitter_c<Submitter, Record> 描述，驱动由 work_executor_c 描述；Worker pool 还要求 multi_worker_executor_c，声明支持并发等待与领取。

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

template <typename Executable>
concept executable_c = requires(Executable & executable) {
    { executable.execute() } noexcept -> std::same_as<void>;
};

template <typename Record>
concept work_record_c = requires(Record & record) {
    { record.getExecutable() } noexcept;
    requires std::is_lvalue_reference_v<decltype(record.getExecutable())>;
    requires executable_c<std::remove_reference_t<decltype(record.getExecutable())>>;
};

template <typename Submitter, typename Record>
concept record_submitter_c = work_record_c<Record> and
    requires(Submitter & submitter, Record & record) {
        { submitter.tryEnqueue(record) } noexcept -> std::same_as<EnqueueResult>;
    };

template <typename Executor>
concept work_executor_c = requires(Executor & executor, const Executor & const_executor) {
    { executor.executeOne() } noexcept -> std::same_as<bool>;
    { executor.close() } noexcept -> std::same_as<void>;
    { const_executor.isEmpty() } noexcept -> std::same_as<bool>;
};

template <typename Executor>
concept waiting_work_executor_c = work_executor_c<Executor> and
    requires(Executor & executor) {
        { executor.waitAndExecuteOne() } noexcept -> std::same_as<bool>;
    };

template <typename Executor>
concept multi_worker_executor_c = waiting_work_executor_c<Executor> and
    requires {
        requires std::same_as<decltype(Executor::k_supports_multiple_workers), const bool>;
        requires Executor::k_supports_multiple_workers;
    };

template <typename Executor>
concept stoppable_work_executor_c = waiting_work_executor_c<Executor> and
    requires(Executor & executor) {
        { executor.requestStop() } noexcept -> std::same_as<void>;
    };

template <typename Factory, typename Executor>
concept context_executor_factory_c = work_executor_c<Executor> and
    requires(Factory & factory) {
        { factory.makeExecutor() } -> std::same_as<Executor>;
    };

template <typename Queue>
concept work_queue_c = requires {
        typename Queue::Executable;
        requires std::same_as<decltype(Queue::k_supports_multiple_producers), const bool>;
        requires std::same_as<decltype(Queue::k_supports_multiple_consumers), const bool>;
        typename std::bool_constant<Queue::k_supports_multiple_producers>;
        typename std::bool_constant<Queue::k_supports_multiple_consumers>;
    } and executable_c<typename Queue::Executable> and
    requires(Queue & queue, const Queue & const_queue, typename Queue::Executable & executable) {
        { queue.tryEnqueue(executable) } noexcept -> std::same_as<EnqueueResult>;
        { queue.tryPop() } noexcept -> std::same_as<typename Queue::Executable *>;
        { queue.close() } noexcept -> std::same_as<void>;
        { const_queue.isEmpty() } noexcept -> std::same_as<bool>;
    };

template <typename Queue>
concept waiting_work_queue_c = work_queue_c<Queue> and
    requires(Queue & queue) {
        { queue.waitPop() } noexcept -> std::same_as<typename Queue::Executable *>;
    };

template <typename Queue>
concept stoppable_work_queue_c = waiting_work_queue_c<Queue> and
    requires(Queue & queue) {
        { queue.requestStop() } noexcept -> std::same_as<void>;
    };

// Join capability alone makes no claim about the kind of execution agent.
template <typename Handle>
concept join_handle_c = std::destructible<Handle> and
    requires(Handle & handle, const Handle & const_handle) {
        { const_handle.joinable() } noexcept -> std::same_as<bool>;
        { handle.join() } -> std::same_as<void>;
    };

// Semantic contract: launch entry exactly once on a separate thread, and return
// its join handle. A failed launch must leave no entry or thread in flight.
template <typename Launcher, typename ThreadHandle>
concept thread_launcher_c = join_handle_c<ThreadHandle> and
    requires(Launcher & launcher, ThreadEntry entry_f, void * state_p) {
        { launcher(entry_f, state_p) } -> std::same_as<ThreadHandle>;
    };

} // namespace lcf::async
```

`executable_c` 只要求 execute() noexcept -> void，不要求 Node、继承或链接字段。work_record_c 返回可执行对象的稳定左值引用，BasicExecutor 在实际提交点检查 Queue 的接纳关系。内部直接调用 getExecutable/tryEnqueue 成员，不增加只有转发作用的 Core CPO。record_submitter_c 保留实际 cv 限定；eClosed 不保留 record，eAccepted 后可能立即完成并销毁，提交方不得再次访问。eFull 与有界入口后续一起加入。

### 4.2 驱动、阻塞等待与停止

上述 concepts 按实际能力命名：record_submitter_c 只描述对某种 Record 的投递；work_executor_c 描述 executeOne/close/isEmpty；waiting_work_executor_c 再增加 waitAndExecuteOne。executeOne 不等待新工作，仍可能等待 Queue 的内部 mutex；waitAndExecuteOne 同时表达等待工作和执行一个 record。两者返回 true 均表示一个 record 执行完成，不代表全部 root 完成；后者返回 false 表示停止或关闭且排空，不应把虚假唤醒当作退出。

concept 的表达式检查只建立接口形状，模型还必须履行语义契约。多 Worker 标记不证明线程安全，join/joinable 也不证明另起线程；不靠添加无调用需求的 get_id/detach 或限定为 std::thread 来伪造证明。并发、完成同步、恰好一次、失败回滚和生命周期通过实现及测试验证。

BasicContext<Executor, ThreadHandle> 的 Executor 要求 multi_worker_executor_c，N 个同类 Worker 与宿主驱动者共享它；run/requestStop 额外要求 stoppable_work_executor_c。独立 BasicExecutor 可只提供 executeOne。context_executor_factory_c<Factory, Executor> 约束 makeExecutor 返回所需 Executor；仅默认工厂要求默认构造。

### 4.3 Queue

work_queue_c<Queue> 通过 Queue::Executable 检查可执行能力与接纳/取出/关闭/空查询，不规定节点类、继承、链表或默认构造。等待/停止分别由 waiting_work_queue_c、stoppable_work_queue_c 表达。

Queue 必须定义编译期 bool 常量 k_supports_multiple_producers 与 k_supports_multiple_consumers。concept 同时检查存在性、类型和常量表达式可用性，false 是合法声明。BasicExecutor 直接暴露对应的 k_supports_multiple_producers 与 k_supports_multiple_workers，不再检测缺省值。多 Worker 模型要求后者为 true；并发安全由实现兑现，concept 不能证明。

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

`ThreadEntry` 与 `ExecutionNode::ExecuteFunction` 在 C++ 类型系统中是同一函数指针类型；前者表达线程入口，后者表达一次节点执行，分别保留其语义名称。

join_handle_c 表示可析构并支持 joinable/join 的完成句柄，不把它称为“线程概念”。thread_launcher_c<Launcher, ThreadHandle> 检查启动调用与句柄返回类型的关系；实际线程语义由这一对实现共同提供：

| 能力 | 必须兑现的语义 |
| --- | --- |
| launcher(entry, state) | 成功后在独立执行线程恰好调用一次 entry(state)，返回的 join 句柄代表该执行；不能同步调用后返回假句柄 |
| 启动发布 | 启动前对 entry 可见状态的初始化与 entry 开始同步；entry 可早于 launcher 返回执行，不得访问尚未初始化的 Worker 线程句柄 |
| handle.joinable() | 表示句柄仍持有待 join 的执行关联，不能当作线程“正在运行”的判断 |
| handle.join() | 等待关联执行结束，完成与 join 返回同步，之后 joinable 为 false；禁止 join 自身 |
| 启动失败 | 抛异常前清理所有尚未交付的执行和启动存储，不留下之后仍会调用 entry 的线程 |
| launcher 存活 | 仅在构造期借用，无需可复制；异步线程状态不得借用已经销毁的 launcher |

仅有同名成员的对象可满足 join_handle_c 的表达式检查，但不能据此作为真实线程后端；伪造启动器同样可能通过类型检查而违反上述契约。默认 ThreadLauncher<std::thread> 直接构造 std::thread 兑现这些语义；其他句柄可注入自定义启动器，自定义后端必须单独验证。BasicWorker::join 是 noexcept，后端 join 报错会终止，不能继续释放仍被线程使用的存储。标准线程资源不受 PMR 控制。

### 4.6 typename 与 concept 的使用位置

单个类型的能力约束优先写成 `template <concept Type>`；concept 的其他参数已确定时，也直接写在模板参数中。跨类型关系、构造参数包和成员的额外启用条件使用 requires；不为缩短写法另建同义 concept。

| 参数位置 | 约束或保留 typename 的理由 |
| --- | --- |
| concept 定义 | C++ 禁止 concept 自身带关联约束，必须用 typename 接受候选类型，并在右侧组合已有 concept 与 requires |
| BasicExecutor<Queue> | work_queue_c 要求 Executable 和两个显式编译期 bool 并发常量；tryEnqueue 检查实际 Record 与 Queue 的关系 |
| BasicWorker<Executor, ThreadHandle> | waiting_work_executor_c、join_handle_c；多个 Worker 共用时由 Context 的 multi_worker_executor_c 检查 |
| BasicContext<Executor, ThreadHandle> | multi_worker_executor_c、join_handle_c、context_executor_factory_c 与 thread_launcher_c；所有 Worker 和 poll/run 共用 Executor |
| DefaultContextExecutorFactory<Executor> | work_executor_c；makeExecutor 按实际构造能力启用默认构造 |
| FixedCapacityVector<Value> | std::destructible；emplaceBack 检查构造关系，emplaceWith 检查 factory 的精确返回类型以支持不可移动元素 |
| SchedulerHandle<Executor> | 类保存 Executor 引用与身份，尚不知道实际 Record，保留 typename；tryEnqueue<Record> 检查二元 record_submitter_c；直接在 Core 实现标准 scheduler 协议 |
| QueueArgs... | std::constructible_from<Queue, Args...>，不能用一个空泛 concept 限定所有配置值 |
| 内部 ScheduleSender/Operation 的 Handle / Receiver | Handle 用 std::copyable 约束实际值复制，实例为已完成定义的 SchedulerHandle；Receiver 使用转发到标准协议的 receiver_of_c<Receiver, ScheduleCompletions> |
| ScheduleSender::connect、ScheduleOperation::start | 实际 operation 完整后再检查 record_submitter_c<Handle, Operation>；不在句柄定义期间递归检查 scheduler 概念 |
| 示例 Receiver::set_error<Error> | 记录通道，不读取 Error 专有接口，无需人为限定为单一错误类型 |

新增 concept 必须说明直接消费方、基础表达式和额外语义。concept 不能证明线程安全、地址稳定、恰好一次或资源寿命，下面另列这些契约。

具有行为的类显式声明析构、默认构造、拷贝/移动构造与赋值。Context、Executor、Worker、Queue、ExecutionNode 和 operation 保持地址及运行时状态稳定，禁止拷贝、移动；ContextInfo 仅允许移动构造以转移签发域。句柄、token、sender、环境和无状态工厂按值语义 default；ContextAllocators 的 PMR allocator 不可赋值，显式 delete 赋值。需要有效引用或配置的类型 delete 默认构造；纯数据聚合保留聚合初始化。

## 5. 可组合的参考实现

### 5.1 ExecutionNode 与 Queue 后端

队列位于 core/queue，与 core/scheduling 并列；依赖方向为 scheduling → queue。公开类型由存储类别、并发拓扑和算法选择共同确定，MVP 为 queue::intrusive::mpmc::Queue<> 与 queue::intrusive::spmc::Queue<>。

各拓扑分别定义 AlgorithmTag 枚举，当前仅登记 eMutex 或 eExchangeLinked。枚举类型隔离跨拓扑误用；主模板只声明，具体算法头定义特化，未实现的标识不能实例化，不隐式回退。新增内置算法时更新本地枚举并增加算法目录及验证。算法家族分类和最终 non_intrusive 支持范围见 CORE_DESIGN 第 5.2 节。

queue_declarations.h 提供枚举和主模板声明，具体算法头单向依赖声明；拓扑 Queue.h 聚合已有实现。两类头都可直接包含，避免聚合头与算法头互相 include。具体 Queue 只消费节点和接纳结果，不依赖 Core concepts 或 execution provider。

`include/async/core/queue/intrusive/mpmc/AlgorithmTag.h`：

```cpp
#pragma once

namespace lcf::async::queue::intrusive::mpmc {

enum class AlgorithmTag
{
    eMutex,
};

} // namespace lcf::async::queue::intrusive::mpmc
```

`include/async/core/queue/intrusive/mpmc/queue_declarations.h`：

```cpp
#pragma once

#include "AlgorithmTag.h"

namespace lcf::async::queue::intrusive::mpmc {

template <AlgorithmTag Algorithm = AlgorithmTag::eMutex>
class Queue;

} // namespace lcf::async::queue::intrusive::mpmc
```

`include/async/core/queue/intrusive/mpmc/Queue.h`：

```cpp
#pragma once

#include <async/core/queue/intrusive/mpmc/mutex/Queue.h>
```

`include/async/core/queue/intrusive/spmc/AlgorithmTag.h`：

```cpp
#pragma once

namespace lcf::async::queue::intrusive::spmc {

enum class AlgorithmTag
{
    eExchangeLinked,
};

} // namespace lcf::async::queue::intrusive::spmc
```

`include/async/core/queue/intrusive/spmc/queue_declarations.h`：

```cpp
#pragma once

#include "AlgorithmTag.h"

namespace lcf::async::queue::intrusive::spmc {

template <AlgorithmTag Algorithm = AlgorithmTag::eExchangeLinked>
class Queue;

} // namespace lcf::async::queue::intrusive::spmc
```

`include/async/core/queue/intrusive/spmc/Queue.h`：

```cpp
#pragma once

#include <async/core/queue/intrusive/spmc/exchange_linked/Queue.h>
```

两种后端都借用外部稳定节点，链接在节点内，拒收不转移所有权。intrusive 是结构分类，非拥有是生命周期契约，当前不创建 non_owning 并列目录。SPMC 的单 producer 限制对整个 Queue 生效；eExchangeLinked 仅标识发布算法，consumer 仍以 mutex 串行领取。

ExecutionNode 是 queue::intrusive::mpmc::Queue<>、queue::intrusive::spmc::Queue<> 和标准 schedule operation 采用的具体侵入式执行节点，保存 atomic next、入队状态、state 和回调。它不代表 executable_c 抽象，不是公共 operation 基类。自定义 Queue 通过 Executable 别名选择可执行对象，无需继承 ExecutionNode；链式存储才需要这些链接字段。最终非侵入式后端按需选择 record 表示，不携带无用 hook；所有权与侵入方式独立，non_intrusive 是存储类别，non_owning 仅描述生命周期。

queue::intrusive::mpmc::Queue<> 在接纳时调用 node.markQueued()，出队时调用 node.clearQueueState()；拒收不修改 node，出队后不再访问它。getNext/setNext 供链式存储实现使用，这些接口不属于通用 Queue/Node 协议。node 由 record 内嵌，不逐次分配，无 friend；Executor 直接调用 node.execute()，节点内部在 dispatch 前保存回调和状态指针。完成回调可能销毁 record，dispatch 后不再访问节点；状态和函数指针不通过 getter 暴露。

`include/async/core/queue/intrusive/spmc/exchange_linked/Queue.h`：

```cpp
#pragma once

#include <async/core/enums.h>
#include <async/core/queue/intrusive/ExecutionNode.h>
#include <async/core/queue/intrusive/spmc/queue_declarations.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>

namespace lcf::async::queue::intrusive::spmc {

template <>
class Queue<AlgorithmTag::eExchangeLinked>
{
public:
    using Executable = ExecutionNode;
    static constexpr bool k_supports_multiple_producers = false;
    static constexpr bool k_supports_multiple_consumers = true;

    static_assert(std::atomic<ExecutionNode *>::is_always_lock_free,
        "Queue<AlgorithmTag::eExchangeLinked> requires lock-free pointer atomics");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
        "Queue<AlgorithmTag::eExchangeLinked> requires lock-free 64-bit atomics");
    static_assert(std::atomic<unsigned>::is_always_lock_free,
        "Queue<AlgorithmTag::eExchangeLinked> requires lock-free unsigned atomics");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
        "Queue<AlgorithmTag::eExchangeLinked> requires lock-free size atomics");

public:
    ~Queue() noexcept = default;
    Queue() noexcept
        : m_stub{nullptr, &execute_stub}, m_head_p{&m_stub}, m_tail_p(&m_stub) {}

    Queue(const Queue &) = delete;
    Queue(Queue &&) = delete;
    Queue & operator=(const Queue &) = delete;
    Queue & operator=(Queue &&) = delete;
public:
    EnqueueResult tryEnqueue(ExecutionNode & node) noexcept
    {
        auto state = m_lifecycle.load(std::memory_order_acquire);
        if ((state & k_closed_bit) != 0) {
            return EnqueueResult::eClosed;
        }
        if ((state & k_producer_active_bit) != 0) {
            std::terminate();
        }
        if (not m_lifecycle.compare_exchange_strong(
                state, state | k_producer_active_bit,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            if ((state & k_closed_bit) != 0) {
                return EnqueueResult::eClosed;
            }
            std::terminate();
        }
#if defined(LCF_ASYNC_ENABLE_TEST_HOOKS)
        if (m_after_producer_entry_hook_f) {
            m_after_producer_entry_hook_f(m_after_producer_entry_hook_state_p);
        }
#endif
        node.markQueued();
        node.setNext(nullptr);
        m_queued_count.fetch_add(1, std::memory_order_relaxed);

        auto * previous_p = m_head_p.exchange(&node, std::memory_order_acq_rel);
        previous_p->setNext(&node);

        m_wake_sequence.fetch_add(1, std::memory_order_release);
        m_wake_sequence.notify_one();

        m_lifecycle.fetch_and(~k_producer_active_bit, std::memory_order_release);
        m_lifecycle.notify_all();
        return EnqueueResult::eAccepted;
    }

    ExecutionNode * tryPop() noexcept
    {
        std::lock_guard lock{m_consumer_mutex};
#if defined(LCF_ASYNC_ENABLE_TEST_HOOKS)
        if (m_after_consumer_lock_hook_f) {
            m_after_consumer_lock_hook_f(m_after_consumer_lock_hook_state_p);
        }
#endif
        return this->popFront();
    }

    ExecutionNode * waitPop() noexcept
    {
        for (;;) {
            if (m_stop_requested.load(std::memory_order_acquire)) {
                return nullptr;
            }
            if (auto * node_p = this->tryPop()) {
                return node_p;
            }
            if (this->isClosedAndQuiescent() and this->isEmpty()) {
                return nullptr;
            }

            const auto observed = m_wake_sequence.load(std::memory_order_acquire);
            if (m_stop_requested.load(std::memory_order_acquire)) {
                return nullptr;
            }
            if (auto * node_p = this->tryPop()) {
                return node_p;
            }
            if (this->isClosedAndQuiescent() and this->isEmpty()) {
                return nullptr;
            }
            m_wake_sequence.wait(observed, std::memory_order_acquire);
        }
    }

    void requestStop() noexcept
    {
        m_stop_requested.store(true, std::memory_order_release);
        this->notifyWaiters();
    }

    void close() noexcept
    {
        auto state = m_lifecycle.fetch_or(k_closed_bit, std::memory_order_acq_rel);
        while ((state & k_producer_active_bit) != 0) {
            m_lifecycle.wait(state, std::memory_order_acquire);
            state = m_lifecycle.load(std::memory_order_acquire);
        }
        this->notifyWaiters();
    }

    bool isEmpty() const noexcept
    {
        return m_queued_count.load(std::memory_order_relaxed) == 0;
    }

private:
#if defined(LCF_ASYNC_ENABLE_TEST_HOOKS)
    friend struct QueueTestAccess;
    using TestHook = void (*)(void *) noexcept;
    TestHook m_after_consumer_lock_hook_f = nullptr;
    void * m_after_consumer_lock_hook_state_p = nullptr;
    TestHook m_after_producer_entry_hook_f = nullptr;
    void * m_after_producer_entry_hook_state_p = nullptr;
#endif

    static constexpr unsigned k_closed_bit = 1u;
    static constexpr unsigned k_producer_active_bit = 2u;

    static void execute_stub(void *) noexcept {}

    ExecutionNode * popFront() noexcept
    {
        auto * tail_p = m_tail_p;
        auto * next_p = tail_p->getNext();

        if (tail_p == &m_stub) {
            if (not next_p) {
                return nullptr;
            }
            m_tail_p = next_p;
            tail_p = next_p;
            next_p = tail_p->getNext();
        }

        if (next_p) {
            m_tail_p = next_p;
            return this->releaseNode(*tail_p);
        }

        if (tail_p != m_head_p.load(std::memory_order_acquire)) {
            return nullptr;
        }

        m_stub.setNext(nullptr);
        auto * previous_p = m_head_p.exchange(&m_stub, std::memory_order_acq_rel);
        previous_p->setNext(&m_stub);

        next_p = tail_p->getNext();
        if (next_p) {
            m_tail_p = next_p;
            return this->releaseNode(*tail_p);
        }
        return nullptr;
    }

    ExecutionNode * releaseNode(ExecutionNode & node) noexcept
    {
        node.clearQueueState();
        m_queued_count.fetch_sub(1, std::memory_order_relaxed);
        return &node;
    }

    bool isClosedAndQuiescent() const noexcept
    {
        return m_lifecycle.load(std::memory_order_acquire) == k_closed_bit;
    }

    void notifyWaiters() noexcept
    {
        m_wake_sequence.fetch_add(1, std::memory_order_release);
        m_wake_sequence.notify_all();
    }

    mutable std::mutex m_consumer_mutex;
    ExecutionNode m_stub;
    std::atomic<ExecutionNode *> m_head_p;
    ExecutionNode * m_tail_p;
    std::atomic<std::size_t> m_queued_count{0};
    std::atomic<std::uint64_t> m_wake_sequence{0};
    std::atomic<unsigned> m_lifecycle{0};
    std::atomic<bool> m_stop_requested{false};
};

} // namespace lcf::async::queue::intrusive::spmc
```

`tryEnqueue` is restricted to one producer. It enters a short lifecycle state with one compare-exchange, marks the node, does `previous = head.exchange(&node)`, then release-stores the node into `previous.next`; it never acquires `m_consumer_mutex` and never allocates. A concurrent second producer is a contract violation and terminates. Multiple workers serialize `tryPop`/`waitPop` under `m_consumer_mutex`; a publication gap returns empty rather than spinning. `waitPop` uses a wake sequence to recheck the queue before waiting, preventing a lost wake when publication races with sleep. `close` marks the inbox closed and waits for the active producer state to clear, so accepted work is fully linked before close returns. Destruction still requires stopping the producer, draining accepted nodes, and finishing all consumer calls before destroying their operations. The producer has a fixed number of queue operations, but atomic notification and platform scheduling do not provide a portable wall-clock WCET guarantee.

Record 的节点类型与实际 Queue 是否兼容由 tryEnqueue 的 requires 表达式检查。自定义节点只需 execute 能力，但接入当前标准 schedule adapter 时，Queue 仍须能接纳该 adapter 内嵌的 ExecutionNode，或另行提供匹配的 operation。

`include/async/core/queue/intrusive/ExecutionNode.h`：

```cpp
#pragma once

#include <atomic>

namespace lcf::async::queue::intrusive {

class ExecutionNode
{
public:
    using ExecuteFunction = void (*)(void *) noexcept;
public:
    ~ExecutionNode() noexcept = default;
    ExecutionNode() = delete;
    ExecutionNode(void * state_p, ExecuteFunction execute_f) noexcept
        : m_state_p(state_p), m_execute_f(execute_f) {}
    ExecutionNode(const ExecutionNode &) = delete;
    ExecutionNode & operator=(const ExecutionNode &) = delete;
    ExecutionNode(ExecutionNode &&) = delete;
    ExecutionNode & operator=(ExecutionNode &&) = delete;
public:
    ExecutionNode * getNext() const noexcept { return m_next_p.load(std::memory_order_acquire); }
    void setNext(ExecutionNode * next_p) noexcept { m_next_p.store(next_p, std::memory_order_release); }
    bool isQueued() const noexcept { return m_queued; }
    void markQueued() noexcept { m_queued = true; }
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
    std::atomic<ExecutionNode *> m_next_p{nullptr};
    void * m_state_p;
    ExecuteFunction m_execute_f;
    bool m_queued = false;
};

} // namespace lcf::async::queue::intrusive
```

`include/async/core/queue/intrusive/mpmc/mutex/Queue.h`：

```cpp
#pragma once

#include <async/core/enums.h>
#include <async/core/queue/intrusive/ExecutionNode.h>
#include <async/core/queue/intrusive/mpmc/queue_declarations.h>

#include <cassert>
#include <condition_variable>
#include <mutex>

namespace lcf::async::queue::intrusive::mpmc {

template <>
class Queue<AlgorithmTag::eMutex>
{
public:
    using Executable = ExecutionNode;
    static constexpr bool k_supports_multiple_producers = true;
    static constexpr bool k_supports_multiple_consumers = true;
public:
    ~Queue() noexcept = default;
    Queue() = default;
    Queue(const Queue &) = delete;
    Queue(Queue &&) = delete;
    Queue & operator=(const Queue &) = delete;
    Queue & operator=(Queue &&) = delete;
public:
    EnqueueResult tryEnqueue(ExecutionNode & node) noexcept
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

    ExecutionNode * tryPop() noexcept
    {
        std::lock_guard lock{m_mutex};
        return this->popFront();
    }

    ExecutionNode * waitPop() noexcept
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

    bool isEmpty() const noexcept
    {
        std::lock_guard lock{m_mutex};
        return m_head_p == nullptr;
    }

private:
    // The caller holds m_mutex. Never dispatch user code while holding it.
    ExecutionNode * popFront() noexcept
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
    ExecutionNode * m_head_p = nullptr;
    ExecutionNode * m_tail_p = nullptr;
    bool m_closed = false;
    bool m_stop_requested = false;
};

} // namespace lcf::async::queue::intrusive::mpmc
```

所有接纳、取出、关闭、停止和等待 predicate 都用同一 mutex 排序，因而没有“检查空 → notify → 入睡”的丢唤醒窗口。queue::intrusive::mpmc::Queue<> 支持多 producer、多 consumer；回调在出队解锁后执行，FIFO 只约束取出顺序，不承诺多 Worker 的开始或完成顺序；tryPop 不等待新工作，但获取 mutex 可能短暂阻塞，它是正确性基线，不是 wait-free。close 后仍可取出已接纳节点；stop 请求优先终止 waitPop，但 tryPop 仍可排空。拒收不修改 node，接纳后恰好出队一次；同一 operation/node 只能 start/提交一次，并保持地址稳定至最后访问。

### 5.2 Executor 与 Worker

`include/async/core/scheduling/BasicExecutor.h`：

```cpp
#pragma once

#include <async/core/concepts.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>

#include <cassert>
#include <concepts>
#include <utility>

namespace lcf::async {

template <work_queue_c Queue = queue::intrusive::mpmc::Queue<>>
class BasicExecutor
{
public:
    static constexpr bool k_supports_multiple_producers = Queue::k_supports_multiple_producers;
    static constexpr bool k_supports_multiple_workers = Queue::k_supports_multiple_consumers;
public:
    ~BasicExecutor() noexcept { assert(m_queue.isEmpty()); }
    BasicExecutor() requires std::default_initializable<Queue> = default;
    template <typename... QueueArgs>
    requires (sizeof...(QueueArgs) > 0) and std::constructible_from<Queue, QueueArgs...>
    explicit BasicExecutor(QueueArgs &&... queue_args) : m_queue(std::forward<QueueArgs>(queue_args)...) {}
    BasicExecutor(const BasicExecutor &) = delete;
    BasicExecutor(BasicExecutor &&) = delete;
    BasicExecutor & operator=(const BasicExecutor &) = delete;
    BasicExecutor & operator=(BasicExecutor &&) = delete;
public:
    template <work_record_c Record>
    requires requires(Queue & queue, Record & record) {
        { queue.tryEnqueue(record.getExecutable()) } noexcept -> std::same_as<EnqueueResult>;
    }
    EnqueueResult tryEnqueue(Record & record) noexcept { return m_queue.tryEnqueue(record.getExecutable()); }
    bool executeOne() noexcept
    {
        if (auto * executable_p = m_queue.tryPop()) {
            executable_p->execute();
            return true;
        }
        return false;
    }
    bool waitAndExecuteOne() noexcept
    requires waiting_work_queue_c<Queue>
    {
        if (auto * executable_p = m_queue.waitPop()) {
            executable_p->execute();
            return true;
        }
        return false;
    }
    void requestStop() noexcept
    requires stoppable_work_queue_c<Queue>
    {
        m_queue.requestStop();
    }
    void close() noexcept { m_queue.close(); }
    bool isEmpty() const noexcept { return m_queue.isEmpty(); }
private:
    Queue m_queue;
};

template <work_executor_c Executor>
struct DefaultContextExecutorFactory
{
    ~DefaultContextExecutorFactory() noexcept = default;
    DefaultContextExecutorFactory() noexcept = default;
    DefaultContextExecutorFactory(const DefaultContextExecutorFactory &) noexcept = default;
    DefaultContextExecutorFactory(DefaultContextExecutorFactory &&) noexcept = default;
    DefaultContextExecutorFactory & operator=(const DefaultContextExecutorFactory &) noexcept = default;
    DefaultContextExecutorFactory & operator=(DefaultContextExecutorFactory &&) noexcept = default;
public:
    Executor makeExecutor() const requires std::default_initializable<Executor>
    {
        return Executor{};
    }
};

} // namespace lcf::async
```

BasicExecutor 拥有 Queue，负责 tryEnqueue、executeOne/waitAndExecuteOne、close 与 isEmpty；不创建线程。DefaultContextExecutorFactory::makeExecutor 创建一个 Executor。能力常量直接引用所选 Queue，multi_worker_executor_c 检查多 Worker 消费能力。

`include/async/core/scheduling/BasicWorker.h`：

```cpp
#pragma once

#include <async/core/thread_types.h>
#include <async/core/concepts.h>

#include <cassert>
#include <concepts>
#include <thread>

namespace lcf::async {

template <join_handle_c ThreadHandle>
struct ThreadLauncher
{
    ~ThreadLauncher() noexcept = default;
    ThreadLauncher() noexcept = default;
    ThreadLauncher(const ThreadLauncher &) noexcept = default;
    ThreadLauncher(ThreadLauncher &&) noexcept = default;
    ThreadLauncher & operator=(const ThreadLauncher &) noexcept = default;
    ThreadLauncher & operator=(ThreadLauncher &&) noexcept = default;
public:
    ThreadHandle operator()(ThreadEntry entry_f, void * state_p) const
        requires std::constructible_from<ThreadHandle, ThreadEntry, void *>
    {
        return ThreadHandle{entry_f, state_p};
    }
};

template <waiting_work_executor_c Executor, join_handle_c ThreadHandle = std::thread>
class BasicWorker
{
public:
    ~BasicWorker() noexcept
    {
        assert(not m_thread_handle.joinable() and "BasicWorker must be joined before destruction");
    }
    BasicWorker() = delete;
    template <thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
    explicit BasicWorker(Executor & executor, Launcher && launcher = {}) :
        m_executor_p(&executor), m_thread_handle(launcher(&run_executor, this)) {}
    BasicWorker(const BasicWorker &) = delete;
    BasicWorker & operator=(const BasicWorker &) = delete;
    BasicWorker(BasicWorker &&) = delete;
    BasicWorker & operator=(BasicWorker &&) = delete;
public:
    void join() noexcept
    {
        // Calling from this worker violates the backend's join precondition.
        if (m_thread_handle.joinable()) {
            m_thread_handle.join();
        }
    }
private:
    static void run_executor(void * state_p) noexcept
    {
        auto & worker = *static_cast<BasicWorker *>(state_p);
        while (worker.m_executor_p->waitAndExecuteOne()) {}
    }
private:
    Executor * m_executor_p;
    ThreadHandle m_thread_handle;
};

} // namespace lcf::async
```

每个 BasicWorker 拥有线程 join 句柄，借用同一个 Executor；宿主 poll/run 可共同消费。回调在 Queue 解锁后执行。requestStop 停止全部等待驱动者，close 在未停止时允许排空后退出；caller 返回 Executor 始终由调用方独立驱动。

Worker 借用的 Executor 必须保持到 join 结束。Worker 析构要求线程已 join；宿主负责显式 close/join。

### 5.3 逻辑身份、配置与 Context

PollBudget 和 PollReport 服务于 Context::poll 的批量驱动包装，不是 work_executor_c 的基础协议。它们放在 context/poll_structs.h，由 BasicContext.h 直接包含；归属 Context 的驱动接口，Queue、Executor、Scheduler 和 concepts 不依赖它们。

`include/async/core/context/poll_structs.h`：

```cpp
#pragma once

#include <chrono>
#include <cstddef>
#include <limits>

namespace lcf::async {

struct PollBudget
{
    std::size_t m_max_records = std::numeric_limits<std::size_t>::max();
    std::chrono::steady_clock::duration m_max_duration = std::chrono::steady_clock::duration::max();
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
public:
    FixedCapacityVector() = delete;
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


SchedulerToken 与 SchedulerHandle 统一定义在 `core/scheduling/SchedulerHandle.h`，完整定义见第 5.4 节。token 仅携带两个 std::uint32_t：域 ID 与目标下标，共 8 字节。ContextInfo 当前只计数，不引入队列类别、空配置对象或运行时路由数组。

`include/async/core/context/ContextInfo.h`：

```cpp
#pragma once

#include <async/core/scheduling/SchedulerHandle.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory_resource>
#include <utility>

namespace lcf::async {

using WorkerCount = std::uint32_t;

struct StorageRequirement
{
    std::size_t m_bytes;
    std::size_t m_alignment;
};

struct ContextStorageRequirements
{
    StorageRequirement m_workers;
};

struct ContextAllocators
{
    using Allocator = std::pmr::polymorphic_allocator<std::byte>;
public:
    ~ContextAllocators() noexcept = default;
    ContextAllocators() = delete;
    explicit ContextAllocators(Allocator workers) noexcept :
        m_workers(workers) {}
    ContextAllocators(const ContextAllocators &) noexcept = default;
    ContextAllocators(ContextAllocators &&) noexcept = default;
    ContextAllocators & operator=(const ContextAllocators &) = delete;
    ContextAllocators & operator=(ContextAllocators &&) = delete;
public:
    Allocator m_workers;
};

class ContextInfo
{
public:
    ~ContextInfo() noexcept = default;
    ContextInfo() = delete;
    explicit ContextInfo(WorkerCount worker_count) noexcept
        : m_domain_id(acquire_domain_id()), m_worker_count(worker_count) {}

    ContextInfo(const ContextInfo &) = delete;
    ContextInfo & operator=(const ContextInfo &) = delete;
    ContextInfo(ContextInfo && other) noexcept
        : m_domain_id(std::exchange(other.m_domain_id, 0)),
          m_worker_count(std::exchange(other.m_worker_count, 0)),
          m_scheduler_count(std::exchange(other.m_scheduler_count, 0)) {}
    ContextInfo & operator=(ContextInfo &&) = delete;

public:
    SchedulerToken addScheduler() noexcept
    {
        if (m_domain_id == 0 || m_scheduler_count == std::numeric_limits<std::uint32_t>::max()) {
            std::terminate();
        }
        const auto index = static_cast<std::uint32_t>(m_scheduler_count++);
        return SchedulerToken{m_domain_id, index};
    }

    WorkerCount getWorkerCount() const noexcept { return m_worker_count; }
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::size_t getSchedulerCount() const noexcept { return m_scheduler_count; }

private:
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
    std::size_t m_scheduler_count = 0;
};

} // namespace lcf::async
```

SchedulerToken 不能默认或数值构造；ContextInfo 的 addScheduler() noexcept 直接返回 token，当前仅递增计数，不分配。零 Worker 也可登记，由 poll/run 推进；已移动配置上登记终止。域号耗尽时终止、不复用。配置操作单线程执行，WorkerCount 构造后不变；QueueConfig 与普通配置容器后续按需加入。

ContextInfo 取得不复用的域 ID，移动转移域和数量并使源对象失效，Context 按值消费配置。token 用两个 std::uint32_t 保存 domain/index，共 8 字节，不延长运行时寿命。getSchedulerHandle 以 assert 检查域，直接把 Executor 引用和 token 身份写入句柄；同域下标来自该配置签发，无需额外查表或重复边界检查。配置计数与存储计算仍用 std::size_t，签发前检查 UINT32_MAX 上限，避免下标截断。域号从 1 起签发，0 留给移走配置；耗尽时终止、不回绕复用；不承诺跨独立动态模块的唯一性。

独立调用方 executor 直接用 SchedulerHandle{executor} 建立提交句柄，不需要 Context token，也不写入 ContextInfo。其身份使用域 ID 0、下标 0；同一独立 Executor 的句柄相等，不同 Executor 的句柄不相等。Context 返回的句柄保存 token 身份，同一 token 产生相等句柄，不同 token 即使共享 Executor 也不相等。复制不分配，句柄不保活 Executor。

`include/async/core/context/BasicContext.h`：

```cpp
#pragma once

#include "ContextInfo.h"
#include "poll_structs.h"
#include <async/core/details/containers/FixedCapacityVector.h>
#include <async/core/scheduling/BasicExecutor.h>
#include <async/core/scheduling/BasicWorker.h>
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
        if (not executor.executeOne()) { break; }
        ++report.m_records;
    }
    report.m_elapsed = Clock::now() - started;
    report.m_count_limit_reached = report.m_records == budget.m_max_records;
    report.m_time_limit_reached = finite_time && report.m_elapsed >= budget.m_max_duration;
    return report;
}

} // namespace details

template <multi_worker_executor_c Executor = BasicExecutor<>, join_handle_c ThreadHandle = std::thread>
class BasicContext
{
    using Worker = BasicWorker<Executor, ThreadHandle>;
    using WorkerList = details::FixedCapacityVector<Worker>;
public:
    ~BasicContext() noexcept { assert(this->isEmpty()); }
    BasicContext() = delete;
    template <typename ExecutorFactory = DefaultContextExecutorFactory<Executor>,
        thread_launcher_c<ThreadHandle> Launcher = ThreadLauncher<ThreadHandle>>
        requires context_executor_factory_c<ExecutorFactory, Executor>
    explicit BasicContext(ContextInfo context_info, ContextAllocators allocators,
        ExecutorFactory executor_factory = {}, Launcher thread_launcher = {})
        : m_domain_id(validate_info(context_info)),
          m_executor(executor_factory.makeExecutor()),
          m_workers(context_info.getWorkerCount(),
              std::pmr::polymorphic_allocator<Worker>{allocators.m_workers})
    {
        try {
            for (WorkerCount index = 0; index < context_info.getWorkerCount(); ++index) {
                m_workers.emplaceBack(m_executor, thread_launcher);
            }
        } catch (...) {
            this->close();
            this->joinWorkers();
            throw;
        }
    }

    BasicContext(const BasicContext &) = delete;
    BasicContext(BasicContext &&) = delete;
    BasicContext & operator=(const BasicContext &) = delete;
    BasicContext & operator=(BasicContext &&) = delete;
public:
    static constexpr ContextStorageRequirements storageRequirements(
        WorkerCount worker_count) noexcept
    {
        return {storage_requirement<Worker>(worker_count)};
    }

    static ContextStorageRequirements storageRequirements(const ContextInfo & info) noexcept
    {
        return storageRequirements(info.getWorkerCount());
    }
    static std::uint32_t validate_info(const ContextInfo & info)
    {
        if (info.getDomainId() == 0 || info.getSchedulerCount() == 0) {
            throw std::invalid_argument{"Context requires an unconsumed configuration with schedulers"};
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
public:
    SchedulerHandle<Executor> getSchedulerHandle(SchedulerToken token) const noexcept
    {
        assert(token.getDomainId() == m_domain_id);
        return SchedulerHandle<Executor>{m_executor, token};
    }

    bool pollOne() noexcept { return m_executor.executeOne(); }
    PollReport poll(PollBudget budget = {}) noexcept
    {
        return details::poll_executor(m_executor, budget);
    }
    void run() noexcept requires stoppable_work_executor_c<Executor>
    {
        while (m_executor.waitAndExecuteOne()) {}
    }
    void requestStop() noexcept requires stoppable_work_executor_c<Executor>
    {
        m_executor.requestStop();
    }
    void close() noexcept
    {
        m_executor.close();
    }
    void joinWorkers() noexcept
    {
        for (auto & worker : m_workers.values()) { worker.join(); }
    }
    bool isEmpty() const noexcept
    {
        return m_executor.isEmpty();
    }

private:
    std::uint32_t m_domain_id;
    mutable Executor m_executor;
    WorkerList m_workers;
};

using Context = BasicContext<>;

} // namespace lcf::async
```

Context 内嵌一个 Executor，默认使用 queue::intrusive::mpmc::Queue<>；ExampleContext 显式换成 queue::intrusive::spmc::Queue<>。只有 Worker 固定数组使用 ContextAllocators，storageRequirements 只报告该数组的字节数/对齐；SchedulerHandle 直接引用共享 Executor，不构造逐目标路由对象。

构造顺序为 Executor、Worker 数组存储块，最后启动 N 个 Worker。固定数组不移动元素，Context 不可移动。工厂独立注入 Queue 参数，不自动传播 Worker allocator；ThreadLauncher 选择线程后端。

数组分配或 Executor 构造失败时按已构造范围回滚；线程启动失败关闭共享 Executor、join 已启动 Worker，再释放数组。close 一次即可唤醒所有等待者，构造阶段没有业务投递。

pollOne/poll/run/requestStop 与全部 SchedulerHandle 均使用同一个 Executor。close/isEmpty 覆盖该执行域；token 查询检查域后直接构造句柄，不访问路由表或 Worker 下标。

run 复用共享 Executor 的 waitAndExecuteOne，不另造等待状态。requestStop 同时结束 run 与后台 Worker 的等待驱动；剩余节点可 poll/drain。未停止时 close 关闭准入并唤醒消费者，排空后退出。isEmpty 不证明没有执行中或未来投递的 root。

### 5.4 用户直接使用的 SchedulerHandle

用户句柄为 SchedulerHandle<Executor>；它保存非拥有 Executor 指针、域 ID 和目标下标，提供 scheduler_concept/schedule/相等比较，满足标准 execution scheduler 协议，同时保留 Core record 提交能力。SchedulerToken 与句柄统一定义在 SchedulerHandle.h，不再需要独立逻辑调度目标代理类型。

用户调用 context.getSchedulerHandle(token) 取得 SchedulerHandle<Executor>；独立返回域用 SchedulerHandle{caller_executor}。句柄可自由复制，获取、复制和投递不分配；value completion scheduler 查询返回包含相同身份的句柄。宿主须保持被引用的 Executor 存活至最后一个操作访问结束。

BasicContext<Executor, ThreadHandle> 返回 SchedulerHandle<Executor>，全部 addScheduler 目标统一共享该 Executor。默认 Context 保持多 producer queue::intrusive::mpmc::Queue<>；SPMC 配置须显式选择并遵守整个 Queue 的单 producer 限制。SchedulerHandle 实现标准 scheduler，基础 Queue/Executor/Worker 直接消费成员能力。

getSchedulerHandle(token) const noexcept 的前置条件是 token 已由构造此 Context 的配置签发；错误归属属于调用方契约违例，在 debug 构建中 assert 域 ID 后直接构造句柄。域相同即保证下标来自该配置登记，不制造正常路径的“可能没有句柄”。token 与句柄都不延长 Context/Executor 寿命。

SchedulerHandle 位于 core/scheduling，Context 常用别名随 BasicContext 定义；标准 sender/operation 适配位于 core/scheduling/details，用户只使用公开句柄和标准 CPO。Core 按需包含协议、operations 和 queries 头，不包含 execution.h，不调用 then/when_all 等组合算法，也不使用 stdexec 私有类型。protocols 和 execution 的基础接口不反向依赖 Core，因此没有循环。

core.h 聚合 Context、SchedulerHandle 和标准协议，因而需要选定的 execution provider。当前 Queue、Executor、Worker 经 core/concepts.h 传递加载 provider；ExecutionNode、固定存储与仅含配置宏的头可独立包含。组件不调用 execution 组合算法，与是否传递加载 provider 是两个不同维度。

`include/async/core/scheduling/details/exec_schedule_adapters.h`：

```cpp
#pragma once

#include <async/core/queue/intrusive/ExecutionNode.h>
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
public:
    ~ScheduleOperation() noexcept = default;
    ScheduleOperation() = delete;
    ScheduleOperation(Handle handle, Receiver receiver)
        noexcept(std::is_nothrow_copy_constructible_v<Handle>
            && std::is_nothrow_move_constructible_v<Receiver>)
        : m_handle(handle), m_receiver(std::move(receiver)), m_node(this, &execute) {}
    ScheduleOperation(const ScheduleOperation &) = delete;
    ScheduleOperation & operator=(const ScheduleOperation &) = delete;
    ScheduleOperation(ScheduleOperation &&) = delete;
    ScheduleOperation & operator=(ScheduleOperation &&) = delete;
public:
    queue::intrusive::ExecutionNode & getExecutable() noexcept { return m_node; }
    void start() & noexcept
        requires record_submitter_c<Handle, ScheduleOperation>
    {
        if (m_handle.tryEnqueue(*this) == EnqueueResult::eAccepted) {
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
private:
    Handle m_handle;
    Receiver m_receiver;
    queue::intrusive::ExecutionNode m_node;
};

template <std::copyable Handle>
struct ScheduleSender
{
    using sender_concept = protocols::sender_t;
    using completion_signatures = ScheduleCompletions;

    struct Environment
    {
        ~Environment() noexcept = default;
        Environment() = delete;
        explicit Environment(Handle scheduler) noexcept(std::is_nothrow_move_constructible_v<Handle>)
            : m_scheduler(std::move(scheduler)) {}
        Environment(const Environment &) = default;
        Environment(Environment &&) = default;
        Environment & operator=(const Environment &) = default;
        Environment & operator=(Environment &&) = default;
    public:
        Handle query(exec::get_completion_scheduler_t<exec::set_value_t>) const noexcept
        {
            return m_scheduler;
        }
    private:
        Handle m_scheduler;
    };
public:
    ~ScheduleSender() noexcept = default;
    ScheduleSender() = delete;
    explicit ScheduleSender(Handle scheduler) noexcept(std::is_nothrow_move_constructible_v<Handle>)
        : m_scheduler(std::move(scheduler)) {}
    ScheduleSender(const ScheduleSender &) = default;
    ScheduleSender(ScheduleSender &&) = default;
    ScheduleSender & operator=(const ScheduleSender &) = default;
    ScheduleSender & operator=(ScheduleSender &&) = default;
public:
    Environment get_env() const noexcept { return Environment{m_scheduler}; }
    template <receiver_of_c<ScheduleCompletions> Receiver>
    requires record_submitter_c<Handle, ScheduleOperation<Handle, Receiver>>
    auto connect(Receiver receiver) const
    {
        return ScheduleOperation<Handle, Receiver>{m_scheduler, std::move(receiver)};
    }

private:
    Handle m_scheduler;
};

} // namespace lcf::async::details
```

`include/async/core/scheduling/SchedulerHandle.h`：

```cpp
#pragma once

#include <async/core/scheduling/details/exec_schedule_adapters.h>
#include <async/core/concepts.h>

#include <cstdint>

namespace lcf::async {

class ContextInfo;

class SchedulerToken
{
    friend class ContextInfo;
public:
    ~SchedulerToken() noexcept = default;
    SchedulerToken() = delete;
    SchedulerToken(const SchedulerToken &) noexcept = default;
    SchedulerToken(SchedulerToken &&) noexcept = default;
    SchedulerToken & operator=(const SchedulerToken &) noexcept = default;
    SchedulerToken & operator=(SchedulerToken &&) noexcept = default;
    bool operator==(const SchedulerToken &) const noexcept = default;
private:
    SchedulerToken(std::uint32_t domain_id, std::uint32_t index) noexcept :
        m_domain_id(domain_id), m_index(index) {}
public:
    std::uint32_t getDomainId() const noexcept { return m_domain_id; }
    std::uint32_t getIndex() const noexcept { return m_index; }
private:
    std::uint32_t m_domain_id;
    std::uint32_t m_index;
};

template <typename Executor>
class SchedulerHandle
{
public:
    using scheduler_concept = protocols::scheduler_t;
public:
    ~SchedulerHandle() noexcept = default;
    SchedulerHandle() = delete;
    explicit SchedulerHandle(Executor & executor) noexcept :
        m_executor_p(&executor), m_domain_id(0), m_scheduler_index(0) {}
    SchedulerHandle(Executor & executor, SchedulerToken token) noexcept :
        m_executor_p(&executor), m_domain_id(token.getDomainId()),
        m_scheduler_index(token.getIndex()) {}
    SchedulerHandle(const SchedulerHandle &) noexcept = default;
    SchedulerHandle(SchedulerHandle &&) noexcept = default;
    SchedulerHandle & operator=(const SchedulerHandle &) noexcept = default;
    SchedulerHandle & operator=(SchedulerHandle &&) noexcept = default;
    bool operator==(const SchedulerHandle &) const noexcept = default;
public:
    auto schedule() const noexcept;
    template <typename Record>
    requires record_submitter_c<Executor, Record>
    EnqueueResult tryEnqueue(Record & record) const noexcept { return m_executor_p->tryEnqueue(record); }
private:
    Executor * m_executor_p;
    std::uint32_t m_domain_id;
    std::uint32_t m_scheduler_index;
};

template <typename Executor>
auto SchedulerHandle<Executor>::schedule() const noexcept
{
    return details::ScheduleSender<SchedulerHandle>{*this};
}

} // namespace lcf::async
```

connect 构造不可移动的稳定 operation，receiver_of_c 检查确切的 value()/stopped() 签名；实际 Record 完整后再检查 record_submitter_c。start 成功发布后不能再访问可能已销毁的 operation，拒收则在 start 线程同步 stopped。执行时读取 receiver stop token，已接纳操作在 owner 驱动时交付 value 或 stopped。MVP 不注册主动取消 callback，请求 stop 后仍需 owner 推进。

sender 的 environment 只对 set_value 声明 completion scheduler；拒收的 stopped 可以在 start 线程发生，不能伪称无条件属于目标 executor。上层 continues_on 在目标接纳且 completion 存储不抛异常时，把上游三种完成一起送回目标；这与 schedule sender 自身的声明不是同一层保证。

### 5.5 聚合入口和 modules

`include/async/core.h`：

```cpp
#pragma once

#include <async/core/concepts.h>
#include <async/core/enums.h>
#include <async/core/context/ContextInfo.h>
#include <async/core/context/BasicContext.h>
#include <async/core/context/poll_structs.h>
#include <async/core/thread_types.h>
#include <async/core/queue/intrusive/ExecutionNode.h>
#include <async/core/scheduling/BasicExecutor.h>
#include <async/core/scheduling/BasicWorker.h>
#include <async/core/scheduling/SchedulerHandle.h>
#include <async/core/queue/intrusive/mpmc/Queue.h>
#include <async/core/queue/intrusive/spmc/Queue.h>
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

本轮用 FixedCapacityVector 保存 Worker 固定数组，非空时恰好一次 allocator.allocate(count)，无几何增长或分块表。Executor 内嵌，Worker 不可移动；factory placement new 只构造已有存储。ContextInfo 只保存域 ID 和计数，句柄携带身份，无逐目标动态分配。

`storageRequirement(s)` 报告 typed storage 的请求字节数/对齐，不是任意 resource 的上游保留量。第 1 节给每次固定申请提供正确对齐的缓冲区和 null upstream，容量不足立即抛 bad_alloc，不偷偷转到系统堆；真实数量来自 info 时用 `Context::storageRequirements(info)` 规划宿主 arena。若合并多个数组到一个 arena，还必须计算各段的对齐填充。pool 的 chunk、簿记和缓存空间另外计量，不能拿 count * sizeof(T) 宣称覆盖其全部成本。

### 6.2 MVP 的逐项申请审查

| 位置/所有者 | 申请与容量 | allocator/后端、释放与线程 |
| --- | --- | --- |
| ContextInfo | Worker/scheduler 数量与域 ID | 当前不动态分配；QueueConfig 出现后再引入普通配置容器 |
| token 域 ID | 一个静态 atomic；签发只使用已分配的配置位置 | 无动态申请；不以堆对象地址作为可复用的身份 |
| Context Executor/Queue | 一个内嵌 Executor，N 个 Worker 与宿主共享，不随 Worker 数复制 Queue | 工厂注入 Queue 配置；最后访问及 join 后释放 |
| Workers 对象数组 | worker_count 个 Worker，一个固定块；只存驱动者对象 | ContextAllocators::m_workers 重绑定到 Worker；不代表线程栈/启动状态也由它分配；销毁前须 join |
| SchedulerToken / SchedulerHandle 身份 | token 为 domain/index，handle 额外保存 Executor 指针；无独立运行时数组 | 获取及复制不分配；最后一个 handle/operation 访问结束后才能销毁 Executor |
| 默认 queue::intrusive::mpmc::Queue<> | head/tail、锁、CV 和状态内嵌，不为每次投递分配 node | 不接收无用 allocator；节点由 operation 提供；mutex/CV 的系统实现边界单列 |
| 自定义 Queue backing storage | 由具体结构与容量决定，不能计入 Executor 对象大小 | 工厂注入 Queue 的 allocator、容量和回收策略；构造失败回滚 |
| SchedulerHandle、内部 ScheduleSender/ScheduleOperation、Receiver、ExecutionNode | 本例栈/operation 内嵌存储；sender 与 operation 的组合类型决定大小 | adapter 不额外 new hook、record 或 owning callback；connect 的稳定 operation 活到最终完成返回，完成允许立即销毁自身 |
| when_all/continues_on 的完成存储 | 当前锁定 stdexec 的本例路径为 operation 内嵌 tuple/variant、子 operation 与 inplace stop 状态 | 这条路径的组合状态不另申请堆；值类型与异常运行库的申请另计，provider/算法或定制 domain 变更需重新审查，不承诺任意第三方算法均如此 |
| Worker 线程启动状态、栈和系统句柄 | 标准库/OS 管理；std::thread 没有标准 allocator 注入接口 | ThreadLauncher<std::thread> 使用标准库线程，PMR 不覆盖；严格控制时注入自定义 ThreadHandle/ThreadLauncher，声明栈来源、启动存储、失败回收和 join；也可由宿主拥有线程、直接驱动 executor |
| mutex/condition_variable 的系统状态 | 平台实现可能使用额外系统资源 | 不属于 async 元数据 allocator；若要求连该部分也可控，则替换 Queue/等待后端并审查平台实现，不添加被忽略的 PMR 参数 |
| 示例调用方 std::thread、iostream、异常及用户值 | 应用/运行库自己负责 | 调用方线程创建失败先关闭/join Context；示例不以这些成本验证 async 元数据零堆申请；严格验收以无日志、受控线程和不分配的值类型隔离测量 |
| factory/allocator 的自定义状态 | 可以借用资源，也可以自己拥有动态存储 | 工厂复制、调用和销毁的行为须声明；传入 PMR allocator 不会自动改变自定义 policy 的内部分配 |

这份表覆盖本轮所有 typed allocate、placement construction、线程创建及可能申请系统资源的位置。timer、coroutine frame、owning post 不在 MVP；后续加入时必须增加各自的容量、失败和回收条目，不能沿用一句“使用 Context resource”。

当前项目锁定 stdexec `dfc8c95718e8c66ff5496bc3b185a9162c93821e`。已检查本例实际使用的实现：`__opstate` 直接保存 state/child operations，when_all 把计数、tuple/variant 和 inplace stop 状态存为成员，continues_on 直接保存 completion storage 和目标 schedule operation，variant 使用内嵌对齐字节数组。本例的 BranchResult/GraphResult 不拥有动态数据，因此这些组合状态不产生额外堆申请；throw/runtime_error/exception_ptr 的异常存储不因此被 PMR 接管。可按版本复核 [basic sender](https://github.com/NVIDIA/stdexec/blob/dfc8c95718e8c66ff5496bc3b185a9162c93821e/include/stdexec/__detail/__basic_sender.hpp)、[when_all](https://github.com/NVIDIA/stdexec/blob/dfc8c95718e8c66ff5496bc3b185a9162c93821e/include/stdexec/__detail/__when_all.hpp)、[continues_on](https://github.com/NVIDIA/stdexec/blob/dfc8c95718e8c66ff5496bc3b185a9162c93821e/include/stdexec/__detail/__continues_on.hpp) 和 [variant](https://github.com/NVIDIA/stdexec/blob/dfc8c95718e8c66ff5496bc3b185a9162c93821e/include/stdexec/__detail/__variant.hpp)。

### 6.3 正确使用与后续扩展

第 1 节的 ContextInfo 仅保存域 ID 和计数；Worker 数组绑定有界 arena，Executor 内嵌，Queue backing storage 由工厂管理，逻辑目标不单独申请存储。同一资源可由宿主显式复用，但不混淆元数据、Queue 与线程栈的所有权及失败策略。

Worker 数组的分配/释放在宿主构造/销毁路径发生，资源不因 Worker 存在就必须同步。Queue 若跨线程分配/释放，应使用线程安全后端或将回收归并到资源 owner。resource/初始缓冲区必须活到最后 deallocate；arena.release 不执行对象析构，runtime arena 活到 Context 析构之后。

当前 addScheduler 只递增计数，无配置期分配失败。运行时数组需验证资源不足、乘法溢出、对齐、零数量、部分构造失败与线程创建失败。统计区分 PMR 请求与上游量，用会失败的上游证明无 fallback。需要可恢复分配失败时，由实际不抛异常的后端提供错误结果，不能仅包装标准 allocator 为 noexcept。

receiver allocator 覆盖适用于实际拥有额外动态状态的 operation，而不是反向改写已建好的 Context 存储；需保存原释放能力，不假定所有 allocator 都有 resource()。错误码分配能力 `allocation_resource_c`、typed storage 与 FramePolicy 按 [CORE_DESIGN.md](../CORE_DESIGN.md) 第 4/8 节扩展。队列结构、timer index、分配缓存、等待策略和默认算法选择由 benchmark 驱动。

## 7. 实现顺序

每步保持可构建，先用实际 provider 验证跨层签名。验收明确 caller 返回域；Context 内部任务由任意可用执行线程完成，不以主线程或 Worker 身份分类。

| 增量 | 实现与验证 | 完成条件 |
| --- | --- | --- |
| M1 协议与单步执行 | executable/record/submitter/queue/executor concepts、枚举、标准概念转发；ExecutionNode、两种 intrusive Queue、BasicExecutor | 能力常量缺失/类型错/非编译期时拒绝；无节点布局的 executable 可用；完成中销毁正确；SPMC producer 不等待 consumer mutex |
| M2 等待与停止 | waiting/stoppable queue；waitAndExecuteOne、持久停止、close/wake、BasicWorker | 不空转或丢唤醒；停止前于等待、执行中停止、关闭排空与停止后 poll 正确 |
| M3 运行时组合 | ContextInfo::addScheduler 计数与 token；一个共享 Executor；固定 Worker 数组；句柄直接保存逻辑身份 | 零 Worker 支持任意逻辑目标；身份独立且不绑定线程；宿主与 Worker 消费同池；构造回滚与显式 close/join 正确 |
| M4 execution 与返回域 | provider 词汇、SchedulerHandle、内部 sender/operation 与独立 caller Executor | 先由零 Worker Context 的 pollOne/poll 完成 schedule→then→receiver；再验证共享执行、when_all 得到 42、最终交付 caller；停止唤醒宿主和 Worker |
| M5 生命周期和基线 | value/error/stopped、取消/拒收、关闭竞争、PMR/线程创建失败、完成中销毁；接入 verify/benchmark/modules | 第 8 节必测项通过；建立空 pollOne、no-op dispatch、run/wake、两 Worker 合流、调用方返回和内存基线；receiver 执行时间单列 |

上述步骤无需 C++ coroutine。Task、co_await、timer、expected 扩展在运行时骨架稳定后按设计文档继续，不作为本轮验收的隐藏前置依赖。

## 8. 验收矩阵

测试按可观察契约划分，接入现有 verify target；完整示例可放 sandbox 直接运行，并把 main 主体提取为 verify helper。CTest 给整个测试设超时，出现丢完成/死锁时进程失败；不用超时后释放活跃 operation 的方式恢复。并发竞态用 latch/gate 或测试 queue 控制，不用 sleep 猜测时序。

| 必测项 | 观察结果 |
| --- | --- |
| poll 驱动 execution | 零 Worker、多个 addScheduler；start 后尚未完成，pollOne/poll 按预算推进所有逻辑目标，value 恰好一次 |
| SPMC CPU 提交 | 显式选择 queue::intrusive::spmc::Queue<>，默认 Context 保持多 producer 能力；明确单 producer 与多个 consumers 的归属；consumer 持锁暂停时 producer 仍能 enqueue；close 与进入提交区但尚未发布的 producer 竞争时 Worker 不提前退出，接纳节点 exactly-once 领取；不得将 SPMC handle 当作多 producer 通用入口 |
| 完整成功链 | 两个逻辑目标共享 Executor，分支合流得到 42；最终 Receiver 在 caller；随后 run 返回并 join |
| error 返回 | 在最后 continues_on 前注入 just_error(std::error_code) 或抛异常的 then；跳过后续 value 函数；set_error 恰好一次、在调用方线程、唤醒主线程退出 |
| stopped 返回 | 在最后 continues_on 前注入 just_stopped；set_stopped 恰好一次且在调用方线程；不依赖成功分支才能退出 |
| 共享消费与返回域隔离 | Context poll/run 与 Worker 都可领取任意逻辑调度目标；仅 caller Executor 执行最终 Receiver |
| 真正并行 | 两个受控 gate 证明共享 CPU Executor 的两个 Worker 可同时领取并执行分支；不使用耗时阈值或 Worker 身份 作为业务 API 证明并行 |
| 等待与竞态 | 空队列等待、新投递唤醒、虚假唤醒重查 predicate、检查空与入睡竞争、requestStop 前于 run/等待中/有工作时均无丢唤醒 |
| 停止和关闭独立 | requestStop 停止 run 和 Workers；仍可接纳与 poll；close 拒收且未停止时排空；两种状态不混淆 |
| 配置与逻辑身份 | addScheduler noexcept 直接返回 8 字节 token；两个 uint32_t 表达 domain/index，签发无截断；0/1/N Worker 与数千逻辑目标正确；跨域 assert；共享 Executor 的不同逻辑身份仍不相等 |
| 固定存储与资源 | ContextInfo 登记不分配；每个非空数组一次精确申请，零数量不申请；start/poll 不增长元数据；null upstream 证明无 fallback；按原 allocator 回收 |
| 构造失败与扩展 | 逐点注入数组分配、executor 构造、thread factory 失败；部分元素逆序销毁、已启动线程全部 close/join；自定义 queue 消费自己的 allocator；线程后端扩展实际被使用 |
| 存活与竞争 | eAccepted 后立即消费、完成中销毁、producer/close 竞争恰好一次；最后访问结束前不释放 executor/operation；跨线程回收遵守后端契约 |
| 失败边界 | 提前关闭返回 scheduler 可在来源线程 stopped；单独断言完成/存活，不在来源线程修改 owner 专有状态；completion 存储抛异常按 provider 契约测试 |
| 分层和配置 | 基础 queue/Executor/Worker 不调用 execution 算法；Queue 声明多 consumer 能力，BasicContext 拒绝不兼容 Executor；SchedulerHandle 满足标准 scheduler 接口，header/module 入口一致 |

普通 source 抛出的异常与最后 continues_on 存储 completion 时抛出的异常要区分：前者在本例的正常存储路径下转回调用方，后者可能在迁移前交付失败。基础 schedule 的 receiver stop token 检查也不等于取消所有已启动业务；sleep_for 的重工作不受 requestStop 抢占。

性能基线分别测量空 `pollOne`、空 poll、no-op dispatch、SPMC producer enqueue、consumer mutex 等待、run 入睡/唤醒、停止、批量软预算、两 Worker 合流、合流 continuation 和最终返回延迟；receiver 用户代码耗时单独统计。比较单 producer 入队延迟和多 consumer 领取/吞吐，不把 receiver 执行时间计入 executor 开销。基准不代替平台级 WCET 证明。benchmark 不保留演示用 1 秒 sleep、100ms 心跳或日志；其他 queue/timer/wake 优化先存基线，再以相同 workload 比较，不能从整轮均值推导 p99。

## 9. 接入与验证命令

正式实现头文件放入 include/async 后，由 cmake/project/config.cmake 自动收集；verify/benchmark 源文件加入各自 target，沿用延迟依赖和 provider 检测，不为文档代码另造一套正式构建。首次接入运行现有 preset：

```text
cmake --preset clang
cmake --build build/clang --config Debug --target async_verify_run
cmake --build build/clang --config Release --target async_benchmark_run
```

完成 module 导出后再运行对应 modules preset；平台支持的 MSVC/GCC 组合也需要验证。当前公共 Core 仍是骨架，现有 execution verify 通过不代表上述 MVP 已实现。参考源码保存在 `.mvp/include/async/`，主示例为 `.mvp/example.cpp`；编译参考示例时使用该目录的 include 路径与选定 provider，构建产物保存在项目 build 目录或 `.agent` 工作目录，不写入 `.mvp`。参考验证不代表正式库已交付。

benchmark 结果按现有 target 写入 results/Release/latest.json；记录 commit、provider、编译器、配置、平台、worker/producer 数、队列实现、资源与 workload，比较前另存基线。长期需求、参考资料和后续能力见 [CORE_DESIGN.md](../CORE_DESIGN.md)。
