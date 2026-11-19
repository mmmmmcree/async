# Async Core

[简体中文](../zh/core-architecture.md)

## Core Concepts and Composition

<!-- diagram-notes: components -->

### Component Responsibilities and Interface Boundaries

Concepts in the diagram express structural interface constraints. Class names identify the current implementations. Arrows show composition or dependencies and carry no inheritance or runtime ordering semantics.

```mermaid
flowchart TB
    %% id: components
    accTitle: Core Components and Dependencies
    context["Context: resource and lifetime boundary<br/>BasicContext"]
    context -->|configured by component type| components["context_components_c<br/>Binds the complete component set<br/>DefaultContextComponents (BasicContext.h)"]
    components --> stages["Peer processing stages<br/>request_dispatcher_c / work_executor_c<br/>BasicDispatcher / BasicExecutor"]
    stages -->|reuses single-step transfer| driver["Driver: connects input to receiver<br/>driver_c: driveOne → std::error_code<br/>BasicDriver / utils::drive_one"]
    driver -->|borrows input| stream["Stream: composable record source<br/>stream_c: pullOne / isEmpty<br/>DispatchRequestStream / WorkStealingStream"]
    driver -->|composes receivers| sink["Sink: composable record receiver<br/>sink_c: receive<br/>SubmissionSink / DispatchRequestSink"]
    stream -->|composes one or more| queue["Queue: record storage and retrieval<br/>producer_queue_c / consumer_queue_c<br/>Queue / Deque"]
    sink -->|enqueue sinks depend on| queue
    stream -->|selects a record source| policy["Policy: candidate selection and ordering<br/>selection_policy_c<br/>RoundRobin / WeightedPriority / utils::NoSelectionPolicy"]
    sink -->|routing sinks select a target| policy
    sink -->|terminal sinks invoke| executable["Executable: runs or cancels work<br/>executable_c / cancellable_executable_c<br/>ExecuteExecutableSink / CancelExecutableSink<br/>→ AnyExecutableRef"]
```

<!-- docs-builder:refs -->
<details>
<summary>Related source</summary>

- [`AnyExecutableRef`](../../../include/async/core/executable/AnyExecutableRef.h)
- [`BasicContext / DefaultContextComponents`](../../../include/async/core/context/BasicContext.h)
- [`BasicDispatcher`](../../../include/async/core/dispatcher/BasicDispatcher.h)
- [`BasicDriver`](../../../include/async/core/driver/BasicDriver.h)
- [`BasicExecutor`](../../../include/async/core/executor/BasicExecutor.h)
- [`CancelExecutableSink`](../../../include/async/core/sink/CancelExecutableSink.h)
- [`context_components_c`](../../../include/async/core/context/declarations/concepts.h)
- [`Deque`](../../../include/async/core/queue/non_intrusive/spmc/Deque.h)
- [`DispatchRequestSink`](../../../include/async/core/dispatcher/DispatchRequestSink.h)
- [`DispatchRequestStream`](../../../include/async/core/stream/DispatchRequestStream.h)
- [`driver_c`](../../../include/async/core/driver/declarations/concepts.h)
- [`executable_c / cancellable_executable_c`](../../../include/async/core/executable/declarations/concepts.h)
- [`ExecuteExecutableSink`](../../../include/async/core/sink/ExecuteExecutableSink.h)
- [`producer_queue_c / consumer_queue_c`](../../../include/async/core/queue/declarations/concepts.h)
- [`Queue`](../../../include/async/core/queue/intrusive/Queue.h)
- [`request_dispatcher_c`](../../../include/async/core/dispatcher/declarations/concepts.h)
- [`RoundRobin`](../../../include/async/core/policy/RoundRobin.h)
- [`selection_policy_c / utils::NoSelectionPolicy`](../../../include/async/core/policy/declarations/concepts.h)
- [`sink_c / rejecting_sink_c`](../../../include/async/core/sink/declarations/concepts.h)
- [`stream_c`](../../../include/async/core/stream/declarations/concepts.h)
- [`SubmissionSink`](../../../include/async/core/sink/SubmissionSink.h)
- [`utils::drive_one`](../../../include/async/core/driver/utils/drive_one.h)
- [`WeightedPriority`](../../../include/async/core/policy/WeightedPriority.h)
- [`work_executor_c`](../../../include/async/core/scheduling/declarations/concepts.h)
- [`WorkStealingStream`](../../../include/async/core/stream/WorkStealingStream.h)

</details>
<!-- /docs-builder:refs -->

A Stream composes queues or other streams and encapsulates selection and behaviors such as retaining a pending record. A Sink handles reception, routing, or terminal processing and can wrap another sink. `BasicDriver` performs one `pullOne → receive` step and returns `std::error_code`. An empty code means the step made progress. `errc::no_work` means no record was available. Services and event pumps use the same result convention. The dispatcher and executor convert codes into `DispatchReport` and `ExecutionReport`, respectively.

`BasicContext<Components>` configures the pipeline, event pump, and thread handles through one component set. `context_components_c<Components>` checks the protocols and construction requirements for `event_pump_t` and `thread_handle_t`. It also checks the assembled component factories for compatible constructors and record types, queue concurrency capabilities, stream retry and drain capabilities, and sink and execution report protocols. The notifier is derived from the configured event pump and participates in the checks. The default component set is defined in the `BasicContext` header.

`utils::NoSelectionPolicy` lets `selected` preserve candidate order, so a priority stream can take work in priority-band order. The default Context uses `WeightedPriority<4, 2, 1>`. The weights determine how often each band is tried first, and each attempt still checks the other candidates. When the weight count and candidate count differ, selection falls back to round-robin.

Streams and sinks share the composition model of execution senders and receivers and have their own protocols. A stream is a synchronous, non-blocking record source. A sink is a synchronous reception boundary. Their interfaces alone do not satisfy `sender_c` or `receiver_c`. `SchedulerHandle` integrates with execution through `ScheduleSender` and `ScheduleOperation`. Completion reaches the receiver through `set_value`, `set_error`, or `set_stopped`.

<!-- diagram-notes: transfer -->

### Acceptance, Retry, and Cleanup

Components provide the capabilities required by their use cases:

```mermaid
flowchart TB
    %% id: transfer
    accTitle: Stream / Sink Transfer Contract
    transfer["Single-step transfer contract<br/>matched_c&lt;Stream, Sink&gt;<br/>Both sides share the same value_t"]
    transfer -->|input| stream["stream_c<br/>pullOne → value_t pointer or null"]
    transfer -->|reception result| sink["sink_c<br/>receive → void or error_code"]
    sink -->|rejecting_sink_c returns error_code| retry["retry_stream_c<br/>Requeue rejected records to keep them in the pipeline"]
    stream -->|optional retry capability| retry
    stream -->|optional shutdown cleanup| drain["drainable_stream_c<br/>pullAny bypasses the selection policy<br/>DispatchRequestStream / PriorityStream"]
    sink -->|returns void| accepted["Guaranteed acceptance<br/>ExecuteExecutableSink / CancelExecutableSink"]
    retry --> recover["no_buffer_space: retain and retry<br/>Other nonempty ec: retain and report"]
    drain --> cleanup["Cancel remaining records after quiescence<br/>CancelExecutableSink → executable.cancel"]
```

<!-- docs-builder:refs -->
<details>
<summary>Related source</summary>

- [`BasicContext / DefaultContextComponents`](../../../include/async/core/context/BasicContext.h)
- [`BasicDispatcher`](../../../include/async/core/dispatcher/BasicDispatcher.h)
- [`BasicExecutor`](../../../include/async/core/executor/BasicExecutor.h)
- [`CancelExecutableSink`](../../../include/async/core/sink/CancelExecutableSink.h)
- [`DispatchRequestStream`](../../../include/async/core/stream/DispatchRequestStream.h)
- [`drainable_stream_c`](../../../include/async/core/stream/declarations/concepts.h)
- [`ExecuteExecutableSink`](../../../include/async/core/sink/ExecuteExecutableSink.h)
- [`matched_c`](../../../include/async/core/driver/declarations/concepts.h)
- [`PriorityStream`](../../../include/async/core/stream/PriorityStream.h)
- [`retry_stream_c`](../../../include/async/core/stream/declarations/concepts.h)
- [`sink_c / rejecting_sink_c`](../../../include/async/core/sink/declarations/concepts.h)
- [`stream_c`](../../../include/async/core/stream/declarations/concepts.h)
- [`utils::drive_one`](../../../include/async/core/driver/utils/drive_one.h)

</details>
<!-- /docs-builder:refs -->

`std::error_code` records the reception outcome. An empty value means acceptance and a nonempty value means rejection. Drivers require `retry_stream_c` when the sink can reject records. `drainable_stream_c` supports shutdown cleanup as an optional stream capability.

<!-- diagram-notes: runtime -->

### Driver Threads, Waiting, and Coordination

The runtime host determines when and on which thread to advance these components. Services and event backends reuse the single-step driver protocol and can implement their internals independently of streams and sinks:

```mermaid
flowchart TB
    %% id: runtime
    accTitle: Runtime Host and Event Pump
    context["Context: composition and shutdown<br/>BasicContext"]
    context -->|exposes entry points| ports["SubmissionPort / ExecutionPort<br/>Get a SchedulerHandle / advance execution"]
    context -->|owns thread lifetimes| workers["BasicWorker / WorkerPool<br/>Wait, wake, and repeat a step"]
    ports -->|owner execution entry point| executor["DispatchingExecutor<br/>dispatchOne, then executeOne"]
    workers -->|drives| executor
    workers -->|stop, completion, and result codes| coordinator["worker_coordinator_c<br/>ContextCoordinator"]
    coordinator -->|reuses the wait protocol| waiter["notify::signal_waiter_c<br/>EventPumpSignal"]
    context -->|owns and drives| host["ServiceHost + EventPump<br/>Ready services, timers, and shared waiting"]
    host -->|non-owning registration| service["service_c: specialized driver_c<br/>start / driveOne / requestStop<br/>AnyServiceRef / sandbox OneShotTimer"]
    host -->|event pump protocol| pump["notify::event_pump_c: specialized driver_c<br/>driveOne / waitUntil / wake<br/>SemaphoreEvent / custom platform pump"]
    service -->|ready, timer, and stop notifications| handle["ServiceHandle<br/>notifyReady / setWakeTime / notifyStopped"]
```

<!-- docs-builder:refs -->
<details>
<summary>Related source</summary>

- [`AnyServiceRef`](../../../include/async/core/service/AnyServiceRef.h)
- [`BasicContext / DefaultContextComponents`](../../../include/async/core/context/BasicContext.h)
- [`BasicDispatcher`](../../../include/async/core/dispatcher/BasicDispatcher.h)
- [`BasicExecutor`](../../../include/async/core/executor/BasicExecutor.h)
- [`BasicWorker`](../../../include/async/core/worker/BasicWorker.h)
- [`ContextCoordinator / ExecutionGuard`](../../../include/async/core/context/details/ContextCoordinator.h)
- [`DispatchingExecutor`](../../../include/async/core/executor/DispatchingExecutor.h)
- [`ExecutionPort`](../../../include/async/core/context/ExecutionPort.h)
- [`notify::event_pump_c`](../../../include/async/core/notify/declarations/concepts.h)
- [`notify::EventPumpSignal`](../../../include/async/core/notify/EventPumpSignal.h)
- [`notify::SemaphoreEvent`](../../../include/async/core/notify/SemaphoreEvent.h)
- [`notify::signal_waiter_c`](../../../include/async/core/notify/declarations/concepts.h)
- [`OneShotTimer`](../../../tests/sandbox/sandbox.cpp)
- [`SchedulerHandle`](../../../include/async/core/scheduling/SchedulerHandle.h)
- [`service_c`](../../../include/async/core/service/declarations/concepts.h)
- [`ServiceHandle`](../../../include/async/core/service/ServiceHandle.h)
- [`ServiceHost`](../../../include/async/core/service/ServiceHost.h)
- [`SubmissionPort`](../../../include/async/core/context/SubmissionPort.h)
- [`worker_coordinator_c`](../../../include/async/core/worker/declarations/concepts.h)
- [`WorkerPool`](../../../include/async/core/worker/WorkerPool.h)

</details>
<!-- /docs-builder:refs -->

`getExecutionPort(token)` executes only the port's local queues and bypasses `DispatchingExecutor`. `DispatchingExecutor` combines dispatch and execution and communicates results through reports. Worker and Context collect error codes in the coordinator. `worker_coordinator_c` belongs to the worker module. `ContextCoordinator` lives in `context/details`.

`ExecutionPort` owns its wait and wake loop. `BasicContext` exposes separate internal interfaces for driver-thread execution and local execution. `run()`, `shutdown()`, and cleanup after service startup fails share the shutdown driver loop. Context records graceful shutdown requests, while the coordinator's execution gate controls fast stop. An empty `ec` does not trigger a stop.

Workers check the stop request before every step. Joining their threads guarantees quiescence after exit. `worker_coordinator_c` uses `notify::signal_waiter_c` to constrain `observe` and `wait` and requires no execution-count interface. Context uses its own `ExecutionGuard` to wait for in-flight calls from external local execution ports.

`BasicWorker::start()` and `WorkerPool::start()` are `noexcept` and return `std::error_code`. They translate thread start exceptions at that boundary. When the pool partially starts, it requests stop, joins the started threads, and returns the original error code. `ServiceHost` borrows its services. Context owns the default event pump and fixed registration storage. Platform event capabilities come from the concrete pump.

## Runtime Flows

<!-- diagram-notes: service-activation -->

### Readiness Notifications and Timers

The Context driver thread serializes service lifecycle calls and `driveOne()`. Callers can use `ServiceHandle` across threads.

```mermaid
flowchart TB
    %% id: service-activation
    accTitle: Service Activation and Event Conversion
    source["OS event callback / external event / service timer"]
    source --> handle["ServiceHandle<br/>notifyReady / setWakeTime"]
    handle --> registration["ServiceHost<br/>Intrusive Queue coalesces ready entries<br/>IndexedHeap updates the timer slot"]
    registration --> wake["EventPump.wake()"]
    wake --> wait["Context driver thread<br/>waitUntil the next deadline; event_pump.driveOne()"]
    wait --> ready["ServiceHost<br/>Move expired timers to ready; take one ready service"]
    ready --> service["Service.driveOne()<br/>Read events and produce submissions"]
    service --> submit["SubmissionPort / SchedulerHandle"]
```

<!-- docs-builder:refs -->
<details>
<summary>Related source</summary>

- [`AnyServiceRef`](../../../include/async/core/service/AnyServiceRef.h)
- [`BasicContext / DefaultContextComponents`](../../../include/async/core/context/BasicContext.h)
- [`IndexedHeap`](../../../include/async/core/details/containers/IndexedHeap.h)
- [`notify::event_pump_c`](../../../include/async/core/notify/declarations/concepts.h)
- [`notify::SemaphoreEvent`](../../../include/async/core/notify/SemaphoreEvent.h)
- [`OneShotTimer`](../../../tests/sandbox/sandbox.cpp)
- [`Queue`](../../../include/async/core/queue/intrusive/Queue.h)
- [`SchedulerHandle`](../../../include/async/core/scheduling/SchedulerHandle.h)
- [`service_c`](../../../include/async/core/service/declarations/concepts.h)
- [`ServiceHandle`](../../../include/async/core/service/ServiceHandle.h)
- [`ServiceHost`](../../../include/async/core/service/ServiceHost.h)
- [`SubmissionPort`](../../../include/async/core/context/SubmissionPort.h)

</details>
<!-- /docs-builder:refs -->

`enableServiceHandling()` calls `Service.start(handle)`. `run()` enables service handling automatically. The caller owns the service and borrows the event pump through `getEventPump()`. The default `notify::SemaphoreEvent` supplies timed waits and cross-thread wakes. A custom pump provides platform OS event registration and callbacks. Idle services receive no per-service polling.

`ServiceHost::get_storage_requirements(capacity)` returns a public `StorageRequirements` with size and alignment. `create(capacity, memory_resource&)` prepares fixed registration entries and a timer heap. The budget query and allocation share an internal layout definition. Context aggregates the component requirements, while components retain their layout templates. The ready queue links the registration entries directly and coalesces repeat notifications through their link state. `IndexedHeap` tracks timer positions and supports O(log N) updates and removals. The host mutex coordinates state changes. The host releases it before calling services or waking the event pump.

<!-- diagram-notes: submission-execution -->

### Enqueueing, Routing, and Completion

Solid arrows in the submission and execution pipeline show the direction of record flow. Both records and executables borrow storage from the operation.

```mermaid
flowchart TB
    %% id: submission-execution
    accTitle: Submission and Execution Pipeline
    port["SubmissionPort<br/>Caller or service driver thread"]
    port --> scheduler["SchedulerHandle<br/>Target + inherited or overridden priority"]
    scheduler --> operation["ScheduleOperation<br/>SchedulingRecord + executable"]
    operation --> gate["SubmissionGate"]
    gate --> submission["SubmissionSink<br/>Enqueue by priority and wake"]
    submission --> request_queues["Request queues<br/>Grouped by priority"]
    request_queues --> requests["PriorityStream / DispatchRequestStream<br/>Select a request; retain pending records"]
    requests --> dispatcher["BasicDispatcher / BasicDriver<br/>Context driver or worker thread"]
    dispatcher --> route["DispatchRequestSink<br/>Route and select the shared target"]
    route --> execution_submission["SubmissionSink<br/>Enqueue by priority and wake"]
    execution_submission --> shared["Shared execution queues"]
    execution_submission --> local["Port local execution queues"]
    shared --> shared_stream["PriorityStream / WorkStealingStream<br/>Try local first, then steal from shared queues"]
    local --> local_stream["Local executable stream<br/>Read only this port's queues"]
    shared_stream --> shared_drive["getExecutionPort() / BasicWorker<br/>Owner or worker thread"]
    local_stream --> local_drive["getExecutionPort(token)<br/>Port caller thread; no stealing"]
    shared_drive --> executor["BasicExecutor / BasicDriver"]
    local_drive --> executor
    executor --> sink["ExecuteExecutableSink + CompletionTracker"]
    sink --> completion["User task / receiver<br/>set_value / set_error / set_stopped"]
```

<!-- docs-builder:refs -->
<details>
<summary>Related source</summary>

- [`BasicContext / DefaultContextComponents`](../../../include/async/core/context/BasicContext.h)
- [`BasicDispatcher`](../../../include/async/core/dispatcher/BasicDispatcher.h)
- [`BasicDriver`](../../../include/async/core/driver/BasicDriver.h)
- [`BasicExecutor`](../../../include/async/core/executor/BasicExecutor.h)
- [`BasicWorker`](../../../include/async/core/worker/BasicWorker.h)
- [`CompletionTracker`](../../../include/async/core/context/details/ContextTracking.h)
- [`ContextResources`](../../../include/async/core/context/details/ContextResources.h)
- [`DispatchRequestSink`](../../../include/async/core/dispatcher/DispatchRequestSink.h)
- [`DispatchRequestStream`](../../../include/async/core/stream/DispatchRequestStream.h)
- [`ExecuteExecutableSink`](../../../include/async/core/sink/ExecuteExecutableSink.h)
- [`ExecutionPort`](../../../include/async/core/context/ExecutionPort.h)
- [`PriorityStream`](../../../include/async/core/stream/PriorityStream.h)
- [`queue::intrusive::mpmc::Queue`](../../../include/async/core/queue/intrusive/mpmc/Queue.h)
- [`receiver_c`](../../../include/async/protocols/receiver.h)
- [`RoundRobin`](../../../include/async/core/policy/RoundRobin.h)
- [`ScheduleOperation`](../../../include/async/core/scheduling/details/ScheduleOperation.h)
- [`SchedulerHandle`](../../../include/async/core/scheduling/SchedulerHandle.h)
- [`SchedulingRecord`](../../../include/async/core/scheduling/SchedulingRecord.h)
- [`SubmissionGate`](../../../include/async/core/context/details/ContextTracking.h)
- [`SubmissionPort`](../../../include/async/core/context/SubmissionPort.h)
- [`SubmissionSink`](../../../include/async/core/sink/SubmissionSink.h)
- [`WorkStealingStream`](../../../include/async/core/stream/WorkStealingStream.h)

</details>
<!-- /docs-builder:refs -->

The owner's and workers' `DispatchingExecutor` selects and drives a dispatcher before attempting shared work. A token-local `ExecutionPort` only executes from its own queues. `executeOne()` returns an `ExecutionReport`. `waitAndExecuteOne()` waits for a wake before attempting execution. Completion updates the outstanding count and notifies waiters.

`ExecutionResult` is an enum class with four mutually exclusive results: `eIdle`, `eSucceeded`, `eStopped`, and `eFailed`. A default `ExecutionReport` is idle. The static methods `make_from_succeeded`, `make_from_stopped`, and `make_from_error_code` create the other reports, and the class encapsulates their fields. A nonempty `ec` means failed, while an empty `ec` passed to `make_from_error_code` means succeeded. Use `isSucceeded`, `isStopped`, and `isFailed` to query the result. Implicit conversion to `bool` returns true only for succeeded. A dispatch-only step can succeed, and `getExecutedCount()` separately reports how many tasks actually ran.

When a sink rejects a record, the driver requeues it first. `no_buffer_space` leaves it pending for retry, while other errors appear in the report. Direct submission returns rejections to the caller. Queues, streams, sinks, and policies are replaceable through component composition. Context reserves fixed storage from an explicit memory resource in `create()`.

<!-- diagram-notes: graceful-shutdown -->

### Closing Submissions and Draining Work

The Context driver thread advances graceful shutdown:

```mermaid
flowchart TB
    %% id: graceful-shutdown
    accTitle: Graceful Shutdown
    close["closeSubmissions()"]
    close --> stop["Service.requestStop()"]
    stop --> services["Keep driving events and ready services<br/>Wait for every notifyStopped()"]
    services --> gate["Close the admission gate"]
    gate --> work["Continue dispatch and execution<br/>Complete admitted work"]
    work --> join["Wait for workers to exit and join them"]
    join --> done["Shutdown complete; run() returns"]
```

<!-- docs-builder:refs -->
<details>
<summary>Related source</summary>

- [`BasicContext / DefaultContextComponents`](../../../include/async/core/context/BasicContext.h)
- [`BasicWorker`](../../../include/async/core/worker/BasicWorker.h)
- [`CompletionTracker`](../../../include/async/core/context/details/ContextTracking.h)
- [`ContextCoordinator / ExecutionGuard`](../../../include/async/core/context/details/ContextCoordinator.h)
- [`DispatchingExecutor`](../../../include/async/core/executor/DispatchingExecutor.h)
- [`notify::event_pump_c`](../../../include/async/core/notify/declarations/concepts.h)
- [`service_c`](../../../include/async/core/service/declarations/concepts.h)
- [`ServiceHandle`](../../../include/async/core/service/ServiceHandle.h)
- [`ServiceHost`](../../../include/async/core/service/ServiceHost.h)
- [`SubmissionGate`](../../../include/async/core/context/details/ContextTracking.h)
- [`WorkerPool`](../../../include/async/core/worker/WorkerPool.h)

</details>
<!-- /docs-builder:refs -->

<!-- diagram-notes: immediate-stop -->

### Execution Gate and Pending Work Cancellation

The Context driver thread advances fast stop:

```mermaid
flowchart TB
    %% id: immediate-stop
    accTitle: Fast Stop
    stop["requestStopProcessing()"]
    stop --> gate["Close the execution gate immediately<br/>Prevent new user tasks from starting"]
    gate --> services["Service.requestStop()<br/>Drive events until notifyStopped()"]
    services --> admission["Close the admission gate"]
    admission --> quiescent["Wait for in-flight callbacks to return<br/>Join workers and wait for execution to quiesce"]
    quiescent --> cancel["pullAll remaining records at each stage<br/>CancelExecutableSink + CompletionTracker"]
    cancel --> completion["Each remaining operation<br/>Receives set_stopped exactly once"]
    completion --> done["Shutdown complete; run() returns"]
```

<!-- docs-builder:refs -->
<details>
<summary>Related source</summary>

- [`ActivityGate`](../../../include/async/core/details/ActivityGate.h)
- [`BasicContext / DefaultContextComponents`](../../../include/async/core/context/BasicContext.h)
- [`CancelExecutableSink`](../../../include/async/core/sink/CancelExecutableSink.h)
- [`CompletionTracker`](../../../include/async/core/context/details/ContextTracking.h)
- [`ContextCoordinator / ExecutionGuard`](../../../include/async/core/context/details/ContextCoordinator.h)
- [`notify::event_pump_c`](../../../include/async/core/notify/declarations/concepts.h)
- [`receiver_c`](../../../include/async/protocols/receiver.h)
- [`ScheduleOperation`](../../../include/async/core/scheduling/details/ScheduleOperation.h)
- [`ServiceHandle`](../../../include/async/core/service/ServiceHandle.h)
- [`ServiceHost`](../../../include/async/core/service/ServiceHost.h)
- [`SubmissionGate`](../../../include/async/core/context/details/ContextTracking.h)
- [`utils::pull_all`](../../../include/async/core/stream/utils/pull_all.h)
- [`WorkerPool`](../../../include/async/core/worker/WorkerPool.h)

</details>
<!-- /docs-builder:refs -->

Sender and receiver completion is implemented. Coroutine promises and `co_await` are future work. A coroutine adapter could submit an operation on suspension and resume along the scheduler-selected execution path. Queue, service, and shutdown protocols would carry over to that adapter.
