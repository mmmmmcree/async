# Async Core 设计

本文规定 async 的目标架构、协议边界和运行时契约。当前 `include/async/core.h` 仍是骨架；execution 已提供 provider 选择、协议及组合算法重导出，`exec::ext` 尚未实现。下文的运行时类型和示例均是目标接口，不表示已经可以编译使用。

设计按使用者看到的组合关系自顶向下展开；首个可运行里程碑的实施顺序见 [CORE_MVP_DEVELOPMENT_PLAN.md](.mvp/CORE_MVP_DEVELOPMENT_PLAN.md)，长期能力顺序见本文第 12 节。修改协议或行为时，应同步更新类型关系、资源与完成契约及验收条件。

## 1. 目标、范围与命名

### 1.1 目标消费场景

首个消费目标是 LCFEngine 仓库 <https://github.com/mmmmmcree/LCFEngine.git> 的 `examples/shader_toy`，用于替换其 Asio 调度。本文提及该项目时均使用仓库 URL 与仓库内相对路径。

运行时必须能组合通用任务控制 loop、N 个 CPU worker，以及 render/gfx 等具有线程归属的外部 loop。专有系统通过注入的逻辑 scheduler 提交通用 CPU 工作，并显式选择完成目标；业务不读取物理 worker 下标，也不依赖其他系统的 scheduler 实现。

Core 应可嵌入、可由调用方驱动，独立支持 record、timer 和 coroutine。标准 sender/receiver 负责任务组合，公开 SchedulerHandle 直接满足标准 scheduler 协议，内部适配实现将运行时能力接入该模型。普通 sender 业务使用 CPU workers 不以协程实现为前提。

### 1.2 架构不变量

- Core 可以依赖标准 execution 协议及其操作/查询接口，provider 由公共 `config.h` 选择，统一别名为 `lcf::async::stde`。SchedulerHandle 直接实现标准 scheduler，适配细节留在 Core；基础 queue/loop/worker/容器只需导出宏时包含 `config/macros.h`。Core 不包含 execution.h、不调用 then/when_all 等组合算法，不使用 stdexec 私有类型；protocols 和 execution 的基础接口不反向依赖 Core。
- 公共 Core 类型通过模板、concept、CPO、ADL 和组合扩展，不建立公共继承体系。`std::pmr::memory_resource` 是预留的标准虚函数资源边界。
- queue/timer 默认借用调用方嵌入的节点；需要拥有闭包或状态时，必须明确分配来源、存活范围和回收路径。
- 已启动操作恰好完成一次。取消、拒收或关闭不能以静默丢弃节点代替完成；请求停止与销毁对象是不同动作。
- 完成可能同步发生，完成函数可能销毁 operation。发布 record 或交付完成后，不得继续假定其所属状态仍存活。
- 跨执行域恢复必须经过目标提交协议。不能因当前线程相同而隐式 inline，也不能把其他域的 continuation 直接作为对称转移结果。
- Core 不提供硬实时保证。预算不能抢占已开始的 C++ 函数、协程段、析构或用户 callback；队列的进展性质也不等于完整提交路径的延迟保证。

### 1.3 命名规则

非 execution 层遵循 LCFEngine 的 `agent-workspace/conventions/cpp-style.md`：普通类型和别名使用 PascalCase，非静态成员函数使用 camelCase，静态成员函数、自由函数、参数及局部变量使用 snake_case，concept 使用 `snake_case_c`。数据成员使用 `m_`，普通常量使用 `k_`，枚举值使用 `ePascalCase`；配置对象保留 `_info`，指针和句柄变量使用约定的语义后缀。

CPO 是可调用接口，对象使用操作名的 snake_case，不加 `k_` 或 `_cpo`；协议标签沿用 execution 的 `snake_case_t` 形式，例如 `try_enqueue_t` 与 `try_enqueue`。普通类文件与类名一致，概念、枚举及操作集合文件使用 snake_case。`protocol.h` 聚合 `protocols/` 下按 sender、receiver、operation_state、scheduler 拆分的协议词汇；操作 CPO 和环境查询分别位于 `execution/operations.h`、`execution/queries.h`。Core 自定义定义按职责放入 `core/concepts.h`、`core/enums.h` 和 `core/operations.h`，其标准概念别名放入 `core/execution_concepts.h`。

`core/` 是头文件目录和依赖边界，不是 C++ 命名空间。Core 的公共类型、concept、CPO 和配置值直接声明在 `lcf::async`；标准 execution 的标记类型、概念和完成签名在 `lcf::async::protocols`，操作 CPO、查询接口和组合算法在 `lcf::async::exec`。只有实现细节才放进局部 `details` 命名空间，不再增加 `lcf::async::core` 这一层。

execution 沿用标准/provider 命名，包括 `protocols::sender`、`exec::schedule`、`exec::get_env` 和 `exec::ext::then_expected`。`core/execution_concepts.h` 直接在 `lcf::async` 中用 `sender_c`、`receiver_c`、`scheduler_c`、`operation_state_c`、`receiver_of_c` 转发原概念；这些别名服务于 Core，execution 直接使用标准概念，不反向包含它们。SchedulerHandle 和内部适配的普通类型仍用项目命名，但 `connect/start/get_env`、`sender_concept`、`completion_signatures` 等协议定制名保留原样；语言要求的 `promise_type/await_suspend`、标准基础词汇和第三方引用亦不改名。适配实现位于 Core 的 details，用户直接取得 SchedulerHandle 并使用标准 CPO。

## 2. 分层、类型与所有权

### 2.1 编译依赖

标准 execution 协议是 Core 调度接口与 execution 算法共同使用的词汇。公共 `config.h` 负责 provider 头文件选择及 `async::stde` 别名；`protocols/` 直接依赖它，按角色重导出词汇，`protocol.h` 只聚合这些头。protocols 不依赖项目的 execution 目录。`execution/operations.h` 和 `execution/queries.h` 同样直接依赖公共 config，重导出基础操作及环境查询；`core/execution_concepts.h` 给 Core 转发概念名称。只需 `LCF_ASYNC_EXPORT` 的基础头包含 `config/macros.h`，因此不必加载 provider。CPO 是实现形式，connect/start/schedule 也参与标准契约；此处按契约词汇、调用入口和组合算法的职责组织，不能把所有 CPO 当成一类算法。异构 record 的基础提交、queue 驱动和存储由 Core 的 concepts/enums/operations 描述，不把这些运行时机制写进标准 sender 协议。

```mermaid
flowchart BT
    P["core/concepts.h · enums.h · operations.h<br/>record / executor_c / queue / 驱动"]
    V["core/execution_concepts.h<br/>Core 使用的标准概念 _c 别名"]
    M["core/memory · cancellation"]
    K["core/containers<br/>FixedCapacityVector 等容器"]
    R["core/scheduling · timer · coroutine"]
    C["core/context"]
    EP["protocol.h · protocols/<br/>角色标记 / 标准概念 / 完成签名"]
    O["execution/operations.h · queries.h<br/>操作 CPO / 环境查询"]
    E["execution.h · algorithms · ext"]
    F["config.h<br/>provider 选择 / async::stde"]
    S["标准 execution / stdexec"]
    A["core/scheduling/SchedulerHandle<br/>details 内部适配"]
    R --> P
    R --> M
    C --> R
    C --> A
    C --> K
    E --> EP
    E --> O
    EP --> F
    O --> F
    F --> S
    V --> EP
    A --> R
    A --> V
    A --> O
```

图中箭头表示“依赖”。Core 各组件只包含自己需要的协议，memory 不因上图分组而依赖 scheduling；Context 是组合层，不被 EventLoop/Worker 反向包含。

| 入口/目录 | 职责 | 依赖边界 |
| --- | --- | --- |
| `config.h`、`config/macros.h` | 前者选择标准 execution/stdexec 并定义 async::stde；后者只定义公共宏 | provider 配置不依赖 Core、protocols 或 execution；基础组件可只包含宏头 |
| `core/concepts.h`、`core/enums.h`、`core/operations.h` | record/executor、Queue/驱动 concepts、接纳结果、提交 CPO | 不需要 execution provider；不包含 queue/loop 实现 |
| `protocol.h`、`protocols/` | 按角色重导出标准标记、概念、完成签名，位于 async::protocols | 直接依赖公共 config.h；不依赖项目 execution 层或 Core |
| `execution/operations.h`、`execution/queries.h` | 操作 CPO、查询及其类型，位于 async::exec | 直接依赖公共 config.h；不包含 Core 或组合算法聚合头 |
| `execution/factories.h`、`completion_adaptors.h`、`composition.h`、`scheduling.h` | sender 工厂、完成处理、合流、调度切换算法，位于 async::exec | 各自直接依赖公共 config.h；由 execution.h 聚合；Core 不包含算法头 |
| `core/execution_concepts.h` | Core 使用的标准概念 _c 别名，直接位于 async | 依赖 protocol.h；execution 不反向包含它 |
| `core/memory`、`core/cancellation` | 资源策略、对象分配、取消状态与 registration | 按需使用标准资源/stop vocabulary |
| `core/containers` | 容器的元素生命周期、容量、布局与访问；内部 FixedCapacityVector 的容量在运行期确定，之后不扩容 | 使用显式 allocator；不依赖 Context、execution 或具体 queue；memory 负责资源策略，容器负责管理元素 |
| `core/scheduling`、`core/timer` | queue、EventLoop、Workers、逻辑目标、标准 scheduler 句柄、驱动预算与 deadline 索引 | 基础机制使用实际需要的协议；SchedulerHandle 和内部 sender/operation 直接使用标准 execution 协议；不调用组合算法 |
| `core/coroutine` | Task、promise、awaiter、frame 与 continuation policy | Core 能力及语言协程协议 |
| `core/context` | 组合与管理 loop、Workers、映射和生命周期；直接返回 SchedulerHandle | Core 组件和标准调度接口；不拥有业务 root scope，不增加句柄策略参数 |
| `execution.h`、后续扩展算法 | 聚合标准词汇、操作/查询接口、标准算法和扩展算法 | 复用统一协议；不依赖 Core |
| `core/scheduling/details/` | 内部 sender/operation 的接纳、执行与完成适配 | 使用 Core 提交接口、概念别名和标准 operations/queries；不建立公开 adapters API，不被基础 queue/loop 反向包含 |

`core.h` 聚合 Core，包括标准调度接口，因此需要选定 execution provider；`execution.h` 聚合协议和算法；`async.h` 是总入口。只使用 Queue、EventLoop、Worker 或固定存储时，可单独包含其基础头文件，不需要 provider。标准协议依赖与编译成本分别评估：当前 stdexec 入口可能包含大量实现，不能仅由命名为 protocol 就推断编译成本很低。

### 2.2 自顶向下的运行时类型图

以下箭头表示组合、能力获取或运行方向，不表示继承或头文件依赖。逻辑层次与实现先后顺序不同；sender 路径可在 operation 层完成，不必经过协程。

```mermaid
flowchart TB
    C["L0 组合入口<br/>async::Context / BasicContext"]
    S["L1 提交句柄<br/>SchedulerHandle：直接满足标准 scheduler<br/>ProducerEndpoint：后续专有入口"]
    Q["L2 逻辑执行目标<br/>LogicalQueue / LogicalQueueToken / WorkerMapping"]
    W["L3 实际执行资源<br/>Workers / Worker / EventLoop"]
    P["L4 机制与策略<br/>InboxSet / IntrusiveReadyQueue / HeapTimerIndex<br/>Clock / WakePolicy / allocation resource / StopState"]
    O["L5 一次操作<br/>ScheduleSender / ScheduleOperation<br/>operation record / TimerOperation"]
    T["L6 协程接口<br/>Task&lt;T, Environment, FramePolicy&gt; / promise_type<br/>ScheduleAwaitable / SleepForAwaitable / SleepUntilAwaitable<br/>ScheduleAwaiter / TimerAwaiter"]
    C -.->|"获取能力"| S
    S -.->|"选定逻辑目标"| Q
    Q -.->|"静态映射"| W
    W -->|"组合并驱动"| P
    P -.->|"存放、索引和推进"| O
    O -.->|"完成 receiver 或恢复 continuation"| T
```

| 层 | 目标类型 | 定位与功能 | 所有权 |
| --- | --- | --- | --- |
| L0 | `BasicContext`、`async::Context` | 装配逻辑队列、执行资源、poll 驱动和关闭协议；Context 是常用配置别名 | MVP 拥有控制 loop 和 worker loops；调用方自有返回 loop 独立驱动；P1 扩展通用外部映射 |
| L0 | `ContextInfo`、`QueueRequest`、`QueueMode` | 描述逻辑需求和 owner/隔离约束，后续增加 worker 预算 | 初始化配置值 |
| L1 | `SchedulerHandle` | 引用逻辑队列身份，直接实现标准 scheduler 和 Core 提交能力；不再外包一层 Scheduler | 可复制、非拥有，只有一个 route 指针，不延长 Context 寿命 |
| L1 | `ProducerEndpoint` | 固定 producer 线程预登记的低延迟提交能力 | 借用目标 lane，保留单 producer 限制 |
| L2 | `LogicalQueue`、`LogicalQueueToken`、`WorkerMapping` | 维护逻辑语义、opaque token 及到 owner/worker 的映射 | Context 管理稳定状态 |
| L3 | `Worker`、`Workers` | Worker 驱动一个绑定 loop；Context 管理 MVP 固定 worker 集合 | Worker 拥有线程并借用 loop；扩展的 worker pool 留给后续阶段 |
| L3 | `BasicEventLoop`、`EventLoop` | 收取、poll、等待和推进 ready/timer/完成 | 不创建线程；控制、worker、调用方返回 loop 各由自己的唯一 owner 驱动 |
| L4 | `MutexInbox`、`BoundedProducerLane`、`InboxSet` | 通用 MPSC、预分配 SPSC lane 和单 owner 汇合 | 节点借用；策略名称在契约稳定后决定是否公开 |
| L4 | `IntrusiveReadyQueue`、`HeapTimerIndex`、`TimerNode` | 本地 ready/backlog 与 deadline 索引 | node 嵌入 operation；动态 backing storage 来源明确 |
| L4 | `Clock`、`WakePolicy`、`std::pmr::memory_resource`、`allocation_resource_c`、`StopState` | 时钟、等待握手、标准/无异常分配能力与取消支持 | 策略或具体状态；资源引用非拥有 |
| L5 | `details::ScheduleSender`、`details::ScheduleOperation` | 内部实现调度；connect 后保存 receiver 和 record，start 时提交；用户通过协议操作 | operation owner 持有稳定存储至最后访问 |
| L5 | 满足 `schedule_record_c` 的 operation record、`TimerOperation` | 一次调度或计时操作的执行/完成状态 | queue/index 仅借用，不隐式拥有协程；Core 不提供固定 `ScheduleRecord` 基类 |
| L6 | `Environment<Executor, Allocator, StopToken>` | 注入实际执行、分配及取消能力 | 持有能力值；引用的服务由各自 owner 保活 |
| L6 | `Task<T, Environment, FramePolicy>`、`promise_type` | 管理协程结果、环境、continuation 和 frame | Task/root owner 管理，不由 Context 自动接管 |
| L6 | `ScheduleAwaitable`、`SleepForAwaitable`、`SleepUntilAwaitable`、`ScheduleAwaiter`、`TimerAwaiter` | 描述 co_await 操作，注册挂起状态并恢复 | awaiter 嵌入或借用一次操作的状态 |

### 2.3 各层需要的协议

| 协议 | Core 基础机制 | execution 通用层 | Core 调度接口/内部适配 |
| --- | --- | --- | --- |
| record、executor、接纳/拒收与存活 | 定义和实现 | 不需要 | 提交并映射完成 |
| queue、timer、clock、wake、资源/取消策略 | 按组件使用 | 不读取具体实现 | 按操作接入能力 |
| C++ coroutine/awaitable | 实现 | provider 协程工具按需使用 | 桥接 sender 与 awaitable |
| sender/receiver、completion signatures、operation state | 按实际需要使用标准定义 | 使用 provider 定义 | 实现具体 sender/operation；用 _c 概念转发约束 |
| `connect/start/set_value/set_error/set_stopped` | 驱动 queue 本身不需要 | 使用 provider CPO | 连接、启动并翻译 Core 结果 |
| 标准 scheduler、environment/query/forwarding | queue/loop 不必实现 | 使用 provider 协议 | SchedulerHandle 直接满足 scheduler；环境按标准查询 |
| allocator、stop token | 使用标准基础词汇和策略 | 通过标准 query 使用 | 保存、转发及适配 |

基础 record 可用静态完成函数或 continuation 交付结果；Core 的标准调度接口直接复用已有 sender/receiver 协议，无需再定义一套替代协议。

## 3. Core 提交协议与一次操作

### 3.1 Record 和 executor

最小调度单位是具有明确存活范围的 record，不是裸 coroutine handle。record 提供静态执行/完成入口，由 operation 或 awaiter 嵌入；异构队列确需擦除时可在边界使用 `void *` 加函数指针，不建立公共 OperationBase。

Record/executor 的接口约束定义在 `core/concepts.h`，提交 CPO 定义在 `core/operations.h`。executor 只表示 record 提交能力，不捆绑 timer、allocator、loop 驱动或 scope 计数。Core 的最小声明只保留 record/executor 两个调用方真正需要的表达式；MVP 暂用成员 CPO，后续再评估 ADL/tag-invoke：

```cpp
namespace lcf::async {

enum class EnqueueResult
{
    eAccepted,
    eFull,
    eClosed,
};

template <typename Record>
concept schedule_record_c = requires(Record & record) {
    { record.getScheduleNode() } noexcept;
};

template <typename Executor, typename Record>
concept executor_c = schedule_record_c<Record>
    && requires(Executor & executor, Record & record) {
        { executor.tryEnqueue(record) } noexcept
            -> std::same_as<EnqueueResult>;
    };

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
    EnqueueResult operator()(
        Executor & executor, Record & record) const noexcept
    {
        return executor.tryEnqueue(record);
    }
};

inline constexpr try_enqueue_t try_enqueue{};

} // namespace lcf::async
```

concept 先检查成员表达式、返回类型和 noexcept，CPO 复用对应 concept 并直接转发。concept 不通过 CPO 定义，避免循环约束。`executor_c<Executor, Record>` 按 Executor 的实际 cv 限定检查；const handle 可经其借用指针访问非 const loop，不需要将底层 queue 整体标为 mutable。

MVP 成员定制使用 `executor.tryEnqueue(record)`；若未来增加 ADL hook，则使用不同名字（例如 `enqueue_record(executor, record)`）与公共对象 `try_enqueue` 区分，避免同命名空间声明冲突。公共调用方只调用 CPO。上面的 `eFull` 属于未来有界入口的目标结果；MVP 的无界 intrusive queue 只需 `eAccepted/eClosed`。

concept 只检查表达式；每个实现还需声明 producer 数量、线程限制、顺序、容量和进展性质。SchedulerHandle 可提供通用多 producer 能力，ProducerEndpoint 保留线程限制，二者不因满足同一 concept 就具有相同并发保证。

### 3.2 接纳与完成契约

| 事件 | record/operation 的责任 |
| --- | --- |
| 构造操作 | owner 持有输入与稳定状态，尚未发布；不隐式执行工作 |
| `eAccepted` | 运行时取得排队访问权；owner 仍负责存储存活，运行时必须执行或按取消/关闭契约交付完成 |
| `eFull/eClosed` | 不接管 record；operation/adapter 按声明处理拒收或保留为尚未提交的业务工作 |
| 执行、取消、到期竞争 | 只允许一个完成胜者；队列摘除不等于所有并发访问已经结束 |
| 完成交付 | 完成函数可能销毁 operation；交付后不得继续访问其成员 |
| 最后访问结束 | owner 才能释放或复用状态；节点脱链、取消 registration 和回收记录也需结束 |

提交不在调用线程 inline 执行 record，但其他 owner 可在发布后、提交返回前消费它。提交实现和 start 路径必须处理这种竞争，不能在发布后再次读取已可能销毁的 operation。

ready queue 和 timer index 不执行 receiver，也不在内部锁下调用用户代码。关闭必须兑现已接纳操作；continuation 入队失败时要有预留容量、有效完成通道或其他明确契约，不能丢失完成，也不能在错误线程直接恢复以伪装成功。

### 3.3 协议复用与边界

`using executor_c;` 可把同一 concept 引入其他命名空间；需要改名时，可定义带完整参数的转发 concept `runtime_executor_c = executor_c<Executor, Record>`。concept 不是类型，不能用类型别名语法替代；通常不新增同义公共名字。

约束放在接口实际消费能力的位置。`BasicContext<EventLoop, Thread>` 检查 loop/thread 的实际驱动和工厂能力，getSchedulerHandle 直接返回 SchedulerHandle<EventLoop>。`BasicEventLoop<Queue>` 使用公开的 `schedule_queue_c<Queue, Node>`。`LogicalQueue<Executor>` 和 `SchedulerHandle<Executor>` 只保存引用，等成员函数得到实际 Record 后检查二元 `executor_c<Executor, Record>`，不人为挑一个 probe record 充当所有 record。内部 receiver_of_c 复用标准完成签名约束；公共句柄在完整定义后用 scheduler_c 验证，避免定义 schedule 返回类型时递归检查自身。单个类型的能力约束优先写成 `template <concept Type>`；跨类型关系、构造参数包和成员的额外启用条件使用 requires。完整逐项审查表见 MVP 指南第 4.6 节。

Core executor 不自动满足标准 scheduler，标准 scheduler 也不自动满足 `executor_c`。反向接入外部 scheduler 时，需要显式 adapter 管理 `schedule/connect/start` 的 operation state 和失败映射，不能强制要求其提供旧版 `enqueue/workStarted/workFinished` 等额外成员，也不能继承低延迟入口的保证。

## 4. 资源、取消与存活

### 4.1 存储来源

`memory_resource` 只解决字节分配，不决定对象共享、arena/pool 选择、并发访问和释放时机。这些由具体 policy 或值类型承担，不再定义带虚函数的 AllocationStrategy。

| 对象 | 资源来源与管理方式 |
| --- | --- |
| Context 配置与固定运行时数组 | 显式 typed allocator、元素数量及字节/对齐报告；配置、worker loops、Workers、routes 分项绑定 |
| queue/timer backing storage | 各自存储 owner 消费的容量与 allocator/policy；不自动使用 Context 数组 allocator |
| record、timer node、connect 返回的 operation、adaptor 成员 | 优先嵌入 owner 的稳定存储，不因统一接口而强制堆分配 |
| async 自有操作的额外动态存储和 owning post 闭包 | receiver 环境可查询的 allocator 优先，否则使用操作绑定的显式 Context 默认值 |
| 无 Context 的 execution 操作 | 自身契约要求 allocator，或调用方显式选用默认资源 |
| coroutine frame | 创建前确定的 FramePolicy；不受后续 start 时环境更改追溯影响 |
| 用户值、异常对象、第三方算法、线程/系统对象 | 分别声明边界，不能声称全部受 Context PMR 控制 |

`memory_resource` 提供字节后端，`polymorphic_allocator<T>` 把元素数量转换为带对齐的申请。PMR 容器从 resource 指针构造是合法便捷接口，不能把接受一个 resource 参数等同于完整的资源设计。每项存储仍需声明数量、容量、分配/释放线程、失败行为与寿命。

MVP 的 `ContextInfo(worker_count, queue_capacity, polymorphic_allocator<QueueBinding>)` 一次分配配置容量，登记只构造元素。`Context::storageRequirements(info)` 在创建前报告 worker loops、Workers 和 routes 的请求字节数/对齐；ContextAllocators 分别提供其 allocator，内部固定存储一次分配，不扩容、不移动元素。配置按值消费，构造完成后可释放临时配置 arena；运行时 allocator/resource 保持到 Context 最后一次释放。控制 loop 直接内嵌，Queue backing storage 由 LoopFactory 独立注入；线程后端另由 ThreadFactory 决定。

这些固定数组由 `core/containers/details/FixedCapacityVector.h` 的内部容器管理：容量在运行期指定，已构造元素数量可逐步增加，元素连续存储且追加不迁移已有元素。`core/memory` 负责分配与回收策略，容器属于 `core/containers`。C++26 `std::inplace_vector<T, N>` 将容量放在类型中、元素内嵌于容器对象，和本轮通过 allocator 获取运行期容量的容器有不同的存储契约；不能仅因固定容量就直接替换。

数量确定后，用正确对齐的有界缓冲区和 null upstream 可以证明元数据无堆 fallback；合并多个数组时必须计入各段对齐，pool 的 chunk/缓存量不能用元素字节数代替。兼容的生命周期与线程需求允许宿主显式共用 resource，但不能默认把所有需求汇入一个 Context resource。逐项申请审查、正确用法和失败验证见 MVP 指南第 6 节；无异常分配能力另按本文第 4.4 节选择。

资源来源显式绑定，不在深层偷偷调用全局 `get_default_resource()`；便捷兼容工厂如 `with_default_resource` 应明确命名。

标准 `get_allocator` 返回 allocator，不是裸 `memory_resource *`；PMR 可用 `std::pmr::polymorphic_allocator` 表达，普通 allocator 走其分配/释放协议，不能统一调用 `.resource()`。标准 query 本身没有 allocator fallback，Context 默认值由宿主/桥接入口显式提供。`execution/queries.h` 重导出 provider 的 `exec::get_allocator`，不新增同义查询。

### 4.2 分配和回收策略

| 策略 | 适用范围 | 必须说明的限制 |
| --- | --- | --- |
| 直接 resource / 显式 `new_delete_resource` | 基线和兼容路径 | 每次分配成本；不得作为隐藏默认 |
| `synchronized_pool_resource`、线程安全 slab | 跨线程短命状态 | 同步与上游成本 |
| `unsynchronized_pool_resource` | 严格单线程分配/释放 | loop 单 consumer 不代表资源只在该线程访问 |
| monotonic/arena | 一批对象整体结束 | 逐个取消不等于立即回收存储 |
| 固定块、recycling/free-list、inline storage | 已有数据支持的热点对象 | 容量耗尽、对齐、扩容及跨线程回收 |

每次分配必须保留释放所需的 allocator/resource、大小和对齐，或由 policy 保证可恢复；不能在释放时重新查询已变更的环境。producer 可能创建状态，worker 或 receiver 可能销毁它；使用线程安全资源，或显式将回收归并到 resource owner。回收记录计入未完成工作，owner/resource 必须保持到 drain。

资源验收覆盖正常/同步完成、构造失败、connect/await_suspend 失败、拒收、取消、关闭、完成中销毁及跨线程释放，不仅检查成功路径。

### 4.3 Hook、统计和特殊分配行为

标准 PMR 路径通过派生 `std::pmr::memory_resource`，在 `do_allocate/do_deallocate` 中包装上游 resource，加入统计、诊断或其他分配策略；`do_is_equal` 保持资源相等与可互相释放的契约。这里使用预留的标准虚函数边界，不新增 async 自有虚基类或转发 Handle。例如宿主可提供：

```cpp
app::TrackingMemoryResource tracked_resource{upstream_resource, statistics};
ContextInfo context_info{worker_count, queue_capacity,
    std::pmr::polymorphic_allocator<QueueBinding>{&tracked_resource}};
```

execution 的普通 allocator 可通过 allocator wrapper 加入同类行为。无异常 resource 则通过满足 `allocation_resource_c` 的组合装饰器包装 `tryAllocate/deallocate`；其 hook 也须不抛异常，不能因记录一次分配而递归触发被跟踪的分配器。

统计明确区分请求字节、成功/失败次数、当前存活字节、峰值和上游实际保留量。分配失败不增加存活字节，释放与原分配对应；多线程计数及记录状态遵守所选 resource 的并发契约。外层装饰器看到的是该层的请求，上游装饰器看到的是 pool/slab 的实际补充，二者不混为同一指标。

统计仅覆盖显式经过该 resource/allocator 的调用。独立 allocator、第三方内部存储和编译器消除的 coroutine frame 不会自动计入；装饰器、上游及统计状态均须覆盖最后访问。

### 4.4 无异常分配与失败返回

若要求分配失败返回错误码且调用 `noexcept`，保证必须由实际字节分配后端实现。`std::pmr::memory_resource::allocate` 和标准 allocator 的 `allocate` 都返回指针并采用失败抛异常的契约；不能只在外面加 `noexcept`、失败时返回 nullptr，或用统计 hook 改写成可恢复错误。

这项需求采用独立的 Core `allocation_resource_c` 协议，放在 `core/memory/allocation.h`，以模板和组合扩展，不依赖 execution，也不新增公共继承体系。C++20 基线用值类型表达结果，避免为基础分配协议要求 `std::expected`：

```cpp
namespace lcf::async {

struct AllocationResult
{
    void * m_storage_p;
    std::error_code m_error;
};

template <typename Resource>
concept allocation_resource_c = requires(
    Resource & resource,
    std::size_t bytes,
    std::size_t alignment,
    void * storage_p) {
    { resource.tryAllocate(bytes, alignment) } noexcept -> std::same_as<AllocationResult>;
    { resource.deallocate(storage_p, bytes, alignment) } noexcept -> std::same_as<void>;
};

} // namespace lcf::async
```

| 层次 | 契约与责任 |
| --- | --- |
| 字节 resource | `tryAllocate(bytes, alignment) noexcept -> AllocationResult`；成功返回有效存储与空 error，失败返回 nullptr 与非空 error，且不遗留须调用方回收的分配；`deallocate` 不抛异常 |
| typed allocator/helper | 检查元素数量乘法溢出和对齐，调用后端并传递错误；构造失败时释放原存储。无异常创建还须约束构造/析构及实际使用的移动操作，字节分配不抛不能保证任意 T 的构造不抛 |
| operation/Task/宿主 | 在各自的创建、connect、start 或 frame 分配阶段交付失败；尚未建立 receiver 的失败用创建结果表达，不能假装已进入 sender error 通道 |

`AllocationResult` 的判别依据是 error；成功/失败的指针关系及零字节、过对齐、容量耗尽的具体规则在最小协议用例中冻结。为恢复原状态而使用的后端清理也在 `noexcept` 边界内。concept 检查返回类型和 noexcept，失败注入测试验证真实语义。

普通资源路径与显式无异常路径分别声明。严格路径从可报告失败的后端开始，不默认通过 try/catch 适配任意 PMR；若提供异常捕获兼容 adapter，必须显式命名并只在启用异常的配置使用。标准 allocator 的 allocate 入口仍须遵守标准契约，其额外 try 分配能力供 async 的受限路径使用；第三方代码只调用标准入口时，不自动获得错误码返回。

标准 `get_allocator` 本身不保证可恢复的无异常分配。受限 operation 对所选 allocator/resource 检查额外能力或声明其不分配条件；无法满足时明确诊断，不能忽略用户 allocator 覆盖而偷偷换成 Context 默认。严格资源的具体类型和值通过显式 ResourcePolicy/工厂绑定，静态类型在创建对应对象前确定，不能塞入普通 PMR 引用冒充同一契约。

资源策略的扩展落在实际存储 owner 上：queue、timer backing storage、typed operation storage 和 FramePolicy 分别消费它们所需的分配接口。`BasicContext` 通过 LoopFactory 为控制/worker loop 分别绑定 Queue 参数，EventLoop 按实际构造关系转发，不自动把数组 allocator 传播到 Queue。基础 queue/event loop concept 不要求默认构造；默认 intrusive queue 没有动态 backing storage，不接受或保存无用 resource。普通 PMR 装饰器已是有效扩展，不必再引入一个只有转发的 ResourcePolicy。

线程资源独立于容器。`std::thread` 的启动状态、栈和系统句柄没有标准 PMR 注入接口；MVP 用 `BasicWorker<EventLoop, Thread>` 和受 thread_factory_c 约束的 factory 暴露后端。默认 StdThreadFactory 明确不保证这些申请受 allocator 控制；严格路径用受控后端或宿主线程驱动 loop。mutex/CV 的平台内部资源、provider 算法、异常对象和用户值同样单独审查，不因某处传入 resource 就宣称全部受控。

`allocation_resource_c` 是供无异常存储 helper/FramePolicy 消费的另一种能力；它不能直接替换标准 PMR allocator。全无异常 Context 创建还需要可报告失败的配置/映射存储、线程后端与创建 API，未实现前不作此承诺。新增存储策略必须覆盖原资源释放、构造回滚、容量失败和跨线程回收；具体结构及默认策略仍由 benchmark 决定。

仅有 hook、pool、slab、arena 或跨线程回收不要求新增分配接口；错误码和无异常失败语义才是上述扩展协议的依据。分配协议也不使线程创建、第三方算法或整个 execution pipeline 自动变为无异常。

### 4.5 取消与任务 owner

请求停止、操作响应取消、停止业务准入、最终关闭执行资源和销毁对象分别定义。可以复用标准 stop vocabulary，也可在满足资源/并发约束时实现 intrusive state/registration；选择不改变操作完成契约。

registration 处理注册与 request_stop 的竞争，以及析构期间 callback 正在运行的情况。用户 callback 不在 queue/timer 内部锁下执行；取消、到期和正常完成必须只有一个胜者。

业务 root owner/scope 管理稳定 operation 存储、stop source、未完成计数和结果交付。Context 管执行资源，不自动收集所有 root；空队列不代表没有挂起的 I/O、timer、子操作或回收工作。完整关闭顺序见第 9 节。

## 5. EventLoop、队列与 timer

### 5.1 单 owner 驱动

EventLoop 是可手动驱动的单 consumer 组件，其他线程只发布/通知。N 个 worker 不能同时调用同一个 EventLoop 的 run/poll。每个 worker 可以驱动自己的 loop；共享多 consumer 后端必须独立满足其契约。

| 成员入口 | 行为 |
| --- | --- |
| `pollOne()` | 不等待，最多执行一个 ready record；遇到暂不可取的入口也及时返回 |
| `Context::poll(PollBudget = {})` | Context 对 `pollOne()` 的非阻塞包装，在 record 间检查数量和时间预算并返回 PollReport |
| `Context/Worker::runOne()` | 上层等待式包装，等待至一个可执行 record 或明确退出条件；最多执行一个 record |
| `Context/Worker::run()` | 上层持续驱动包装，空闲时等待，收到新工作后继续；瞬时空队列不使其退出 |

空唤醒不计为执行记录。WakePolicy 协调 ready 状态、下一个 deadline 和 stop/close；必须避免“检查为空 → producer 发布并通知 → owner 入睡”的丢唤醒。timer 控制若只允许 owner 访问，则跨线程注册/取消通过控制 record 完成。

`pollOne` 限制的是 record 数量。record 可恢复协程直至下一挂起点，用户 awaitable 也可能同步恢复其他协程，不能把它解释为“一次 resume”或固定业务指令数。

`event_loop_c` 只约束 Context 直接调用的基础入口。`PollBudget`、`PollReport`、`poll`、`runOne` 和 `run` 属于 Context/Worker 的组合包装；外部 EventLoop 只需提供基础入口即可接入，是否提供同名便利成员由具体类型决定。

`PollBudget` 和 `PollReport` 定义在 `core/scheduling/Poll.h`，由 `BasicContext.h` 直接包含。`EnqueueResult` 是提交操作必需的返回类型，定义在 `core/enums.h`；批量驱动的预算与报告属于 scheduling。`protocol.h` 只聚合标准 execution 的协议词汇，不包含这些 Core 运行时定义。

### 5.2 队列职责与选择

| 位置 | 并发角色 | 首版选择 |
| --- | --- | --- |
| 跨线程入口 | 多 producer、单 owner | MutexInbox 正确性基线；固定 producer 可登记 BoundedProducerLane |
| owner 本地 ready/backlog | 单线程维护 | IntrusiveReadyQueue，借用节点，不额外分配 |
| 多入口汇合 | 单 owner 收取各 lane/inbox | InboxSet，有界批次和公平推进 |
| CPU 工作入口 | 首版每 worker 独立入口 | 由逻辑映射选定目标；不把 SPSC lane 交给多个 worker 共同 pop |

`inbox` 表示跨线程入口的角色，MVP 用公开的 `schedule_queue_c<Queue, Node>` 描述 EventLoop 实际调用的 `tryEnqueue/tryPop/close/empty`。`schedule_queue_c`、`blocking_schedule_queue_c`、`stoppable_schedule_queue_c` 定义在 `core/concepts.h`，直接声明在 `lcf::async`，作为自定义 Queue 的稳定扩展协议。这是带接纳生命周期的调度 queue 协议，不是强加给所有通用容器或 owner-local ready queue 的统一 concept；`close` 与接纳的同步必须由具体 queue 保证。`Node` 参数允许表达真实存储单位，默认 BasicEventLoop 选择公开的 `ScheduleNode`；概念定义不依赖具体节点布局，也不要求 queue 默认构造。`ScheduleNode` 定义在 `core/scheduling/ScheduleNode.h`，直接声明在 `lcf::async`，供默认 loop、自定义 Queue 和 adapter 共用；它是内嵌 hook，不是 operation 基类。Record 到该节点的 `queue_record_c` 检查仍属于 details，不作为 Queue 的公共协议。

队列策略在构造/producer 登记时选择，业务不在每次提交时指定算法。基础运行时应提供可用的配套 endpoint、唤醒和存活规则，不要求专有系统自行拼接。策略声明顺序、容量、扩容、接纳/拒收、发布未完成状态、节点回收及 unregister/drain 条件；仅有同名 push/pop 不表示符合契约。

| 候选入口 | producer 成本 | consumer 限制 | 定位 |
| --- | --- | --- | --- |
| mutex + intrusive queue | 可能等待其他线程持锁 | 简单、易验证 | 通用基线 |
| CAS inbox，摘链后反转 | 竞争时重试 | 合批维护及本地 backlog | 数据支持后再引入 |
| exchange intrusive MPSC | exchange 与链接发布，无 CAS 重试循环 | producer 暂停可造成发布缺口 | 动态 producer 的后续候选 |
| 固定 producer 的预分配 SPSC ring | 无 producer 间共享入队锁，满时立即返回 | lane 生命周期、扫描和公平性 | 专有线程低延迟优先候选 |

若算法存在发布缺口，内部区分“可取/确认空/发布未完成”；poll 不自旋等待 producer，等待握手保证链接补齐后可再检查，也不能提前回收仍可能被 producer 写入的节点。合批摘链、反转和 timer 维护须有显式成本边界，避免一次 poll 实际变成全队列 drain。

### 5.3 专有 producer 的低延迟契约

目标是让 render/gfx 的已准备 record 不因 TaskSystem consumer 持队列锁而等待，允许 consumer 延后收取；不是设计一把偏向 producer 的通用锁。固定 producer 可按“producer/目标 owner”登记 SPSC lane，普通来源走独立 MPSC 入口，由单 owner 汇合。

```text
render producer --> render lane --+
gfx producer ----> gfx lane ------+--> target worker/loop owner --> local ready
other producers -> MPSC inbox ----+
```

登记、扩容、闭包/operation 构造及输入准备不放在提交快路径。`try_enqueue(endpoint, record)` 满时立即返回 eFull，关闭时返回 eClosed；不循环等待，不隐式转入带锁队列或堆分配 fallback。尚未提交的业务可留到下一帧重试；已接纳操作不能覆盖或静默合并。

有限容量、始终接纳、producer 永不等待和 consumer 无限落后不能同时保证。宿主明确容量与积压处理，owner 对各 lane 有界轮询；单 lane FIFO 不推导跨 lane 全局 FIFO。lane 不是逻辑队列身份，同一 producer 可投递多个逻辑目标。

完整提交还包含通知和拒收清理。通知可合并，但若采用周期检查，必须声明最大检查间隔；若立即唤醒，单独测量其同步/系统调用成本。不能仅凭队列没有 mutex 宣称整个 post 或完整路径 wait-free，也应避免每条 lane 每次争用同一全局计数器。

业务入口满载时，停止通知及已接纳操作的完成仍需可推进，例如预留完成容量或独立控制状态。关闭与内部完成的准入由运行时统一定义，不能仅靠一个 bounded queue 解决。

### 5.4 Timer 和推进预算

timer index 只管理 deadline 和借用 node，不持有 queue、不等待、不直接运行用户代码。首版用 intrusive binary min-heap：TimerNode 保存 heap index，取消/重排为 O(log n)，backing storage 来源显式，Clock 可替换为 fake clock。

| 索引能力 | 契约 |
| --- | --- |
| `insert` | 登记 node，报告是否需调整下一次等待 |
| `remove` | 摘除 node，遵守取消/到期竞争与最后访问规则 |
| `nextDeadline` | 查询最近 deadline，空索引语义明确 |
| `expire` | 有界收集到期 node 交给 loop，不在索引内部调用 callback |

单次等待先实现；若需要重复 timer，再明确周期相对时间、追赶/合并规则。timer 到期后转入 ready，由 operation 决定完成。大量同时到期、提前 timer 和取消控制记录都计入维护预算。

PollBudget 包含 record 数和持续时间限制；PollReport 的 `m_records` 统计已开始并返回的 record，`m_elapsed` 包含维护、执行和必要清理。预算在 record 边界检查，耗时是软限制，无 ready 时不为达到预算而等待。计时使用可替换 Clock/诊断策略，不能强制所有低开销路径承担高精度计时成本。

默认预算支持无参 `poll()`。一次 poll 返回无工作只表示本次未执行 record，不证明挂起操作、外部 producer 或业务 root 已全部完成；drain 仍由 root owner 和关闭协议判定。

## 6. Workers、逻辑队列与 Context

### 6.1 组件职责

在目标架构中，EventLoop 管事件推进，不创建线程；Worker 管一条线程并驱动绑定 loop；Context 管理 MVP 的固定 worker 集合和逻辑映射。`BasicEventLoop<Queue>` 与 `BasicContext<EventLoop, Thread>` 都是模板类，分别受公开的 schedule_queue_c 与实际 loop/thread 能力约束；工厂参数另外检查，不通过继承或 friend 建立组件耦合。每个 loop 都只有一个 consumer，且在其句柄有效期间地址稳定。MVP 包含一个由 Context owner 手动驱动的控制 loop、两个 worker loops，以及由调用方子线程持有并驱动的独立返回 loop；通用外部注册、多种 loop 组合、worker pool 运行期伸缩和额外映射策略留给后续阶段。

Context 可以拥有一个由宿主手动驱动的 loop、仅绑定外部 loop、拥有 CPU Workers，或混合拥有控制 loop/Workers 并借用 render/gfx loop。外部线程的创建、驱动和 join 仍由宿主管理；Context 不越线程归属代为推进。选择自有 CPU Workers 模式时至少配置一个 worker；没有所需 CPU 执行资源时拒绝相应队列请求，不把计算默默转到 main/render。

MVP 的 Context 拥有一个由宿主驱动的控制 loop 和配置指定的 worker loops。`Context::run()` / `pollOne()` / `poll(PollBudget = {})` 都只推进控制 loop；宿主指定唯一 owner，验收中该 owner 为主线程，并使用阻塞等待式 run。每个 Worker 等待并驱动自己的 loop。调用方子线程直接用 EventLoop、LogicalQueue 和 SchedulerHandle 建立返回 scheduler，在心跳循环中 pollOne 并 sleep，不注册进 Context，也不需要另一个 Worker。

`BasicContext<EventLoop, Thread>` 直接内嵌控制 loop，以固定数组分别保存 worker loops、Workers 和 routes。根据 ContextInfo 的数量与 storageRequirements 报告，通过各项 typed PMR allocator 在构造期一次分配；不再依赖 deque 的分块或 vector 的增长。控制驱动和停止直接访问控制成员；worker 路由使用实际 worker 下标，关闭与空状态查询覆盖两类 loops。Context 不可移动，数组不移动元素，路由与 Worker 借用的 loop 地址保持稳定。Context = BasicContext<>；getSchedulerHandle 直接构造标准 SchedulerHandle，不注入句柄策略或额外包装类。

最终验收为：调用方子线程 connect/start → 两 Worker 各 sleep 1s 后返回值 → `when_all` → `continues_on(control_scheduler)` → 主线程控制 continuation 消费合流值 → `continues_on(caller_scheduler)` → 调用方子线程的 Receiver。最终 Receiver 分别验证 value/error/stopped 的线程归属，并调用线程安全的 `context.requestStop()` 唤醒主线程，使 run 返回；主线程 join 调用方后才读取完整结果，再 close/join Context。停止请求持久保存，在 run 前发出也有效；只停止控制 loop 的等待驱动，不关闭准入、不丢排队工作、不停止 Worker。各 loop 在 root 完成前保持准入，跨线程完成数据的存储和移动不抛异常；提前拒收或转移时存储抛异常另测，不能默认仍在目标线程交付。Context 的驱动、停止、关闭和空队列检查不作用于调用方的独立返回 loop。

业务 scheduler 是非拥有能力句柄，不创建线程、不阻塞析构、不隐式延长整个 Context 寿命。scope 的操作存活与执行资源存活通过显式关闭协议协调。

### 6.2 逻辑目标与静态映射

借鉴 LCFEngine Vulkan core 的“逻辑需求 → 实际资源”映射：应用控制逻辑队列数量，宿主决定物理 worker 预算。队列可多对一映射；增加逻辑队列只增加潜在并行，不创造额外执行资源。

| 模式 | 承诺 | 首版映射 |
| --- | --- | --- |
| `QueueMode::eSerial` | 同一逻辑队列的 record 不重叠并具有串行同步关系 | 固定到控制 loop 或一个 worker loop；多个逻辑队列可共享 |
| `QueueMode::eThreadBound` | record 在指定 owner 线程执行 | main/render/gfx 等外部或内部 owner |
| `QueueMode::eParallel` | 独立 record 可以并发，无队列级全局串行顺序 | 后续独立多 worker 分发扩展 |

LogicalQueueToken 与 LogicalQueue 一起定义在 `core/scheduling/LogicalQueue.h`，是由 ContextInfo::addControlQueue/addWorkerQueue 成功返回的不透明值；用户不能默认或数值构造。token 保存不复用的配置域 ID 和路由下标，Context 校验两者。getSchedulerHandle(token) 要求本配置签发的 token，直接返回 SchedulerHandle，不包装 optional；错误归属属于契约违例，失败时终止。需要检查动态来源时调用 findLogicalQueue(token)，跨 Context/过期 token 返回空指针。仅 ContextInfo 是签发 friend；它不借此访问 queue/loop 私有实现。移动配置转移该域并使源对象失效，Context 按值消费配置；容量不足或越界不签发。token 不延长 Context/route 寿命，稳定 handle 仍受关闭与存活契约约束。WorkerIndex 和 QueueBinding 属于 Context 配置，放在 ContextInfo.h。

serial 约束一次 record，不保证整个异步任务跨挂起点独占队列，也不保证任务完成顺序。逻辑身份由运行时和 LogicalQueueToken 决定，不因映射到同一线程而相等；producer tag、lane 与 worker 下标不与之混用。

`LogicalQueue<Executor>` 只保存到 executor 的稳定路由，身份来自路由地址，不要求自行创建 token；因此独立返回 loop 可以直接建立 LogicalQueue{loop}。它不存 record，也不提供 pop/poll；物理 queue 负责 record 存储，EventLoop 负责出队并 dispatch。MVP 的 Context 在构造时用显式 allocator 建立固定路由表，每个成功登记的 token 一项，目标为控制 loop 或指定 worker loop；多个 token 可以映射到同一 loop，但逻辑身份仍然不同。SchedulerHandle 借用路由项；相同 Context/token 得到相等 handle，不同 route 的 handle 不相等。

路由表在发布任何 handle 前按最终登记数一次分配并构建，运行期间不可扩容、移动或改绑，Context 本身也不可移动。后续增加通用 `QueueRequest/QueueMode/WorkerMapping`、外部 loop 与可选并行路由时扩展初始化和路由数据；泛型句柄仍只需要提交关系，不应包含具体 BasicEventLoop/Context 头文件或承担映射求解。

QueueRequest 描述模式、所需能力、owner/affinity 和共享/独占要求；成功登记才签发 token，业务名称由调用方保存。硬约束不足时创建失败；分散到不同 worker 可以是软偏好，业务需要物理隔离时必须声明为硬要求。`hardware_concurrency()` 只是提示，不是可用队列的硬件上限。

映射初始化后稳定，快路径不运行 solver、不创建线程或争用全局路由锁。首版新增配置需 quiesce/重建；动态迁移、work stealing 和权重优先级均为后续项。未来 serial 迁移必须验证唯一执行令牌、同步交接及关闭；thread-bound 不迁移。

参考源码为 LCFEngine 的 `libs/vk_core/src/context/DeviceContext.cpp`、`libs/vk_core/include/vk_core/queue/LogicalQueue.h`、`libs/vk_core/src/queue/QueueAccess.cpp`。借鉴逻辑映射不表示照搬其实际队列 mutex；异步提交的延迟仍由入口和 wake 路径决定。

### 6.3 Producer 和跨域工作

普通 producer 调用 context.getSchedulerHandle(token)，直接获取标准 SchedulerHandle；固定线程另可登记 ProducerEndpoint。标准 sender 的构造、connect 和 start 可处于不同线程，SPSC 限制必须在实际提交线程满足，不能只看获取句柄或构造 sender 的线程。通用 scheduler 只有在 start 时验证绑定，或提供声明清楚的线程安全路由，才可利用 lane。

render 提交 CPU 工作必须支持两种显式路径：

- `render → CPU → render/gfx/main`：直接向目标 worker 提交，完成转回指定域，通常优先。
- `render → main → CPU → render`：需要统一准入或记账时，控制 loop 只执行短的提交 record。

main_loop 表示通用控制角色，不强制为进程主线程。往 main loop 投递普通函数不会自动并行；CPU 重工作必须选择 CPU scheduler。长期阻塞读取或 GPU 等待应由宿主接受占用 worker 的代价，或使用独立阻塞资源/后端。

为 render 优化 main loop 的队列并不改善直接 CPU 提交，低延迟 lane 必须配置到实际目标 worker；若使用共享 mutex 工作后端，经 main 转发只是把锁成本交给 main owner，不是消除了锁。任何共享多 consumer 替代后端单独验证，不直接复用 SPSC 消费路径。

## 7. execution、Environment 和内部适配

### 7.1 Scheduler 与一次调度

execution 继续重导出 provider 类型/CPO，不包装标准算法。公开 SchedulerHandle 直接实现 scheduler_concept/schedule/相等比较，只有一个指向 LogicalQueue 的非拥有指针。schedule 返回内部 details::ScheduleSender，connect 建立 details::ScheduleOperation，start 才调用 Core 提交；operation 内嵌 record，运行或拒收后交付相应完成通道。用户只使用句柄和标准 CPO，不命名内部类型、不显式调用适配转换。

```text
schedule(scheduler) -> ScheduleSender
connect(sender, receiver) -> stable ScheduleOperation
start(operation) -> try_enqueue(executor, record)
record execution / rejection / cancellation -> receiver completion
```

adapter 必须满足选定 provider 的 scheduler/sender/receiver/operation_state concept，同时验证以下语义：

- schedule sender 成功完成为 `set_value_t()`，error/stopped 如实声明；不存在“只要符合 scheduler 就不会失败”的保证。
- 句柄复制、移动、赋值、比较和析构满足目标无异常契约；相等反映同一逻辑执行资源，不只比较物理线程。
- 标准要求可并发的复制、比较、schedule 不引入数据竞争，多个 producer 可启动各自 operation；同一 operation 仅 start 一次。
- scheduler 析构不等待已连接 receiver 完成，关闭/drain/join 归 owner。
- value completion scheduler 与 schedule 使用的句柄一致；error/stopped 仅在确能保证资源归属时提供对应查询。
- 标记、queryable 和 forward-progress query 按版本实现；手动 loop、绑定线程和 CPU workers 如实声明各自的推进能力。

当前工作草案与项目固定 stdexec 的 concept 检查项可能不同，不将某个 provider 接受的最小类型视为全部标准要求。升级或切换 provider 时重新核对。标准 scheduler 不要求 `post/execute/run/poll`；这些不是 Core executor 的标准化同义成员。

### 7.2 Environment 的查询与传播

Environment 是标准 execution 的可查询对象协议，不是固定字段结构体。`get_env(sender)` 返回 sender 属性，`get_env(receiver)` 返回操作接收环境；两者不会自动合并，空环境也不自动带上 Context。

| 查询 | 要求与使用边界 |
| --- | --- |
| `get_env(object)` | 查询不抛，返回 queryable 对象，结果遵守对象存活要求 |
| `get_scheduler(env)` | 有效时结果满足 scheduler；缺失不自动选择当前线程或 CPU pool |
| `get_allocator(env)` | 有效时结果满足对应 allocator 要求；资源优先级按第 4 节，不另建 query |
| `get_stop_token(env)` | 返回符合 stoppable-token 协议的对象；无定制时使用不可停止 token |
| `get_completion_scheduler<Tag>(get_env(sender))` | 描述 value/error/stopped 某通道的完成资源，实际完成必须遵守；不同于 receiver 的关联 scheduler |
| async timer/I/O 服务 query | 属扩展能力，由内部适配实现定义类型、缺失诊断、forwarding 和引用存活规则 |

支持哪些属性由具体环境决定；标准规定有效查询的约束，不要求每个环境提供全部能力。标准 `forwarding_query` 和各 adaptor 语义控制转发；显式覆盖优先，scheduler 转移可能替换或屏蔽关联属性，不能无条件转发所有 query。自定义服务 query 同样明确 forwarding 行为。

Context 默认值由桥接入口显式组合，用户有效 allocator/stop token 覆盖得到保留；sender 属性不会自动覆盖 receiver 环境。必需服务静态缺失时给出约束诊断，动态不可用时按声明交付拒收/error/stopped。环境查询本身不切线程。

### 7.3 调度组合和完成域

`starts_on` 选择启动资源，`continues_on` 显式转移后续完成，普通 `then/let_value` 不自动选择 CPU worker。`starts_on` 不保证所有异步完成留在启动线程，也不自动回提交线程。使用具有返回原域语义的 `on` 时，必须具备有效接收环境 scheduler，并核对 provider 支持。

Receiver 本身是 completion 的接收对象，不拥有“运行线程”属性；哪个线程调用 `set_value/set_error/set_stopped`，对应回调就在该线程执行。`continues_on` 存储上游完成数据并提交调度操作，由目标执行资源推进后再调用下游；`get_completion_scheduler` 描述保证，不负责迁移。最终 Receiver 的三个完成通道都需要调用方线程归属时，显式选择调用方驱动的返回 scheduler，并让该执行域保持到完成返回。

目标调度成功后才可依赖该域；转移被拒收、停止或失败时，不能承诺错误仍在目标 owner 交付。completion signatures 与 completion scheduler 查询必须相符。线程绑定的业务错误收尾通过 owner mailbox 或仍有效的目标调度路径处理，不在来源线程越权修改 render/gfx 状态。

### 7.4 `exec::ext::then_expected`

目标扩展接入 `noexcept` 的 `std::expected<T, E>` 任务函数。它在上游 value 到达时调用函数，不在 sender 构造时执行重工作，也不自行切换 scheduler。

| 输入/结果 | 下游行为 |
| --- | --- |
| 上游 value | 用值参数调用 F，F 必须 nothrow-invocable 并返回 expected |
| `expected<T, E>` 成功 | `set_value(T)` |
| `expected<void, E>` 成功 | `set_value()` |
| `unexpected(E)` | `set_error(E)`，后续 value 分支短路 |
| 上游 error/stopped | 原样传播，跳过 F |

适配器不使用 try/catch 转换异常，违反 noexcept 会按 C++ 规则终止。受限路径还需约束结果交付涉及的 T/E 移动及存储操作；构造、connect、资源分配和其他算法的失败契约分别声明，不能把局部 noexcept 扩大到整个 pipeline。

shader_toy 使用 `E = std::error_code`，但扩展不必局限该错误类型。裸 error_code 返回值如何适配另行设计，不在此接口中隐式推断。`std::expected` 需要 C++23 库能力；当前 C++20 基线下按能力启用该扩展，不默默提升整个 Core 的语言要求。

## 8. Core 协程与环境绑定

### 8.1 静态能力注入

Core 的 Environment 是能力容器；具体 operation 使用哪种 record 时，再检查 `executor_c<Executor, Record>`。execution Environment 则是查询协议，二者由内部适配实现连接，不要求 Core 引用标准 query tag。

```cpp
namespace lcf::async {

template <typename Allocator>
concept allocator_c = requires { typename Allocator::value_type; } && requires(
    Allocator & allocator,
    std::size_t count,
    typename std::allocator_traits<Allocator>::pointer storage_p) {
    typename std::allocator_traits<Allocator>::value_type;
    typename std::allocator_traits<Allocator>::pointer;
    { std::allocator_traits<Allocator>::allocate(allocator, count) }
        -> std::same_as<typename std::allocator_traits<Allocator>::pointer>;
    std::allocator_traits<Allocator>::deallocate(allocator, storage_p, count);
};

template <typename StopToken>
concept stop_token_c = requires(const StopToken & stop_token) {
    { stop_token.stop_requested() } noexcept -> std::convertible_to<bool>;
    { stop_token.stop_possible() } noexcept -> std::convertible_to<bool>;
};

template <typename Executor, allocator_c Allocator, stop_token_c StopToken>
struct Environment
{
    using executor_type = Executor;
    using allocator_type = Allocator;
    using stop_token_type = StopToken;

    Executor m_executor;
    Allocator m_allocator;
    StopToken m_stop_token;

    const Executor & executor() const noexcept { return m_executor; }
    const Allocator & allocator() const noexcept { return m_allocator; }
    const StopToken & stop_token() const noexcept { return m_stop_token; }
};

template <typename Type>
concept environment_c = requires(const Type & environment) {
    typename Type::executor_type;
    typename Type::allocator_type;
    typename Type::stop_token_type;
    { environment.executor() } -> std::same_as<const typename Type::executor_type &>;
    { environment.allocator() } -> std::same_as<const typename Type::allocator_type &>;
    { environment.stop_token() } -> std::same_as<const typename Type::stop_token_type &>;
} && allocator_c<typename Type::allocator_type>
   && stop_token_c<typename Type::stop_token_type>;

template <typename FramePolicy>
concept frame_policy_c = requires(
    FramePolicy & policy,
    std::size_t bytes,
    std::size_t alignment,
    void * storage_p) {
    { policy.allocate(bytes, alignment) } noexcept -> std::same_as<AllocationResult>;
    { policy.deallocate(storage_p, bytes, alignment) } noexcept -> std::same_as<void>;
};

template <typename Value, environment_c Environment, frame_policy_c FramePolicy>
class Task;

template <typename Type>
struct is_duration : std::false_type {};

template <typename Rep, typename Period>
struct is_duration<std::chrono::duration<Rep, Period>> : std::true_type {};

template <typename Duration>
concept duration_c = is_duration<std::remove_cvref_t<Duration>>::value;

template <typename Type>
struct is_time_point : std::false_type {};

template <typename Clock, typename Duration>
struct is_time_point<std::chrono::time_point<Clock, Duration>> : std::true_type {};

template <typename TimePoint>
concept time_point_c = is_time_point<std::remove_cvref_t<TimePoint>>::value;

template <duration_c Duration>
struct SleepForAwaitable;

template <time_point_c TimePoint>
struct SleepUntilAwaitable;

template <typename Rep, typename Period>
    requires duration_c<std::chrono::duration<Rep, Period>>
auto sleep_for(std::chrono::duration<Rep, Period> duration);

template <time_point_c TimePoint>
auto sleep_until(TimePoint deadline);

} // namespace lcf::async
```

这里的限定按协议语义划分，而不是为每个模板参数机械添加 concept。Environment 保存 Executor 时尚不知道具体 Record，保留 `typename Executor`；具体 operation 再按环境访问器实际返回的 cv/ref 类型检查提交，例如 `executor_c<const Executor, Record>`。`Allocator` 使用标准 allocator 形状的 `allocator_c`，先检测 `value_type`，再实例化 allocator_traits，避免 int 等无效候选在检测阶段硬报错；它与第 4.4 节可报告错误的 `allocation_resource_c` 是两种不同能力。`StopToken` 使用 C++20 兼容的 `stop_token_c`，至少提供 `stop_requested()` 和 `stop_possible()`，registration 另由取消协议定义。`Environment` 通过关联类型和访问器满足 `environment_c`，不要求继承具体 Core 类型，也不把 execution 的 query tag 直接写入 Core。

`FramePolicy` 不是任意 tag，必须提供无异常的 frame 分配和释放契约；其 `AllocationResult` 形状与 frame 分配失败的 promise 机制一起冻结。`Task::Value` 暂不增加宽泛的值类型 concept，因为是否支持 `void`、引用和不可移动结果取决于 Task 的结果存储策略；在该策略确定后再定义精确的 `task_value_c`。chrono awaitable 则分别用 `duration_c` 和 `time_point_c` 识别标准 `duration`/`time_point` 特化，避免错误类型落入通用 awaitable 转发。

Task 的 promise 组合结果存储、Environment、continuation、取消 registration 和 FramePolicy。awaiter 只通过 Core executor/服务能力工作；timer/I/O 单独注入，不经 executor 反查具体 EventSource 或 Context。

```mermaid
flowchart TD
    R["receiver environment"] -->|"标准 query 与显式默认值"| B["内部环境绑定"]
    B -->|"创建前确定类型，启动前绑定实际值"| T["Task / promise / awaiter"]
    T -->|"Core executor 与服务协议"| O["Core operation"]
    C["Context / 外部 owner"] -.->|"提供能力并保持存活"| B
```

模板参数只确定类型，实际 executor/allocator/token 值仍需对象承载。`Task<T, Environment, FramePolicy>` 可由宿主提供 PascalCase 常用别名。协程返回类型与 promise 在编译时确定；connect 不能改变已经创建 frame 的类型。若从 receiver 推导静态环境，必须在其类型确定后调用 coroutine factory；固定类型字段也不能通过赋值切换成另一种 executor 类型。

Core Task 独立支持 Core awaitables。接入任意 sender 的 co_await 由内部适配实现经泛型 promise 扩展入口或显式桥接完成；需要环境视图和转换策略时在协程创建时选定，不能把 stdexec 算法硬编码进 Core promise。具体桥接入口在协程阶段通过最小示例确定，再冻结其公开 API。

### 8.2 Frame、挂起与恢复

frame 可能在协程函数调用时分配，早于 connect/start。显式 frame context 参数或延迟调用 coroutine factory 在创建前选择资源；对子协程显式传播，不能依赖跨线程 TLS scope。保存原始释放信息，允许编译器消除 frame，不断言一定发生 allocate/deallocate。

采用无异常分配协议时，FramePolicy 还需结合 promise 的分配失败机制（包括适用的 `get_return_object_on_allocation_failure`）定义 Task 创建失败结果；后端返回 AllocationResult 不会自动改变语言的 frame 分配行为。

语言 Awaitable 协议不会自动传递 execution environment。execution-aware 桥接按 provider 提供 promise 环境视图，并在 await_transform/as_awaitable 路径连接 sender；Core 普通 awaitable 保持语言语义。标准默认 sender-to-awaitable 桥接含异常转换，不能当作 expected/error_code 的无异常实现；普通 await_resume 返回 expected 也不会自动让协程短路。

timer awaitable 使用独立环境服务，不从 executor 反查 EventLoop。`sleep_for(duration)` 与 `sleep_until(deadline)` 是显式操作；async Task 可通过 promise 的 `await_transform(std::chrono::duration)` 提供 `co_await 1ms` 语法糖。duration 转换与其他 awaitable/sender 分发互斥，缺少 timer 能力时明确诊断。相对 deadline 在挂起注册时按该服务的 Clock 计算，非正等待立即完成；让出执行权另用 schedule。timer 服务、恢复目标和取消状态分别注入，基础三字段 Environment 不自动带有 timer。

final_suspend 通过 continuation policy 选择目标提交；严格 loop 边界默认排队，inline 仅为显式策略。挂起发布前准备好结果存储、取消及恢复目标，处理同步完成与 await_suspend 的握手，保证协程不被重复恢复或销毁。环境、服务引用及取消状态覆盖最后访问，不能借用 connect/await_suspend 的临时局部对象。

### 8.3 旧 standalone 实现的取舍

旧提交 `dd81e559f5a928d1da2a87f97b0c3d4364728ece` 将 CoroutineEnvironment 固定为具体 Executor，start 将 `get_scheduler` 结果转换成该类型，awaiter 再调用 enqueue。CallbackExecutor 还要求 workStarted/workFinished，timer 通过 getEventSource 查找服务，因而仅满足标准 scheduler 的类型仍无法接入。

目标保留恢复域和任务存活语义，但分别交给 executor 协议、continuation policy、root owner/scope 和独立服务能力。Environment 模板化后不得再强制收窄为带额外私有要求的 Executor。旧代码可参考竞争处理，不能作为当前头文件依赖或资源默认值的模板。

## 9. shader_toy 目标用法与关闭

### 9.1 执行域与宿主装配

| 角色 | 当前消费端对象 | async 目标 |
| --- | --- | --- |
| 通用控制 | TaskSystem 的 `m_io_context`、`m_worker` | 单 owner 控制 loop，不自动执行 CPU 重工作 |
| CPU 计算 | `m_cpu_io_context`、当前两个 `m_cpu_workers` | 宿主配置 N 个 Workers，逻辑目标静态映射 |
| render | RenderTaskSystem 的 `m_render_io_context`、processOne/waitOne | 外部 owner 驱动 EventLoop，维护 render 状态 |
| gfx | `m_gfx_io_context`、`m_gfx_worker` | 指定 owner 执行上传与图形操作 |

宿主创建资源并注入 scheduler，RenderSystem 不创建共享 worker pool。下面展示未来外部 loop 登记的目标接口；addExternalQueue 尚未进入 MVP，allocator、容量和 worker 映射由宿主先规划：

```cpp
namespace async = lcf::async;

async::ContextInfo context_info{worker_budget, 5, configuration_allocator};
const auto image_token = context_info.addWorkerQueue(image_worker).value();
const auto shader_token = context_info.addWorkerQueue(shader_worker).value();
const auto resource_token = context_info.addWorkerQueue(resource_worker).value();
const auto gfx_token = context_info.addExternalQueue(gfx_loop).value();
const auto render_token = context_info.addExternalQueue(render_loop).value();

// Runtime metadata, queue storage and thread backend are separate choices.
async::Context async_context{
    std::move(context_info), context_allocators, loop_factory, thread_factory};

const auto task_context = ShaderToyTaskContext{
    .m_image_scheduler = async_context.getSchedulerHandle(image_token),
    .m_shader_scheduler = async_context.getSchedulerHandle(shader_token),
    .m_resource_scheduler = async_context.getSchedulerHandle(resource_token),
    .m_gfx_scheduler = async_context.getSchedulerHandle(gfx_token),
    .m_render_scheduler = async_context.getSchedulerHandle(render_token),
    .m_device_context = device_context,
    .m_gfx_task_queue = gfx_task_queue,
};
```

前三条是不同 serial 逻辑队列，可在一个 worker 上正确串行推进，映射分散且物理资源足够时才有实际并行；gfx/render 绑定其 owner。外部 loop 也使用显式资源与容量策略。专有低延迟 endpoint 的登记和实际 start 线程约束按第 5、6 节处理。

### 9.2 expected 资源任务流水线

LCFEngine 的 `examples/shader_toy/render_system/src/private_include/ShaderToyResourceTasks.h` 声明了 load_image、upload_image、load_shader_source、compile_shader_source 等 noexcept expected 任务。下例的 `app::` 函数是目标宿主封装：同步任务返回 expected，批量 shader 封装保留该契约，真正异步的 gfx 操作用 sender helper 表达。

```cpp
namespace ex = async::exec;

auto images = ex::starts_on(task_context.m_image_scheduler,
        ex::just(image_path) | ex::ext::then_expected(app::load_image))
    | ex::continues_on(task_context.m_gfx_scheduler)
    | ex::ext::then_expected([task_context](Image pixels) noexcept {
        return app::upload_image(task_context, std::move(pixels));
    })
    | ex::let_value([task_context](const ImageResourceInfo & image_resource_info) {
        return app::create_cloud_sea_packet_sender(task_context, image_resource_info);
    });

auto shaders = ex::starts_on(task_context.m_shader_scheduler,
    ex::just(shader_paths)
        | ex::ext::then_expected(app::load_shader_sources)
        | ex::ext::then_expected(app::compile_shader_sources));

auto prepared = ex::when_all(std::move(images), std::move(shaders))
    | ex::continues_on(task_context.m_gfx_scheduler)
    | ex::let_value([task_context](const CloudSeaPacket & packet, const ShaderProgramInfoList & programs) {
        return app::prepare_passes_sender(task_context, packet, programs);
    })
    | ex::continues_on(task_context.m_resource_scheduler)
    | ex::let_value([task_context, swapchain_lease](const InstanceInfo & instance_info) {
        return app::build_instance_sender(task_context, swapchain_lease, instance_info);
    })
    | ex::continues_on(task_context.m_render_scheduler)
    | ex::let_value([render_state](PreparedCloudSea & result) {
        return app::install_render_state_sender(render_state, std::move(result));
    });

// The application owner constructs operation states at stable addresses.
resource_task_owner.connectAndStart(
    std::move(prepared), ResourceRootReceiver{completion_mailbox});
```

图片与 shader 分支独立启动，when_all 汇合；expected 失败经 error channel 跳过后续 value 阶段。线程绑定的异步 helper 必须声明完成域；如果 helper 不保证仍在 gfx，则下一步操作前再次 continues_on，不能仅靠最初 starts_on 推导线程归属。无异常承诺仅适用于各已声明的任务/适配路径。

helper 按值持有异步阶段需要的数据；路径、shader 名单、swapchain lease、render state、服务引用和图形资源由 root/宿主保活。ResourceTaskOwner 在稳定地址直接构造 connect 结果，不要求 operation state 可移动；登记完成追踪后再 start，完成回调中可安全回收。

ResourceRootReceiver 的 error/stopped 发布到线程安全 completion mailbox，由指定 owner 消费；目标调度拒收时，不能在错误来源线程修改 render 状态。`AsyncTaskChain/AsyncTaskFlow` 的共享 DAG、多订阅与一次启动语义须另行确认，when_all 不自动覆盖全部需求。这里是 sender 图，不依赖尚未实现的 Task。

### 9.3 关闭顺序

1. 停止新的业务 root 与 file watcher 等外部 producer；向已有 root 请求取消。
2. 保持 main/render/gfx 和 CPU 资源可推进，停止业务准入但允许已接纳操作所需的内部完成、取消与回收记录。
3. root owner/scope 追踪子操作、跨域完成、mailbox 和回收工作，等待全部 drain；owner 继续驱动，不能阻塞等待依赖自身的工作。
4. 确认没有后续投递者后解除外部 loop 借用，最终关闭内部执行资源，从安全的宿主线程 join 自有 Workers。
5. 最后释放 root 存储、外部 owner、图形资源和 allocator/resource。析构只回收已满足关闭前置条件的对象，不偷偷执行可能死锁的 drain/join。

thread-bound 完成必须在 owner 有效期间排空。若 scheduler 已关闭而拒收，按声明恰好交付失败/停止，不能强行恢复到来源线程。首版保证一次完整生命周期；若要兼容旧 TaskSystem 的 run/stop/run，需另定义 restart、旧句柄和残留状态的关系并验收。

## 10. 验收与性能基线

### 10.1 正确性验收矩阵

以下编号供开发计划引用。每项按可观察契约拆成测试，通过可控信号/gate 构造并发，不用线程 ID 抽样或计时猜测代替正确性。

| 编号 | 必须证明的行为 |
| --- | --- |
| V01 分层 | Core 调度层可依赖标准 execution 协议，基础 queue/loop/worker 头可独立使用；Core 不调用组合算法或 provider 私有类型；_c 概念转发保持原约束，CPO 重导出保持 provider 身份 |
| V02 操作存活 | 同步完成、发布后立即消费、完成中销毁、拒收、取消和关闭均恰好完成；operation/node 不早释放、不重复回收 |
| V03 入口与唤醒 | 多 producer 各自提交，单 owner 消费；空闲再投递、入睡竞态、stop/提前 timer 不丢唤醒；lane 满载不阻塞控制/完成 |
| V04 驱动预算 | Context 的 pollOne/poll 实际驱动 execution 并完成 receiver；pollOne 非阻塞且最多执行一个 record，poll 遵守数量/软时间预算；runOne 等待且不将空唤醒计数；维护和长 callback 遵守软预算声明 |
| V05 Timer/取消 | fake clock 验证注册、取消、到期竞争和 burst 维护；registration 析构与 callback 竞争安全 |
| V06 映射与并行 | 1/2/N worker、逻辑队列多于 worker 均正确；serial 不重叠、thread-bound 命中 owner；硬隔离不足则失败，资源允许时用 gate 证明并行 |
| V07 组合与关闭 | 纯外部 loop、自有 Workers、混合配置均能 drain；关闭覆盖在途 CPU/跨域/回收/mailbox，不 join 自身、不接管外部线程 |
| V08 Scheduler | concept 及资源相等、跨线程构造/connect/start、并发 schedule/start、非阻塞析构、各完成通道及真实 completion scheduler |
| V09 Environment/资源 | 空环境、缺失能力诊断、覆盖/forwarding、不混淆 sender 属性；普通 allocator/PMR 与 hook 统计；无异常资源失败注入、能力负例、构造回滚、跨线程释放和失败回收 |
| V10 expected 扩展 | success/error/void、上游 error/stopped 跳过 F、nothrow 约束和 completion signatures；支持的无异常子集在禁用异常配置下单独验证 |
| V11 协程 | 两种 executor/环境实现接入同一 Core 协议；挂起握手、取消和恢复域正确；frame 创建早于环境绑定仍使用原资源，子协程传播明确 |
| V12 消费端 | 两条 render/CPU 路径、图片/shader 汇合、gfx/render owner、每阶段 error/stopped、root 输入寿命及 watcher 停止/完整 drain |
| V13 低延迟策略 | 暂停 consumer 时未满 lane 仍发布、满时立即拒收；其他 producer 暂停不阻塞独立 lane；若引入弱 MPSC，验证发布缺口、不自旋与安全回收 |

shader_toy 的 sender 迁移需要 V01–V10、V12–V13；使用 co_await 后再要求 V11。可选队列算法仅在启用时要求其专属测试，不用 benchmark 代替这些验收。

### 10.2 性能测量

| 范围 | 分离测量的项目 |
| --- | --- |
| 提交 | record 准备、发布、wake、拒收清理及完整提交；p50/p99/p99.9、观测最大值与积压峰值 |
| 消费与公平性 | 发布至收取/执行延迟、扫描/合批成本、各逻辑队列等待、通知合并及饥饿 |
| 驱动 | 空 poll、一次 record、预算达到、长 callback、维护和最终清理 |
| Timer | 不同 active 数量和 deadline 分布；插入/取消/重排、同时到期、精度、每 node 字节和 wake 次数 |
| 分配 | allocate/tryAllocate/deallocate、对齐、失败/构造回滚、跨线程回收、hook 成本、同步竞争、请求与上游次数、峰值保留及缓存局部性 |
| 协程与业务 | frame 创建/销毁、一次 resume、最终 callback；1/2/N worker 下完整任务图与阻塞工作占用 |

观测最大值不代表已证明的最坏时间上界。性能结果记录平台、负载与配置，作为版本基线；替换算法或默认策略须同时通过正确性和目标负载测量。

每次优化先固定目标 workload 和可观察契约，保存当前基线，再替换一个机制；完成后运行相关 verify 与 Context 集成回归，并在相同 workload/配置下比较性能。结果记录 commit、provider/编译器、构建配置、平台、worker/producer 数、容量和 workload。尾延迟使用逐操作采样或直方图，不能从 Google Benchmark 整轮均值推导 p99。

## 11. 实施边界与待决选择

目标架构采用单 owner EventLoop、mutex MPSC 基线、intrusive ready、heap timer、静态逻辑映射和显式 root owner。首个交付包含主线程控制 loop、固定 worker loops 和调用方自有返回 loop：调用方线程启动任务，两 Worker 产出值并合流，主线程控制 continuation 消费结果，最终 Receiver 回到调用方线程；验证三种完成通道、关闭和 drain，并建立 verify/benchmark 基线。MVP 的具体实施顺序见 [CORE_MVP_DEVELOPMENT_PLAN.md](.mvp/CORE_MVP_DEVELOPMENT_PLAN.md)。

| 已确定的边界 | 后续用实现和数据选择 |
| --- | --- |
| 队列可替换、线程约束不变 | 普通入口 mutex/exchange/CAS、lane 容量、扫描和 wake 策略 |
| serial/thread-bound 语义与静态逻辑身份 | 分散偏好和隔离配置细节；parallel、迁移和 work stealing 后置 |
| timer 只索引、loop 推进、资源显式 | heap backing 容量、tree/wheel/calendar 替代及重复 timer 规则 |
| frame 创建前选资源、环境不依赖具体 executor | coroutine factory/promise 绑定入口、显式 inline policy 及无异常桥接 |
| 跨线程回收必须安全、取消恰好完成 | synchronized resource 或 owner 回收，stop registration 的具体实现 |
| 普通配置借用 PMR；可恢复的无异常分配采用独立协议 | hook/统计的装饰位置、ResourcePolicy 绑定、typed helper 和错误交付，不为转发增加 Handle |
| 运行时存活与业务 scope 分离 | root scope 是否需要通用库封装；共享 DAG 和 restart 按消费需求确认 |

具体实现不得靠新增继承体系、调用上层组合算法或使用 provider 私有类型来掩盖运行时接口问题；标准 execution 协议是允许的直接依赖。长期能力按第 12 节演进，每阶段仍须满足本章的正确性和性能验收。

## 12. 能力演进路线

MVP 完成后沿既有 Context → execution 路径扩展；阶段可以并行安排，但各自的前置契约和验收不得省略。

| 阶段 | 前置 | 能力范围 | 验收矩阵 |
| --- | --- | --- | --- |
| P1 运行策略与外部映射扩展 | MVP 已验证 run/requestStop 和独立调用方返回 loop | 可配置等待策略、可选 runOne 包装、外部 loop 的通用注册/借用、thread-bound 路由与扩展映射策略 | V03、V04、V06–V08 |
| P2 环境与资源扩展 | MVP；多域回收验证依赖 P1 | allocator 覆盖与 forwarding、PMR/allocator hook、`allocation_resource_c` 和 typed helper、无异常失败交付、取消 registration 与跨线程回收 | V02、V09 |
| P3 timer | MVP、P2；等待驱动依赖 P1 | 独立 timer service、`HeapTimerIndex`、`TimerOperation`、fake Clock、跨线程控制 record、一次性等待和提前 deadline 唤醒 | V03–V05 |
| P4 专有 producer 入口 | MVP；多目标路由依赖 P1 | 预分配 `BoundedProducerLane`、`ProducerEndpoint`、有界公平汇合与 unregister/drain；满载明确拒收，不隐式退化到阻塞队列或动态分配 | V03、V13 |
| P5 expected 扩展 | MVP | execution 独立的 `then_expected`，覆盖 value/error/void、上游短路、nothrow 约束和 completion signatures | V10 |
| P6 协程 | P2、P3；多域恢复另需 P1 | Core `Environment`、`Task`、`FramePolicy`、schedule/sleep awaitable、duration `await_transform`，随后在内部桥接 sender | V02、V09、V11 |
| P7 shader_toy 接入 | P1–P5；协程可选依赖 P6 | 迁移 LCFEngine 仓库 <https://github.com/mmmmmcree/LCFEngine.git> 的 `examples/shader_toy`；分阶段打通 render/CPU/gfx 调度、expected 任务图、owner mailbox、停止与完整关闭 | V01–V10、V12–V13；使用协程时加 V11 |

依赖关系概览：

```mermaid
flowchart LR
    M["MVP"] --> P1
    M --> P2
    M --> P4
    M --> P5
    P2 --> P3
    P2 --> P6
    P3 --> P6
    P1 --> P7
    P2 --> P7
    P3 --> P7
    P4 --> P7
    P5 --> P7
    P6 -. "使用协程时" .-> P7
```

阶段边界：P2 的错误码分配是独立能力，不通过包装 PMR 或增加转发 Handle 实现；标准 stop vocabulary 优先，确有并发/资源需要才新增状态。P3 先实现单次等待；重复 timer 与 timing wheel 后置。P6 分开完成 Core Task 与 sender 桥接，并按 promise/frame 机制处理分配失败。P7 先覆盖 `render → CPU → render/gfx` 和 `render → main → CPU → render`，再接图片/shader 汇合及真正异步 gfx 操作；thread-bound error/stopped 由 owner mailbox 收尾。

## 13. 参考资料

标准链接跟随工作草案变化，具体实现以构建时选定 provider/版本为准。第三方源码仅作机制参考，不增加依赖或承诺照搬其默认资源/线程模型。

### 13.1 execution 与语言协议

- queryable、get_env、queries、forwarding：<https://eel.is/c++draft/exec.queryable>、<https://eel.is/c++draft/exec.get.env>、<https://eel.is/c++draft/exec.queries>、<https://eel.is/c++draft/exec.fwd.env>。
- scheduler、schedule、异步操作和完成资源：<https://eel.is/c++draft/exec.sched>、<https://eel.is/c++draft/exec.schedule>、<https://eel.is/c++draft/exec.async.ops>、<https://eel.is/c++draft/exec.get.compl.sched>。
- starts_on/continues_on 与协程适配：<https://eel.is/c++draft/exec.starts.on>、<https://eel.is/c++draft/exec.continues.on>、<https://eel.is/c++draft/exec.as.awaitable>。
- frame allocation、co_await 与跨线程恢复：<https://eel.is/c++draft/dcl.fct.def.coroutine>、<https://eel.is/c++draft/expr.await>、<https://eel.is/c++draft/coroutine.handle.resumption>。
- 标准资源、pool、allocator、stop token 与线程数提示：<https://eel.is/c++draft/mem.res>、<https://eel.is/c++draft/mem.res.pool>、<https://eel.is/c++draft/mem.poly.allocator>、<https://eel.is/c++draft/stoptoken>、<https://eel.is/c++draft/thread.thread.static>。

### 13.2 调度、队列与 timer

- Asio poll_one/post 及 intrusive op_queue：<https://think-async.com/Asio/asio-1.30.2/doc/asio/reference/io_context/poll_one.html>、<https://think-async.com/Asio/asio-1.30.2/doc/asio/reference/post/overload1.html>、<https://github.com/chriskohlhoff/asio/blob/asio-1-30-2/asio/include/asio/detail/op_queue.hpp>。
- libuv loop 阶段、驱动与合并通知：<https://docs.libuv.org/en/v1.x/design.html>、<https://docs.libuv.org/en/v1.x/loop.html>、<https://docs.libuv.org/en/v1.x/async.html>。
- exchange MPSC 发布缺口、CAS inbox 与 SPSC：<https://sites.google.com/site/1024cores/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue>、<https://www.boost.org/latest/doc/html/doxygen/classboost_1_1lockfree_1_1mpsc__weak__queue.html>、<https://github.com/facebook/folly/blob/main/folly/AtomicIntrusiveLinkedList.h>、<https://www.boost.org/latest/doc/html/doxygen/classboost_1_1lockfree_1_1spsc__queue.html>。
- Asio timer heap 与 libuv heap/timer：<https://github.com/chriskohlhoff/asio/blob/asio-1-30-2/asio/include/asio/detail/timer_queue.hpp>、<https://github.com/libuv/libuv/blob/v1.48.0/src/heap-inl.h>、<https://github.com/libuv/libuv/blob/v1.48.0/src/timer.c>、<https://docs.libuv.org/en/v1.x/timer.html>。
- Seastar timer_set 与 Asio 串行执行契约：<https://github.com/scylladb/seastar/blob/master/include/seastar/core/timer-set.hh>、<https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/core/strands.html>。

### 13.3 资源、协程与取消实现

- Asio associated allocator、frame 回收和取消 slot：<https://think-async.com/Asio/asio-1.30.2/doc/asio/overview/core/allocation.html>、<https://github.com/chriskohlhoff/asio/blob/asio-1-30-2/asio/include/asio/impl/awaitable.hpp>、<https://think-async.com/Asio/asio-1.30.2/doc/asio/overview/core/cancellation.html>。
- cppcoro 嵌入 operation、Task 与取消 registration：<https://github.com/lewissbaker/cppcoro/blob/master/include/cppcoro/static_thread_pool.hpp>、<https://github.com/lewissbaker/cppcoro/blob/master/include/cppcoro/task.hpp>、<https://github.com/lewissbaker/cppcoro/blob/master/include/cppcoro/cancellation_registration.hpp>。
- libuv 全局 allocator 的边界：<https://docs.libuv.org/en/v1.x/misc.html>；LLVM arena/recycling：<https://llvm.org/docs/ProgrammersManual.html#pointers-and-allocations>、<https://llvm.org/doxygen/classllvm_1_1RecyclingAllocator.html>。
- Boost.Context 分配策略与 EASTL allocator：<https://www.boost.org/doc/libs/1_85_0/libs/context/doc/html/context/stack.html>、<https://github.com/electronicarts/EASTL/blob/master/include/EASTL/allocator.h>。
