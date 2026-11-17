# Async Core

## 核心概念与组合

图中的 concept 表示结构化接口约束，类名给出当前实现。箭头标明组合或依赖关系，不表示继承或运行时调用顺序。

```mermaid
flowchart TB
    context["Context：资源与生命周期边界<br/>BasicContext"]
    context -->|按组件类型装配| components["context_components_c<br/>完整组件的类型绑定<br/>DefaultContextComponents（BasicContext.h）"]
    components --> stages["同级处理阶段<br/>request_dispatcher_c / work_executor_c<br/>BasicDispatcher / BasicExecutor"]
    stages -->|复用单步搬运| driver["Driver：连接输入与接收端<br/>driver_c：driveOne → DriveReport<br/>BasicDriver / utils::drive_one"]
    driver -->|借用输入| stream["Stream：可组合的记录源<br/>stream_c：pullOne / isEmpty<br/>DispatchRequestStream / WorkStealingStream"]
    driver -->|组合接收端| sink["Sink：可组合的记录接收端<br/>sink_c：receive<br/>SubmissionSink / DispatchRequestSink"]
    stream -->|组合一条或多条| queue["Queue：记录存储与领取<br/>producer_queue_c / consumer_queue_c<br/>Queue / Deque"]
    sink -->|入队接收端依赖| queue
    stream -->|选择记录来源| policy["Policy：选择候选者及顺序<br/>selection_policy_c：select<br/>RoundRobin / WeightedPriority"]
    sink -->|路由接收端选择目标| policy
    sink -->|终端接收端调用| executable["Executable：执行或取消任务<br/>executable_c / cancellable_executable_c<br/>ExecuteExecutableSink / CancelExecutableSink<br/>→ AnyExecutableRef"]
```

Stream 可以组合 Queue 或其他 Stream，封装选择策略及 pending 等行为；Sink 封装接收、路由或终端处理，也可以继续包装其他 Sink。Driver 只负责一次 `pullOne → receive`，不靠 Sink 的静态标签判断是否执行任务。Dispatcher 与 Executor 复用 BasicDriver，分别给出 `DispatchReport` 和 `ExecutionReport`；两者及 `DriveReport` 均通过 `getErrorCode()` 读取结果码。

`BasicContext<Components>` 通过一个组件集配置流水线、事件泵与线程句柄。`context_components_c<Components>` 检查 `event_pump_t`、`thread_handle_t` 的协议和构造要求，以及组件工厂组合后的构造接口、记录类型一致性、队列并发能力、Stream 重试与清理能力、Sink 和执行阶段的报告协议。Notifier 从配置的事件泵推导，并用于检查实际组合；默认组件与 BasicContext 放在同一头文件中。

Stream/Sink 与 execution 的 sender/receiver 共享组合思路，但属于不同协议：Stream 是同步、非阻塞的记录源，Sink 是同步接收边界；它们不因此满足 `sender_c` / `receiver_c`。`SchedulerHandle` 通过 `ScheduleSender` / `ScheduleOperation` 接入 execution，最终完成仍通过 receiver 的 `set_value / set_error / set_stopped` 交付。

能力按使用场景细化，不要求所有组件承担重试、清理或等待：

```mermaid
flowchart TB
    transfer["单步搬运契约<br/>matched_c&lt;Stream, Sink&gt;<br/>Stream 与 Sink 的 value_t 相同"]
    transfer -->|基础输入| stream["stream_c<br/>pullOne → value_t 指针或空"]
    transfer -->|接收结果| sink["sink_c<br/>receive → void 或 error_code"]
    sink -->|rejecting_sink_c：返回 error_code| retry["retry_stream_c<br/>拒收后 requeue，记录仍由流水线保留"]
    stream -->|可选重试能力| retry
    stream -->|可选停止清理能力| drain["drainable_stream_c<br/>pullAny 绕过选择策略<br/>DispatchRequestStream / PriorityStream"]
    sink -->|返回 void| accepted["保证接收<br/>ExecuteExecutableSink / CancelExecutableSink"]
    retry --> recover["no_buffer_space：保留并重试<br/>其他非空 ec：保留并进入 report"]
    drain --> cleanup["静止后统一取消剩余记录<br/>CancelExecutableSink → executable.cancel"]
```

`std::error_code` 是接收结果，不等同于错误；空值表示已接收，非空值表示未接收。`retry_stream_c` 只在接收端可能拒收时由 Driver 要求；`drainable_stream_c` 用于停止清理，不属于基础 Stream 的必需能力。

运行宿主负责决定何时、在哪个线程推进这些组件；服务和事件后端复用单步驱动协议，不要求其内部由 Stream/Sink 实现：

```mermaid
flowchart TB
    context["Context：装配与关闭<br/>BasicContext"]
    context -->|提供用户入口| ports["SubmissionPort / ExecutionPort<br/>取得 SchedulerHandle / 推进执行"]
    context -->|拥有线程生命周期| workers["BasicWorker / WorkerPool<br/>等待、唤醒与重复执行一步"]
    ports -->|owner 执行入口| executor["DispatchingExecutor<br/>先 dispatchOne，再 executeOne"]
    workers -->|驱动| executor
    workers -->|停止查询、完成判断与错误码汇总| coordinator["worker_coordinator_c<br/>ContextCoordinator"]
    coordinator -->|复用等待协议| waiter["notify::signal_waiter_c<br/>EventPumpSignal"]
    context -->|拥有并驱动| host["ServiceHost + EventPump<br/>就绪服务、定时激活与统一等待"]
    host -->|非拥有登记| service["service_c：特化的 driver_c<br/>start / driveOne / requestStop<br/>AnyServiceRef / sandbox OneShotTimer"]
    host -->|事件泵协议| pump["notify::event_pump_c：特化的 driver_c<br/>driveOne / waitUntil / wake<br/>SemaphoreEvent / 自定义平台事件泵"]
    service -->|就绪、定时、停止完成通知| handle["ServiceHandle<br/>notifyReady / setWakeTime / notifyStopped"]
```

`getExecutionPort(token)` 只执行该 port 的 local 队列，不经过 `DispatchingExecutor`。后者仅组合 dispatch 与 execute，并通过报告传递结果；Worker 和 Context 负责向 Coordinator 汇总错误码。`worker_coordinator_c` 位于 worker 模块，具体的 `ContextCoordinator` 留在 context/details。

ExecutionPort 负责等待与唤醒循环；BasicContext 分别提供驱动线程执行与 local 执行的内部接口。`run()`、`shutdown()` 和服务启用失败后的清理共用关闭驱动循环。Context 只记录优雅关闭请求，快速停止以 Coordinator 的执行 gate 为准；空 `ec` 不触发停止。

Worker 在每步执行前检查停止请求，退出后的静止由线程 join 保证。`worker_coordinator_c` 复用 `notify::signal_waiter_c` 约束 `observe / wait`，不要求执行计数接口；Context 使用自己的 `ExecutionGuard` 等待外部 local ExecutionPort 的在途执行。

`BasicWorker::start()` 与 `WorkerPool::start()` 均为 `noexcept`，返回 `std::error_code`；线程启动异常在该边界转换。Pool 部分启动失败时，先请求停止并 join 已启动线程，再返回原始结果码。`ServiceHost` 借用服务；Context 拥有默认事件泵与固定登记存储，平台事件能力由具体事件泵提供。

## 运行流程

服务激活与事件转换：生命周期和 `driveOne()` 由 Context driver thread 串行调用；`ServiceHandle` 可跨线程使用。

```mermaid
flowchart TB
    source["OS 事件回调 / 外部事件 / 服务定时请求"]
    source --> handle["ServiceHandle<br/>notifyReady / setWakeTime"]
    handle --> registration["ServiceHost<br/>intrusive Queue 合并 ready<br/>IndexedHeap 更新 timer 槽"]
    registration --> wake["EventPump.wake()"]
    wake --> wait["Context driver thread<br/>waitUntil 最近期限；event_pump.driveOne()"]
    wait --> ready["ServiceHost<br/>定时到期转 ready；取一个就绪服务"]
    ready --> service["Service.driveOne()<br/>采集事件，生成提交"]
    service --> submit["SubmissionPort / SchedulerHandle"]
```

`enableServiceHandling()` 调用 `Service.start(handle)`；`run()` 自动启用。Service 由调用方持有，通过 `getEventPump()` 借用事件泵。默认 `notify::SemaphoreEvent` 只提供定时等待和跨线程唤醒；平台 OS 事件注册及回调由自定义事件泵提供。休眠服务不逐个轮询。

`ServiceHost::get_storage_requirements(capacity)` 返回公共 `StorageRequirements`，表达字节数和对齐要求；`create(capacity, memory_resource&)` 准备固定登记项和定时堆，预算查询与分配共用内部布局定义。Context 汇总组件需求，布局模板留在组件内部。就绪队列直接链接登记项，重复通知通过链接状态合并；`IndexedHeap` 维护定时项的位置，支持 O(log N) 更新和删除。状态修改由 Host 的互斥锁协调，调用服务与唤醒事件泵在解锁后进行。

提交与执行流水线：实线表示记录流向，记录及 executable 均借用 operation 的存储。

```mermaid
flowchart TB
    port["SubmissionPort<br/>调用方或 service driver thread"]
    port --> scheduler["SchedulerHandle<br/>目标 + 继承或覆盖的优先级"]
    scheduler --> operation["ScheduleOperation<br/>SchedulingRecord + executable"]
    operation --> gate["SubmissionGate"]
    gate --> submission["SubmissionSink<br/>优先级入队并唤醒"]
    submission --> request_queues["Request queues<br/>按优先级分组"]
    request_queues --> requests["PriorityStream / DispatchRequestStream<br/>选择请求；保留 pending"]
    requests --> dispatcher["BasicDispatcher / BasicDriver<br/>Context driver 或 worker thread"]
    dispatcher --> route["DispatchRequestSink<br/>目标路由 + shared 目标选择"]
    route --> execution_submission["SubmissionSink<br/>优先级入队并唤醒"]
    execution_submission --> shared["Shared execution queues"]
    execution_submission --> local["Port local execution queues"]
    shared --> shared_stream["PriorityStream / WorkStealingStream<br/>本地优先；空时窃取 shared queues"]
    local --> local_stream["Local executable stream<br/>只读取该 port 的 queues"]
    shared_stream --> shared_drive["无参 getExecutionPort() / BasicWorker<br/>owner 或 worker thread"]
    local_stream --> local_drive["getExecutionPort(token)<br/>调用该 port 的线程；不窃取"]
    shared_drive --> executor["BasicExecutor / BasicDriver"]
    local_drive --> executor
    executor --> sink["ExecuteExecutableSink + CompletionTracker"]
    sink --> completion["用户任务 / receiver<br/>set_value / set_error / set_stopped"]
```

Owner 与 worker 的 `DispatchingExecutor` 先选择并驱动 dispatcher，再尝试执行 shared 工作。Token local ExecutionPort 只执行自身队列。`executeOne()` 返回 ExecutionReport，`waitAndExecuteOne()` 等待唤醒再尝试执行。完成会更新 outstanding 并通知等待者。

`ExecutionResult` 是独立的枚举类，具有 `eIdle / eSucceeded / eStopped / eFailed` 四种互斥结果。`ExecutionReport` 默认表示 idle；静态接口 `make_from_succeeded / make_from_stopped / make_from_error_code` 创建其余报告，字段由类封装。非空 `ec` 只对应 failed，空 `ec` 传入 `make_from_error_code` 时对应 succeeded。`isSucceeded / isStopped / isFailed` 查询状态，隐式 bool 仅在 succeeded 时为 true；dispatch-only 也可 succeeded，实际执行数单独由 `getExecutedCount()` 返回。

Sink 拒收时，driver 先将记录 `requeue`：`no_buffer_space` 留待重试，其他错误进入 report；直接 submit 的拒收错误交回调用方。各队列、stream、sink、policy 通过组件组合替换，Context 在 `create()` 时用显式 memory resource 预留固定存储。

优雅关闭由 Context driver thread 推进：

```mermaid
flowchart TB
    close["closeSubmissions()"]
    close --> stop["Service.requestStop()"]
    stop --> services["继续驱动事件和就绪服务<br/>等待全部 notifyStopped()"]
    services --> gate["关闭 admission gate"]
    gate --> work["继续 dispatch / execute<br/>完成已接纳工作"]
    work --> join["等待 worker 退出并 join"]
    join --> done["关闭完成；run() 返回"]
```

快速停止由 Context driver thread 推进：

```mermaid
flowchart TB
    stop["requestStopProcessing()"]
    stop --> gate["立即关闭 execution gate<br/>不再启动用户任务"]
    gate --> services["Service.requestStop()<br/>继续事件驱动直到 notifyStopped()"]
    services --> admission["关闭 admission gate"]
    admission --> quiescent["等待运行中回调返回<br/>join workers；等待执行静止"]
    quiescent --> cancel["pullAll 各级剩余记录<br/>CancelExecutableSink + CompletionTracker"]
    cancel --> completion["每个剩余 operation<br/>恰好一次 set_stopped"]
    completion --> done["关闭完成；run() 返回"]
```

当前已有 sender/receiver completion 路径，尚未接入 coroutine promise 或 `co_await`。未来 coroutine 可在 sender/receiver 边界适配：挂起时提交 operation，恢复时沿 scheduler 选定的 execution 路径完成；不改变队列、service 和关闭协议。
