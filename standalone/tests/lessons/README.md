# Coroutine lessons

These lessons assume ordinary C++ knowledge: functions, classes, references, destructors and exceptions. No coroutine, sender/receiver or scheduler background is required. Each `.cpp` contains one GoogleTest lesson, its teaching types and their definitions. Read the `TEST` first, then follow the functions it calls. `EXPECT_*` checks a prediction; `ASSERT_*` stops the lesson when a prerequisite fails.

The complete course has **31 planned lessons in seven stages**. Lessons 01–04 are implemented and complete Stage I; Lessons 05–31 are the implementation roadmap below. The goal is to explain the actual standalone backend, from a coroutine's first suspension to scheduling, composition, cancellation and frame reclamation.

The learning sequence is:

```text
coroutine language mechanics
  -> callbacks and the sender/receiver protocol
  -> queues, schedulers and threads
  -> the real Awaitable and its sender bridge
  -> execution graphs and local error handling
  -> cancellation, timers and signals
  -> allocation, reclamation and a complete application trace
```

Each stage has an exit question. Proceed when you can explain its answer from the experiment and then locate that mechanism in the real implementation. The [complete curriculum](#complete-curriculum) describes the prerequisites and experiment for every lesson.

The first four files introduce small teaching types in observable steps. Lesson 03 includes a brief production preview with standalone `Awaitable`. This preview is optional on a first reading; its driver and implementation are opened in Lessons 13–18.

| Lesson | Question | File |
| --- | --- | --- |
| 01 | What remains alive when a function suspends? | [Frame lifetime and manual resume](lesson01_frame_lifetime.cpp) |
| 02 | What exactly does `co_await` call? | [The awaiter protocol](lesson02_await_protocol.cpp) |
| 03 | How does a child return a value or exception to its parent? | [Nested coroutines and continuations](lesson03_nested_coroutines.cpp) |
| 04 | Who owns the frame and its data while it is suspended? | [Ownership across suspension](lesson04_ownership.cpp) |

## Build, run and debug

After configuring a development preset, run from the async project root:

```powershell
cmake --build --preset header-msvc-debug --target async_standalone_lessons
```

This target builds and runs all four lessons. Substitute `header-clang-debug` or another configured build preset as needed. To run already-built lessons through CTest:

```powershell
ctest --preset header-msvc-debug -L standalone_lessons --output-on-failure
```

In VS Code, build and debug `async_standalone_lessons_tests`. Select a single lesson with an executable argument such as:

```text
--gtest_filter=CoroutineLessons.Lesson02AwaitReadySuspendResume
```

The `standalone.` prefix belongs to CTest names; it is absent from GoogleTest filters. Breakpoints in `await_suspend`, `await_resume`, `return_value` and `final_suspend` expose the execution order. Prefer a Debug build and use the explicit counters/events as a reference when debugger stepping crosses compiler-generated code.

The targets appear under `async/standalone/tests/lessons`. `LCF_ASYNC_STANDALONE_BUILD_LESSONS` controls their availability while standalone development tests are enabled. Both main-build and deferred-build modes are supported. Lessons stay outside installed headers, the module interface and CMake target exports. GoogleTest is a development dependency.

## Lesson 01: separate four moments

Start with `two_steps()`. Its body has two observable steps separated by `co_await std::suspend_always {}`. The test drives those steps manually:

| Operation | Observable state |
| --- | --- |
| Call `two_steps(lifecycle)` | A return object exists; the body has not entered. |
| First `work.resume()` | The body constructs `LocalLifetime` and suspends. The local remains alive. |
| Second `work.resume()` | The body continues, calls `return_void`, destroys its local, and reaches final suspension. |
| Leave the scope of `work` | Its destructor calls `destroy`; the promise is destroyed with the frame. |

Three objects have different roles:

- The **coroutine frame** retains execution state, including the promise and locals that must survive suspension. Its storage can sometimes be optimized away; a heap allocation is not required for every call.
- The **promise** supplies hooks for compiler-generated calls. The name `ManualCoroutine::promise_type` identifies it. This type is separate from `std::promise`.
- The **return object**, `ManualCoroutine`, owns a frame handle in this lesson. A `std::coroutine_handle` itself does not automatically destroy anything when copied or destroyed.

`get_return_object()` produces the object returned by the initial call. `initial_suspend()` makes this coroutine lazy. Later, `co_return` calls `return_void()` and leads to `final_suspend()`. These hooks follow the language's [coroutine definition protocol](https://eel.is/c++draft/dcl.fct.def.coroutine).

Here final suspension retains the frame until the owner destroys it. `done()` becoming true and frame destruction therefore happen at different moments. The test resumes only unfinished, suspended work; resuming again after final suspension would violate the handle's preconditions.

The test checks the executing thread before and after the explicit suspension. Its caller performs both resumptions on the same thread. No background worker exists in this lesson.

**Checkpoint:** predict `m_live_locals` after each table row. Set a breakpoint in the promise destructor and observe how late it runs.

## Lesson 02: open the await expression

`await_answer()` waits on `ProbeAwaiter`. Its owner repeats Lesson 01's lazy coroutine without the lifecycle counters. The new mechanism is in three methods:

1. `await_ready()` decides whether suspension can be skipped.
2. When needed, `await_suspend(handle)` receives a handle to the awaiting coroutine. This awaiter returns a `bool` to decide whether it stays suspended.
3. `await_resume()` supplies the value of the `co_await` expression when execution continues.

The test observes three paths through the [await-expression protocol](https://eel.is/c++draft/expr.await):

| `await_ready()` | `await_suspend()` | State after the caller's first `resume()` |
| --- | --- | --- |
| `true` | Skipped | `await_resume()` returned 21; the coroutine stored 42 and completed. |
| `false` | Returns `false` | Execution continued immediately; the same result is available. |
| `false` | Returns `true` | The coroutine remains suspended; no answer is available yet. |

For the third input, the test takes the saved handle and explicitly resumes it. Only then does `await_resume()` supply 21 and allow the assignment of 42. `work` owns the frame; the saved handle merely refers to it.

An awaiter's implementation determines who resumes a suspended coroutine and when. This test has neither a queue nor a worker. Suspension preserves coroutine state while allowing the current thread to return to its caller.

**Checkpoint:** find the input that calls `await_resume()` without calling `await_suspend()`. Find the input that completes with one explicit `resume()` despite `await_ready()` being false.

## Lesson 03: follow two frames

Read `parent()` and `child()` before `IntTask`. The parent creates a lazy child and awaits it. The child pauses once so the test can inspect their intermediate state.

`IntTask::operator co_await() &&` transfers ownership of the child frame into an `Awaiter`. The parent suspends. Its awaiter's `await_suspend(parent)` saves the parent handle as the child's **continuation** and returns the child handle. A handle-valued return transfers execution to that coroutine; this is commonly called symmetric transfer.

The successful path is:

```text
test: work.start()
  parent: before co_await
  parent: child created
  child: before pause
test: control has returned; parent is unfinished

test: resume the saved child handle
  child: co_return 21
    promise.return_value(21) stores the result
    final_suspend's await_suspend returns the parent handle
  parent: its child's await_resume reads 21
  parent: after co_await
    co_return 42 stores the parent's result
test: work is done; getResult() reads 42
```

The test never explicitly resumes the parent a second time. Returning the continuation from the child's final awaiter accounts for that progress. The root's continuation is `std::noop_coroutine()`, a special no-op coroutine handle.

The child's frame remains available while the parent's `await_resume()` reads the stored result. The awaiter's destructor then destroys the child frame. By the time the external `waiting.resume()` returns, its borrowed child handle is dangling; the test never uses it again.

The throwing path uses the same completion mechanism. The child promise captures an `exception_ptr`; final suspension transfers to the parent; the parent's await expression rethrows from the child's `await_resume()`. With no catch in the parent, its promise captures that exception. The test observes it through `getResult()`. Its event log confirms that the parent's code after `co_await` did not execute.

At the bottom, `standalone_parent()` and `standalone_child()` repeat the value-producing nesting with the real library. `sync_wait()` starts and waits for that operation. Step over this optional preview for now: Lesson 13 opens the driver, and Lessons 14–18 open the real coroutine implementation. The body-entry counter confirms that the real `Awaitable` is also lazy.

**Checkpoint:** break in the child's `return_value`, `FinalAwaiter::await_suspend` and the parent's `Awaiter::await_resume`. Predict the next breakpoint before continuing.

## Lesson 04: follow ownership, not just execution

Lesson 03 followed control from parent to child and back. This lesson asks what happens to the same relationship when an owning return object is moved or the work is discarded.

Read the three inputs in the test first:

| Case | Body locals constructed | Code after the awaits | What releases the payload? |
| --- | --- | --- | --- |
| Discard before first resume | None | Never executed | Destroying the unstarted parent's frame releases its by-value parameter. |
| Complete parent and child | Two | Both bodies continue | The completed child's owning awaiter destroys the child frame and its payload parameter. |
| Destroy suspended parent and child | Two | Never executed | Destroying the parent destroys its child-owning awaiter, which destroys the child frame. |

`Payload` counts live owned data. `BodyLocal` counts constructed and destroyed body locals. These counters measure different lifetimes: a coroutine's by-value parameters are already retained when its lazy call returns, while body locals are constructed only when execution reaches their declarations.

The experiment separates three ownership transfers:

1. Calling `parent(...)` moves a `unique_ptr<Payload>` into the parent frame. The lazy body has not entered, but the payload is already retained.
2. `auto owner = std::move(original)` transfers the owning frame handle and clears the source. It does not relocate the frame or run its body. The moved-from object is inspected only through `ownsFrame()`.
3. When the parent awaits `child(...)`, it moves the payload into the child frame. The parent's awaiter now owns that child frame. A continuation handle points back to the parent but does not own it.

The normal path resumes the child and completes both bodies. Its payload and body locals are already gone when the test observes `owner.done()`. The completed root frame itself still exists until the owning return object's destructor runs, as observed in Lesson 01.

The discarded path destroys suspended work without resuming it. Local destructors and parameter destructors run; statements after the await do not. This accounts for a child frame through the parent's awaiter even when neither body reaches `co_return`.

The external `borrowed` integer lives in the test. The coroutine parameters are references to that same object: changing it from 10 to 20 before entry is visible to the child, and changing it to 30 while suspended is visible after resumption. The test verifies identity as well as values. Keeping a reference in a frame does not extend the referred object's lifetime; the test's scopes deliberately keep it alive until every borrowing frame is destroyed.

Before destruction, the test forgets its borrowed child handle. On the normal path, the child frame has already been destroyed when the external `resume()` returns. This model has no outside callbacks that could resume destroyed work; the registration and cancellation lessons later explain that additional coordination.

**Checkpoint:** draw the ownership graph at the child's pause. Mark owning frame handles, the unique payload owner and borrowed references separately. Then cross out the parent owner and explain the resulting destructor chain without executing either body further.

## Connect the teaching types to standalone

Paths below are relative to `standalone/include/async`.

| Mechanism observed | Actual implementation | What the full implementation adds |
| --- | --- | --- |
| Move-only frame owner | [core/coroutine/Awaitable.h](../../include/async/core/coroutine/Awaitable.h) | Sender completion signatures and `connect`. |
| Promise hooks and lazy initial suspension | [AwaitablePromise in details/awaitable.h](../../include/async/core/coroutine/details/awaitable.h) | Allocation hooks and `await_transform` dispatch for tasks, senders and scheduling requests. |
| Store and take a result | [details/ReturnStorage.h](../../include/async/core/coroutine/details/ReturnStorage.h) | Generic values, `void`, move semantics and alignment handling. |
| Await a child and read its outcome | [TaskAwaiter in details/awaitable.h](../../include/async/core/coroutine/details/awaitable.h) | Parent environment, return executor and exception-safe frame destruction. |
| Return to a continuation | [details/state.h](../../include/async/core/coroutine/details/state.h) | Executor-aware completion and notification of the root receiver. |
| Determine whether later resumption is needed | [details/sender_awaiter.h](../../include/async/core/coroutine/details/sender_awaiter.h) | Sender adaptation and coordination when completion races with suspension. |

`IntTask` exposes manual `start`/`getResult` methods for observation. In the library, `AwaitableOperation::start()` drives a root and initializes its environment. Root completion delivers value, error or stopped to a receiver. For nested work, `TaskAwaiter` and `CoroutineState` maintain the parent relationship. A change of executor can introduce a queue boundary; inspect `CoroutineState::finish()` before generalizing this lesson's immediate transfer to every library operation.

Lessons 01–02 and the void ownership model in Lesson 04 terminate on an unexpected body exception to keep their experiments focused. Lesson 03 introduces exception transport. All four lessons use controlled, single-threaded resumptions. Concurrency and cancellation will add ownership and synchronization requirements.

## Complete curriculum

Lessons 01–04 have executable files. All subsequent rows are planned lessons, with deliberately separate learning steps. Source paths in the tables are relative to `standalone/include/async`.

### Stage I — Understand suspended functions, Lessons 01–04

Start with ordinary function calls, RAII and exceptions. Keep execution on one thread. Draw a frame as a box containing a promise, retained locals and a current suspension point; mark which object owns it and which handles merely refer to it.

| Lesson | Prerequisites | Experiment and observable result | Source endpoint |
| --- | --- | --- | --- |
| **01. Frame lifetime** — implemented | Ordinary function calls and destructors | Call, resume twice and destroy. Observe body entry, a retained local, completion and later promise destruction separately. | `core/coroutine/Awaitable.h`, promise hooks in `details/awaitable.h` |
| **02. Awaiter protocol** — implemented | 01 | Compare ready, rejected-suspension and delayed-resumption paths. Count each awaiter hook and predict when the result becomes available. | Awaiter methods in `details/awaitable.h` and `details/sender_awaiter.h` |
| **03. Parent and child** — implemented | 01–02 | Suspend a child, resume it externally, and follow its value or exception back into the parent. Record the continuation transfers. | `TaskAwaiter`, `FinalAwaiter`, `CoroutineState` |
| **04. Ownership across suspension** — implemented | 01–03, move semantics | Move an owning return object, pass data into suspended work, and destroy unfinished nested work. Use lifetime counters to identify the owner of every frame and the lifetime required by references. | `Awaitable` moves/destructor, `TaskAwaiter` destruction and its `FrameOwner` |

**Exit question:** when a child completes, who resumes the parent, who reads the result, and who destroys each frame? Explain why a saved handle may already be dangling when its `resume()` call returns.

### Stage II — Build execution from callbacks, Lessons 05–08

Start from the manual resumer in Lesson 02. First put that responsibility into a tiny queue. Then represent work and its completion explicitly. Coroutines and worker threads are unnecessary for the first sender/receiver experiments.

| Lesson | Prerequisites | Experiment and observable result | Source endpoint |
| --- | --- | --- | --- |
| **05. A hand-driven callback queue** | 02, 04 | Enqueue a callback that resumes suspended work. Pump one callback at a time and show that enqueueing alone does not execute it. Track the callback's owner. | `core/scheduling/details/queue.h`, `Callback` and `Context::pollOne` |
| **06. Sender, receiver and operation state** | 04–05 | Write a minimal integer sender and recording receiver. Separate construction, `connect` and `start`; observe that only `start` produces the value. Keep its operation alive until completion. | `core/protocol/operations.h`, `core/protocol/details/utility.h` |
| **07. Completion channels and signatures** | 06, basic templates | Extend the sender to complete with a value, an error or stopped. Observe one terminal outcome per execution; describe these possibilities with completion signatures, including void and multiple values. | Protocol traits and `core/protocol/details/completion.h` |
| **08. An inline pipeline** | 06–07 | Connect `just(21) | then(...)` to a receiver. Distinguish building the graph from running it; count the continuations executed by one `start`. Introduce pipe syntax through this one concrete use. | `execution/factories.h`, `continuations.h`, `details/adaptor.h` |

**Exit question:** how can an entire synchronous pipeline finish inside a single `start()` call? Identify the sender description, the live operation state and the receiver at each edge.

### Stage III — Give execution a place to run, Lessons 09–13

Reuse the callback queue and completion protocol. Introduce the real runtime on one thread before adding a worker. Queue synchronization and explicit gates then make thread handoffs observable without relying on sleeps.

| Lesson | Prerequisites | Experiment and observable result | Source endpoint |
| --- | --- | --- | --- |
| **09. Context and executor** | 05–08 | Post work through a real executor and drive its context manually. Observe queue readiness and distinguish an execution handle from the context that pumps work. | `core/scheduling/Context.h`, `Executor.h`, `details/CallbackExecutor.h`, `operations.h` |
| **10. Scheduler as a sender factory** | 06–09 | Connect/start `schedule(scheduler)` and observe its receiver remain pending until the context runs. Introduce the scheduler query on a receiver environment using this concrete need. | `core/scheduling/Scheduler.h`, `details/scheduled_operation.h`, protocol environment queries |
| **11. Worker threads and work lifetime** | 04, 09–10 | Introduce mutex/condition-variable waiting with a worker running the context. Use gates and thread IDs to identify submission, execution and completion; inspect idle waiting, work accounting, stop and restart separately. | `Context::run/runUntilIdle`, queue wakeups and executor work accounting |
| **12. Cooperative polling budgets** | 08–11 | Queue several callbacks and limit one poll by callback count. Then let one callback advance multiple ready continuations. Compare logical steps, callback count and the points where a time budget is checked. | `PollBudget`, `PollReport`, `Context::poll/pollOne` |
| **13. What sync_wait actually drives** | 06–12 | Compare inline completion with queued completion. Identify the calling thread, the supplied run-loop scheduler, the wait and the final value/error/stopped observation. Revisit Lesson 03's preview. | `execution/sync_wait.h`, `core/scheduling/RunLoop.h`, `core/synchronization/details/CompletionEvent.h` |

**Exit question:** draw a timeline from `start` through enqueue, context polling and receiver completion. Explain which boundaries are budgeted and why an already-running callback is not preempted by the cooperative budget.

### Stage IV — Read the real coroutine bridge, Lessons 14–18

The language mechanics, protocol and runtime are now familiar. Replace the teaching coroutine with standalone `Awaitable` and open one additional mechanism at a time. Use default cancellation and allocation settings initially; their policies are studied later.

| Lesson | Prerequisites | Experiment and observable result | Source endpoint |
| --- | --- | --- | --- |
| **14. Awaitable as both child and root work** | 01–04, 06–07, 13 | Run value, void and move-only results first through nested `co_await`, then as root operations. Match promise hooks and result storage to the teaching model; identify root receiver completion. | `Awaitable.h`, `AwaitablePromise`, `TaskAwaiter`, `AwaitableOperation`, `ReturnStorage.h`, `details/traits.h` |
| **15. Awaiting a sender** | 02, 06–07, 10, 14 | Await a sender in a real coroutine. Follow `await_transform`, `connect`, `start`, the adapter receiver and `await_resume`. Contrast an awaited child with an awaited protocol operation by drawing both paths. | `AwaitablePromise::await_transform`, `SenderAwaiter` |
| **16. Completion during suspension setup** | 11, 15 | Use controlled senders that complete inside `start` or after suspension is established. Introduce atomic phase transitions through their timelines; trace registering/suspended/completed and explain how the handshake selects one continuation path. | `SenderAwaiter::await_suspend/publish/resume` and its atomic phase |
| **17. Environment and continuation routing** | 03, 10, 14–16 | Follow the scheduler environment into nested work and identify the saved return executor. Observe root completion and nested completion as two uses of the same coroutine state. | `CoroutineEnvironment`, `CoroutineState::finish/dispatch`, `TaskAwaiter::await_suspend` |
| **18. Yield and reschedule** | 09–12, 15–17 | Record thread IDs and queue events around `yield` and `reschedule_on`. Follow entry onto the new executor and the child's return to its parent executor. | `core/coroutine/operations.h`, scheduling-request `await_transform` overloads, `CoroutineState` |

**Exit question:** trace a real coroutine from `sync_wait` to `AwaitableOperation::start`, through an awaited sender and a nested child, and back to root completion. Explain when continuation transfer is immediate and when it enters a queue.

### Stage V — Compose independent operations, Lessons 19–24

Reuse a small computation and vary one graph operation per lesson. The learner should inspect both observable behavior and the operation states that keep it alive. Recovery and retry have separate lessons because they create different execution structures.

| Lesson | Prerequisites | Experiment and observable result | Source endpoint |
| --- | --- | --- | --- |
| **19. Scheduling a graph** | 08–11, 17–18 | Apply `starts_on`, `continues_on` and `on` to the same work. Record where the producer and consumer run and what environment the nested operation receives. | `execution/scheduling.h` |
| **20. Value mapping and operation creation** | 08, 14–15, 19 | Compare `then` returning a value with `let_value` returning a new sender. Track construction and lifetime of the nested operation as well as its completion. | `execution/continuations.h`, protocol operation storage |
| **21. Join independent branches** | 07, 11, 19–20 | Gate two `when_all` branches and finish them in reverse order. Observe completion only after both terminate and values delivered in input order; then inspect its stored terminal state. | `execution/when_all.h` |
| **22. Share one execution** | 04, 07, 19–21 | Subscribe two branches to `split(work)`, count one producer invocation, and attach a late consumer. Inspect shared ownership and replayed completion storage. | `execution/split.h`, `CompletionStorage::replay` |
| **23. Local error recovery** | 07, 20–22 | Inject a failure at a known node and recover there with an error continuation or `from_result`. Count which upstream work already ran and which downstream work resumes. | Error continuations in `execution/continuations.h`, `execution/error_handling.h` |
| **24. Retry as new attempts** | 04, 20, 23 | Have a factory fail before succeeding. Observe fresh operation states, attempt counts and the retry limit. Then use synchronous failures to understand the iterative drive loop. | `RetryContext`, `RetryOperation::complete/drive` in `execution/error_handling.h` |

**Exit question:** annotate a graph containing a scheduled producer, a shared result, two branches, a join and local recovery. For each node, identify whether it owns one child operation, several children or shared completion state.

### Stage VI — End pending work safely, Lessons 25–28

Begin with cancellation of a single operation, then study callback registration, and only then add the timer and signal mechanisms that use that registration. Revisit the join/share cases with cancellation after understanding their success paths.

| Lesson | Prerequisites | Experiment and observable result | Source endpoint |
| --- | --- | --- | --- |
| **25. Stop requests and stopped completion** | 07, 11, 15–17, 21–24 | Request stop before start and while work is pending. Observe the terminal channel and skipped coroutine code; then compare cancellation of one shared consumer with cancellation of all consumers. | `core/cancellation/operations.h`, `execution/environment.h`, coroutine stop paths, `split.h` and `when_all.h` |
| **26. Registration and destruction races** | 04, 11, 16, 25 | Register a callback with an already-stopped token and observe that cancellation can run during registration. Use gates to trace completion versus cancellation and receiver-triggered operation destruction. | `core/cancellation/details/StopRegistrationGate.h`, callback ownership and completion paths |
| **27. Timers** | 10–13, 15–16, 25–26 | Schedule a deadline, drive its context, and cancel another pending wait. Trace timer insertion, readiness, removal and exactly one terminal delivery. | `core/timer/operations.h`, `senders.h`, `details/timer.h`, `details/timer_wait.h`, queue timer handling |
| **28. Signals and notifications** | 07, 15–16, 25–27 | Register waiters, emit a signal and observe wakeup. Then control reset/cancellation ordering and track subscriber lifetime through completion. | `core/synchronization/Signal.h`, `details/notification.h`, `notification_state.h`, `CompletionEvent.h` |

**Exit question:** for a pending timer or signal wait, list who owns the operation, who may complete it, how cancellation is registered, and which synchronization step prevents a second completion or access after destruction.

### Stage VII — Account for memory and explain a full run, Lessons 29–31

The mechanisms are already understood. Now examine their storage and optimization policies, preserving the behavior established by the earlier lessons.

| Lesson | Prerequisites | Experiment and observable result | Source endpoint |
| --- | --- | --- | --- |
| **29. Frame allocation and execution allocators** | 01, 04, 14, 17, 26 | Use a counting memory resource for frame creation and another execution environment. Distinguish allocation before a lazy body starts from allocator queries supplied when execution starts; pair allocation with eventual destruction. | `FrameAllocatorScope.h`, promise `operator new/delete`, `execution/environment.h`, `ReturnStorage.h` |
| **30. Frame reuse and thread lifetime** | 11, 26, 29 | Observe repeated same-sized frame allocation and cross-thread reclamation. Trace cache bins, resource provenance and thread-local cache retirement. Use counts and ownership traces before interpreting benchmark timings. | `core/coroutine/details/frame.h`, existing frame-lifetime verification and benchmark probes |
| **31. Capstone: trace the whole backend** | 01–30 | Drive a frame context with a cooperative budget while a worker produces a shared result. Branch, join, wait on a timer/signal, recover or retry locally, then cancel and shut down. Annotate every queue hop, completion and owner release. | Follow `async.h` through `core` and `execution`, ending at queue removal and frame/operation destruction |

**Exit question:** given the capstone trace, explain the source path responsible for each event and predict how changing a scheduler, poll budget, failure point or stop request changes the trace. Account for every live frame and operation through shutdown.

### How each lesson should teach

Each executable lesson follows the same learning loop:

1. **Predict:** state one concrete question and show the small scenario before opening its machinery.
2. **Observe:** run a deterministic experiment. Expose values, hook counts, thread IDs, ownership or event order with assertions.
3. **Explain:** connect each observed transition to the relevant code. Introduce one main mechanism; mark optional previews explicitly.
4. **Vary one condition:** change readiness, completion order, executor or failure position while retaining the familiar scenario.
5. **Read the real implementation:** name the invariant and source entry points that explain the result. Reduce reliance on teaching types as the course advances.

Keep a lesson's declarations, definitions, experiment and English explanation together in its `.cpp`. Each lesson is an independently runnable test; prerequisite references describe reading order, not execution dependencies. Keep helper code local and visible. For concurrency, establish order through explicit gates and bound waits. For performance, explain measurements after correctness and lifetime are understood.

The final criterion is a source-level explanation of standalone: how work is represented, started, queued, resumed, completed, cancelled and reclaimed. Passing a lesson's assertions is the observable check for that explanation.
