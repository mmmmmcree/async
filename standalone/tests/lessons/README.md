# Coroutine lessons

## Coroutine semantic pseudocode

This reference opens the implicit operations behind Lessons 01-04. It uses C++-style **semantic pseudocode**, not compilable C++, a required frame layout, or the exact source emitted by a compiler. `BODY_*` stands for user code. Every `__...` operation represents compiler machinery; in particular, suspension and transfer cannot be implemented by inserting an ordinary C++ `return` into the original function.

Read the [coroutine definition model](https://eel.is/c++draft/dcl.fct.def.coroutine), [await expressions](https://eel.is/c++draft/expr.await), [yield expressions](https://eel.is/c++draft/expr.yield), [coroutine return statements](https://eel.is/c++draft/stmt.return.coroutine), and [handle resumption/destruction](https://eel.is/c++draft/coroutine.handle.resumption) alongside the sketches. These are working-draft links; the lessons build as C++23.

The sections follow one execution: [create the frame](#call-time-setup-and-frame-allocation), [enter the coroutine](#the-coroutine-execution-body), [open each await](#the-co_await-expansion), [yield](#the-co_yield-expansion), [return](#the-co_return-expansion), then [complete or destroy](#completion-exceptions-and-destruction).

### Call-time setup and frame allocation

For a free or static member coroutine declared `R example(P0 p0, P1 p1)`, the promise type is selected at compile time:

```cpp
using Promise = typename std::coroutine_traits<R, P0, P1>::promise_type;
```

For a non-static member coroutine, the traits arguments also include the object parameter type, accounting for its cv/ref qualification. A promise **object** is constructed for each invocation; the compiler does not generate a new promise **type** at run time.

```cpp
R example(P0 p0, P1 p1)
{
    // This is the requested frame-storage size, not sizeof(Promise).
    std::size_t bytes = __required_frame_storage_size();

    // Allocation can be omitted when the compiler can embed the state elsewhere.
    // Otherwise use the allocation selection described below.
    void* storage = __allocate_or_embed_coroutine_frame(bytes, p0, p1);

    if (__allocation_failure_hook_is_present && storage == nullptr) {
        // No promise or frame parameter copies have been constructed.
        return Promise::get_return_object_on_allocation_failure();
    }

    auto& frame = __begin_frame_lifetimes(storage);

    // By-value parameters: construct retained copies from the original
    // parameter objects as xvalues. Reference parameters still borrow.
    // The order between different parameter-copy initializations is unspecified.
    __construct_parameter_copies(frame, p0, p1);

    // Use lvalues denoting the retained parameters for constructor lookup.
    // If a viable constructor exists, use Promise(q0, q1);
    // otherwise use Promise(). For an implicit object member, include *this.
    __construct_promise(frame, __copied_parameter_lvalues(frame));

    // Initialize the call's return object/reference directly from this expression.
    // Do not assume an extra Task copy, or store the return object in the frame.
    __initialize_call_result(frame.promise.get_return_object());

    // Executes the body below until it suspends or completes.
    __enter_first_activation(frame);

    // The caller receives the already-initialized result, even for a lazy task.
    __return_call_result_to_caller();
}
```

The sketch omits construction guards. If parameter copying, promise construction, `get_return_object()`, or the initial await before its `await_resume()` throws, the compiler unwinds the objects already constructed and releases any dynamically allocated frame storage. An allocation failure happens before these objects exist. None of these failures is handled by the body's `promise.unhandled_exception()`.

The allocation operation above follows these branches when storage is needed:

| Lookup/result | Allocation behavior |
| --- | --- |
| Promise lookup finds `operator new` declarations | First try `Promise::operator new(bytes, p0, p1)`, passing original parameters as lvalues; for an implicit object member include `*this`. If no overload is viable, retry with `bytes` alone. Failure of both attempts is a compile error, not a fallback to global allocation. |
| Promise lookup finds no declarations | Use global allocation, normally `::operator new(bytes)`. |
| Promise declares `get_return_object_on_allocation_failure` | The selected allocator must be `noexcept`; global allocation uses the nothrow form. A null result returns the failure hook's result without constructing the promise or entering the body. |
| No allocation-failure hook | Allocation failure normally throws `std::bad_alloc` to the caller. |
| Allocation is elided | No dynamic allocation call is required; the implementation still preserves the observable coroutine object lifetimes. |

Promise `operator new`/`operator delete` can route storage through a custom allocator or `std::pmr::memory_resource`. Because allocation precedes promise construction, `operator new` cannot obtain its resource from an already-constructed promise instance. Deallocation does not receive the original coroutine arguments; retain whatever resource information it needs. Standalone does this with frame metadata and [FrameAllocatorScope](../../include/async/core/coroutine/FrameAllocatorScope.h); [Lesson 29](lesson29_frame_allocators.cpp) separates frame allocation from execution-environment allocation. A resource must outlive every frame allocated through it and support the threads that eventually allocate/deallocate those frames.

### The coroutine execution body

The following is the execution part of that one invocation, not a separately called ordinary function. The compiler records a continuation position and any live objects that must survive a suspension. A later `resume()` jumps to that recorded position; it does not reconstruct the promise, repeat `get_return_object()`, or restart `initial_suspend()`.

`__AWAIT(operand, kind)` stands for the next section's expansion. The `Initial`, `Ordinary`, `Yield`, and `Final` labels select the applicable transformation rules; they are not library types.

```cpp
auto& promise = frame.promise;
frame.initial_await_resume_called = false; // Initialized once, before entry.

try {
    __AWAIT(promise.initial_suspend(), Initial);

    {
        BODY_A;

        // Original: decltype(auto) value = co_await EXPR;
        decltype(auto) value = __AWAIT(EXPR, Ordinary);

        BODY_B;

        // Original: co_yield YIELD_EXPR;
        __AWAIT(promise.yield_value(YIELD_EXPR), Yield);

        BODY_C;

        // Any explicit co_return is expanded using the return rules below,
        // then leaves this scope and jumps to FINAL_SUSPEND.
        __EXPAND_EXPLICIT_CO_RETURN_IF_REACHED();

        // Otherwise apply the fallthrough rule below. This is not an ordinary
        // C++ return statement, and is not always valid for a value coroutine.
        __APPLY_BODY_FALLTHROUGH_RULE();
    }
}
catch (...) {
    if (!frame.initial_await_resume_called) {
        throw; // Failed initial entry: unwind setup and escape to the caller.
    }

    // Policy supplied by the promise: store an exception, terminate, or throw.
    promise.unhandled_exception();
}

FINAL_SUSPEND:
__AWAIT(promise.final_suspend(), Final);

// Reached only when the final await does not retain the suspended frame.
__destroy_completed_coroutine_state_and_release_storage();
```

The initial-await flag is set immediately **before** evaluating the initial awaiter's `await_resume()`. Thus an exception from that `await_resume()` is handled by `unhandled_exception()`, but an earlier initial-await failure is not. The final await is outside the `try`; the whole expression `co_await promise.final_suspend()` must be non-throwing, not just the `final_suspend()` function itself.

### The co_await expansion

First select the awaiter, preserving the source expression's type, value category, and temporary lifetimes:

| Step | Rule |
| --- | --- |
| Transform the operand | For an ordinary `co_await EXPR`, if promise lookup finds any `await_transform` declaration, use `promise.await_transform(EXPR)`; otherwise use `EXPR`. An unusable declared overload is an error, not a reason to fall back to `EXPR`. |
| Initial, final, or yield await | Skip `await_transform`, but still perform the following awaiter-selection steps. |
| Select `operator co_await` | Perform the language's operator overload resolution. Use the selected member or non-member call; if no viable function exists, use the operand itself. Ambiguity is an error. This conversion is performed once, not repeatedly on its own result. |
| Materialize the awaiter | If necessary, materialize a prvalue temporary. Name the resulting object `e` as an lvalue, even when the original expression was an rvalue. The calls below use that lvalue. |

Then the three awaiter methods participate as follows. `__LEAVE_ACTIVATION()` and `__TRANSFER_AND_LEAVE_ACTIVATION(next)` are terminal control-flow operations for this activation: **they preserve live coroutine scopes** rather than running their destructors like a normal function return.

```cpp
auto&& e = __select_awaiter(promise, EXPR, kind);

// P is this awaiting coroutine's promise type, not the awaited child's type.
auto h = std::coroutine_handle<P>::from_promise(promise);

// Contextual bool conversion: await_ready need not return exactly bool.
bool ready = __contextually_convert_to_bool(e.await_ready());
if (!ready) {
    __save_resume_position(AWAIT_RESUME);
    __consider_current_coroutine_suspended();

    // This expression must be a prvalue of exactly void, bool,
    // or std::coroutine_handle<Z>. An arbitrary bool-convertible type is invalid.
    using S = decltype(e.await_suspend(h));

    if constexpr (std::is_void_v<S>) {
        try {
            e.await_suspend(h);
        }
        catch (...) {
            __resume_current_and_rethrow(); // Does not call await_resume().
        }

        __LEAVE_ACTIVATION(); // Return control to the caller/resumer.
    }
    else {
        S decision; // Compiler control-flow temporary, not a promised frame field.
        try {
            decision = e.await_suspend(h);
        }
        catch (...) {
            __resume_current_and_rethrow();
        }

        if constexpr (std::is_same_v<S, bool>) {
            if (decision) {
                __LEAVE_ACTIVATION();
            }

            // false: immediately resume this coroutine and continue below.
            __continue_current_coroutine_inline();
        }
        else {
            // S must be std::coroutine_handle<Z>.
            // Resume that coroutine; this coroutine stays suspended.
            // This transfer is OUTSIDE the await_suspend exception handler.
            __TRANSFER_AND_LEAVE_ACTIVATION(decision);
        }
    }
}

AWAIT_RESUME:
if (kind == Initial) {
    frame.initial_await_resume_called = true;
}

// This is the type, value category, and result of the original co_await.
// It may be void, a value, or a reference; this helper does not add a copy.
__use_as_await_expression_result(e.await_resume());
```

`h` is always the current awaiting coroutine's typed handle. An `await_suspend(std::coroutine_handle<>)` parameter accepts it through implicit type erasure; a parameter `std::coroutine_handle<P>` retains `.promise()` access. This explains the parent parameter versus the final awaiter's completed parameter in [Lesson 03](lesson03_nested_coroutines.cpp).

The `await_resume()` return type is not restricted to `bool`, `void`, or a handle; those restrictions apply to `await_suspend()`. It can return a move-only object, a reference, or even another awaitable. That last object is the result of this await expression and is not automatically awaited again.

For one dynamic evaluation of a `co_await`, awaiter selection and `await_ready()` occur once if evaluation reaches them. `await_suspend()` occurs at most once and is skipped when ready is true. On successful immediate continuation or later resumption, `await_resume()` occurs once. If the suspended coroutine is destroyed instead, it is never called. A loop may evaluate the same source-level `co_await` many times; those are separate evaluations. None of the three methods is required to run if an earlier operand/selection step throws.

Returning `void` or `true` does not itself resume the current coroutine. Returning `false` immediately resumes it. Returning a handle resumes the target, which may itself transfer onward. A `std::noop_coroutine()` handle is a valid no-op target; an empty `std::coroutine_handle<>{}` is not a valid resume target. No handle transfer restarts awaiter selection or calls `await_ready()` on the handle.

Once `await_suspend` publishes `h`, another thread, or an inline callback, can resume the coroutine before `await_suspend` returns. It can even destroy the awaiter/frame. The publishing code and terminal leave/transfer paths must not access objects whose lifetimes have ended. The pseudocode's terminal helpers do not write to the frame. Do not also return `false` after already arranging a competing resumption. [Lesson 16](lesson16_completion_handshake.cpp) studies this handshake.

### The co_yield expansion

Both yield forms use the same await protocol:

```cpp
co_yield EXPR;
// Equivalent await operation, marked Yield so await_transform is skipped:
__AWAIT(promise.yield_value(EXPR), Yield);

co_yield {A, B};
// The braced-init-list is passed directly to yield_value:
__AWAIT(promise.yield_value({A, B}), Yield);
```

`yield_value` decides how to expose or store the yielded item and returns the operand for this await. The selected awaiter's `await_ready`/`await_suspend` decide whether to stay suspended; **the keyword alone does not guarantee suspension**. A generator commonly returns `std::suspend_always` to let its consumer read the item before resuming it. A `co_yield` expression's type, value category, and result come from that awaiter's `await_resume()`, so a custom yield can also produce a value back inside the coroutine. Yielding does not call `return_value()` and does not finish the coroutine. There is no built-in scheduler or thread handoff.

### The co_return expansion

These are alternative source forms. `co_return` chooses a promise completion hook; it does not return the computation result directly to the original C++ caller.

| Source | Semantic expansion before leaving the body |
| --- | --- |
| `co_return RESULT;`, with non-void expression | `promise.return_value(RESULT); goto FINAL_SUSPEND;` |
| `co_return {A, B};` | `promise.return_value({A, B}); goto FINAL_SUSPEND;` |
| `co_return;` | `promise.return_void(); goto FINAL_SUSPEND;` |
| `co_return VOID_EXPR;`, where the expression has type `void` | `VOID_EXPR; promise.return_void(); goto FINAL_SUSPEND;` |
| Reach the end of the body without explicit `co_return` | Equivalent to `co_return;` if overload resolution for `promise.return_void()` succeeds; otherwise undefined behavior. |

For example, a void-completion path is:

```cpp
// Original:
co_return notify(); // notify() returns void.

// Semantic expansion:
{
    notify();
    promise.return_void();
    goto FINAL_SUSPEND;
}
```

An exception from `notify()` prevents the subsequent `return_void()` call and follows the body's exception path. Likewise, an exception while evaluating `RESULT` or invoking `return_value()` follows the exception path instead of the normal return jump. The completion hook expressions themselves must have type `void`. Preserve C++'s implicit-move rules for eligible return operands; the table is a semantic sketch, not a literal source rewrite that discards those rules.

After a successful completion hook, the jump leaves body scopes, destroying their local objects, **before** calling `final_suspend()`. The examples show alternative promise completion contracts rather than a runtime choice between hooks; the lesson promise types supply the matching hook. A coroutine cannot use an ordinary `return;` or `return value;` statement; `return_void()` is a promise method, not a C++ `return void` statement. The original call can return `Task<void>` while its eventual completion carries no value.

### Completion, exceptions, and destruction

The object returned by `final_suspend()` is an **awaiter/awaitable**, not the next handle. Only its selected awaiter's `await_suspend()` may return a next handle. At this point the body scopes have already ended, while the promise and retained parameters still exist.

| Final await behavior | Result |
| --- | --- |
| `await_ready()` is true, for example `std::suspend_never` | Run final `await_resume()`, flow off the replacement body, and destroy the state. A previously exposed handle must not subsequently be used to inspect or destroy the released frame. |
| Ready is false and `await_suspend()` returns false | Immediately continue through final `await_resume()`, then destroy the state. |
| Ready is false and `await_suspend()` returns void or true | Keep the completed frame suspended and return control to the caller/resumer. `std::suspend_always` follows the void branch; it does not resume a parent automatically. |
| Ready is false and `await_suspend()` returns a handle | Keep this completed frame suspended and resume the target, as with Lesson 03's child-to-parent transfer. A root can return `std::noop_coroutine()` when there is no continuation. |

For a live handle to a coroutine suspended at its final suspend point, `done()` is true and `resume()` is forbidden. When the final await retains the frame, do not later resume it to execute the final awaiter's `await_resume()`: the owner destroys the frame instead. The parent reads a completed child's result through the **parent's own child-awaiting awaiter**; that is a different awaiter from the child's final awaiter.

The exception paths are distinct:

| Failure location | Control flow |
| --- | --- |
| Allocation, parameter copies, promise construction, or `get_return_object()` | Propagate to the coroutine caller after applicable setup cleanup, or return the allocation-failure hook result for null allocation. The body has not started. |
| Initial operand/awaiter selection, readiness, or `await_suspend()` | Initial `await_resume()` has not started: propagate to the coroutine caller with cleanup. |
| Initial `await_resume()` | The flag is already true: call `unhandled_exception()` if the exception escapes the coroutine's handlers. |
| Body expression, ordinary/yield awaiter selection, readiness, `yield_value()`, `await_resume()`, or completion hook | Ordinary C++ handlers inside the body can catch it. If it escapes the body, unwind its scopes and call `unhandled_exception()`. |
| Ordinary/yield `await_suspend()` | Resume the awaiting coroutine and rethrow at the await expression without calling `await_resume()`; a surrounding body handler may catch it. |
| `unhandled_exception()` returns normally | Proceed to the final await. Storing an `exception_ptr` is promise policy, not automatic language behavior. |
| `unhandled_exception()` itself throws | The coroutine is considered suspended at the final suspend point, and the exception escapes to the caller/resumer. The normal `final_suspend()` expression is skipped. |
| Final await | The entire expression must be non-throwing. Violating a `noexcept` function's guarantee terminates; there is no second body catch around the final await. |

Ordinary C++ destructor and `noexcept` rules still apply. In particular, an exception that would escape a `noexcept` coroutine call or resumption wrapper terminates instead of propagating beyond that boundary.

Frame destruction has two entry paths, with corresponding cleanup:

```cpp
// Path A: final await continued, so control flows off the replacement body.
// Path B: the owner calls handle.destroy() on a suspended coroutine.

__destroy_live_awaiters_and_body_locals_if_any();
__destroy_promise();
__destroy_parameter_copies();
__release_frame_storage_if_dynamically_allocated();
```

Path B can abandon a coroutine at its initial or an ordinary suspension point. It exits live scopes and destroys their objects without running `await_resume()`, the remaining body, `return_void()`/`return_value()`, or a new call to `final_suspend()`. It is not equivalent to resuming the coroutine until it completes. Destroying a frame that is running is invalid; destroying a suspended parent also requires respecting any outstanding callbacks that borrow its handle.

Deallocation lookup starts in the promise type and otherwise uses global deallocation. A usual sized deallocation function is preferred when both sized and unsized forms are present; no viable usual form is a compile error. A selected sized `operator delete` receives the storage address and size, not the original coroutine arguments. The compiler manages the actual layout and may omit allocation/deallocation together. A `coroutine_handle` does not own or automatically release this state merely by being copied or going out of scope; the lesson's task/awaiter types supply ownership.

## Course overview

These lessons assume ordinary C++ knowledge: functions, classes, references, destructors and exceptions. No coroutine, sender/receiver or scheduler background is required. Each `.cpp` contains one GoogleTest lesson, its teaching types and their definitions. Read the `TEST` first, then follow the functions it calls. `EXPECT_*` checks a prediction; `ASSERT_*` stops the lesson when a prerequisite fails.

The complete course has **31 implemented lessons in seven stages**. Each lesson is an independently runnable GoogleTest. The goal is to explain the actual standalone backend, from a coroutine's first suspension to scheduling, composition, cancellation and frame reclamation.

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

## Standard coroutine references

These are the C++ standard draft clauses behind the language mechanics used in Lessons 01–04. They describe coroutine transformations and the awaiter protocol; the compiler is not required to emit the same C++ source text or literal `try`/`catch` blocks.

| Topic | C++ draft clause |
| --- | --- |
| Coroutine function definition, promise, frame, initial/final suspend and `co_return` | [`dcl.fct.def.coroutine`](https://eel.is/c++draft/dcl.fct.def.coroutine) |
| `co_await` expression and `await_ready` / `await_suspend` / `await_resume` | [`expr.await`](https://eel.is/c++draft/expr.await) |
| `co_yield` expression | [`expr.yield`](https://eel.is/c++draft/expr.yield) |
| `co_return` forms, including void expressions and body fallthrough | [`stmt.return.coroutine`](https://eel.is/c++draft/stmt.return.coroutine) |
| Handle `resume()`, `done()`, and `destroy()` preconditions | [`coroutine.handle.resumption`](https://eel.is/c++draft/coroutine.handle.resumption) |

Read [`dcl.fct.def.coroutine`](https://eel.is/c++draft/dcl.fct.def.coroutine) first for the complete coroutine function model, then [`expr.await`](https://eel.is/c++draft/expr.await) for the local `co_await` transformation used in Lesson 02. The standard specifies observable semantics and preconditions; it does not require a compiler to emit literal `try`/`catch` source code.

| Lesson | Question | File |
| --- | --- | --- |
| 01 | What remains alive when a function suspends? | [Frame lifetime and manual resume](lesson01_frame_lifetime.cpp) |
| 02 | What exactly does `co_await` call? | [The awaiter protocol](lesson02_await_protocol.cpp) |
| 03 | How does a child return a value or exception to its parent? | [Nested coroutines and continuations](lesson03_nested_coroutines.cpp) |
| 04 | Who owns the frame and its data while it is suspended? | [Ownership across suspension](lesson04_ownership.cpp) |
| 05 | Who calls resume after the caller hands work off? | [A callback queue](lesson05_callback_queue.cpp) |
| 06 | What do sender, receiver and operation state each do? | [The completion protocol](lesson06_sender_receiver.cpp) |
| 07 | How do value, error and stopped differ? | [Completion channels](lesson07_completion_channels.cpp) |
| 08 | Can a whole pipeline finish inside start? | [An inline pipeline](lesson08_inline_pipeline.cpp) |
| 09 | Which object submits, and which object runs work? | [Context and executor](lesson09_context_executor.cpp) |
| 10 | What does a scheduler manufacture? | [A schedule sender](lesson10_scheduler_sender.cpp) |
| 11 | Why does an idle worker wait or exit? | [Worker and work lifetime](lesson11_worker_lifetime.cpp) |
| 12 | Does a polling budget count callbacks or logical steps? | [Cooperative budgets](lesson12_poll_budgets.cpp) |
| 13 | Who drives queued work inside sync_wait? | [The waiting run loop](lesson13_sync_wait.cpp) |
| 14 | How can Awaitable be both a child and a root sender? | [Real coroutine results](lesson14_awaitable_roots.cpp) |
| 15 | How does co_await use connect and start? | [Awaiting a sender](lesson15_await_sender.cpp) |
| 16 | What if completion happens before await_suspend returns? | [The completion handshake](lesson16_completion_handshake.cpp) |
| 17 | How does a coroutine return to its own executor? | [Environment and routing](lesson17_environment_routing.cpp) |
| 18 | How do yield and reschedule_on change execution? | [Queue and thread handoffs](lesson18_yield_reschedule.cpp) |
| 19 | How do starts_on, continues_on and on differ? | [Scheduling a graph](lesson19_schedule_graph.cpp) |
| 20 | Does returning a sender also start it? | [then and let_value](lesson20_map_and_flatten.cpp) |
| 21 | When may independent branches complete a join? | [when_all](lesson21_join_branches.cpp) |
| 22 | Who owns a shared result for late subscribers? | [split](lesson22_share_execution.cpp) |
| 23 | Where does execution resume after local recovery? | [Error recovery](lesson23_local_recovery.cpp) |
| 24 | Does a retry reuse an already-started operation? | [Fresh attempts](lesson24_retry_attempts.cpp) |
| 25 | When does stopped skip pending coroutine code? | [Cooperative cancellation](lesson25_stop_requests.cpp) |
| 26 | Can registering or completing destroy the operation? | [Registration and destruction](lesson26_registration_lifetime.cpp) |
| 27 | Who makes a timer ready, and who delivers it? | [Timers](lesson27_timers.cpp) |
| 28 | Is a signal resettable, and what do late waiters see? | [One-shot notifications](lesson28_signals.cpp) |
| 29 | Which allocator creates a frame, and when? | [Frame and execution allocators](lesson29_frame_allocators.cpp) |
| 30 | Can a frame outlive its creator thread? | [Reuse and reclamation](lesson30_frame_reclamation.cpp) |
| 31 | Can you explain each completion and owner through shutdown? | [A complete backend trace](lesson31_backend_trace.cpp) |

## First-reading guide

Use a C++23-capable toolchain and read in numeric order. These are standalone's sender/receiver APIs under `lcf::async::exec`, not an assumption that the standard library supplies the same algorithms. In the files, `namespace ex = lcf::async::exec;` is only a shorter spelling.

Start at the `TEST` near the bottom of each file. Read its setup and the assertion immediately after each action, predict the result, then open the helper named by that action. Do not start by reading every template declaration. A prerequisite is a reading prerequisite; no lesson needs another test to have run first. The first four teaching types explain language mechanics; production types take over from Lesson 09.

Small pieces of C++ notation recur:

- `sender_concept` and `receiver_concept` are marker type aliases. A concept checks requirements at compile time; these markers are not inheritance hierarchies.
- `set_value_t(int)` is a function type used to describe a channel and its payload. `completion_signatures<...>` lists possible outcomes; it does not execute them. Lesson 07 checks both the types and actual deliveries.
- `ex::connect`, `start` and completion objects call the corresponding member functions. A terminal receiver member is `noexcept`. Exceptions raised by work travel through `set_error`; they do not escape the terminal call.
- `auto operation = connect(...)` constructs the operation directly. Some operation types are intentionally immovable because callbacks point into them; guaranteed copy elision allows a factory to return such an object.
- A result tuple holds zero, one or several values. `ASSERT_TRUE(result)` checks that an optional contains a tuple before `std::get<0>(*result)` reads it. An engaged empty tuple represents successful void completion; it differs from `nullopt` (stopped).
- `NativeApi.h` is a development-only include wrapper in `tests/common`. It selects the public `async.h` header or the module import; it does not provide a hidden lesson runtime. Teaching helpers remain in their own `.cpp`.

For each experiment, keep four questions separate: **who owns it, who starts it, who resumes it, and where does it run?** A scheduler in an environment does not by itself create a worker or queue initial body entry. Explicit scheduling and the thread that pumps a context determine execution.

## Build, run and debug

After configuring a development preset, run from the async project root:

```powershell
cmake --build --preset header-msvc-debug --target async_standalone_lessons
```

This target builds and runs all 31 lessons. Substitute `header-clang-debug` or another configured build preset as needed. To run already-built lessons through CTest:

```powershell
ctest --preset header-msvc-debug -L standalone_lessons --output-on-failure
```

In VS Code, build and debug `async_standalone_lessons_tests`. Select a single lesson with an executable argument such as:

```text
--gtest_filter=CoroutineLessons.Lesson02AwaitReadySuspendResume
```

To select a later stage, use a wildcard such as `--gtest_filter=CoroutineLessons.Lesson2*` (Lessons 20–29). To inspect Lesson 30's address-reuse observation, add `--gtest_output=xml:lessons.xml` and read its `repeated_frame_addresses` property. This observation is deliberately not a portable performance assertion.

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

All rows have executable files linked in the index. The steps deliberately separate mechanisms; the notes after the curriculum explain the main observations. Source paths in the tables are relative to `standalone/include/async`.

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
| **27. Timers** | 10–13, 15–16, 25–26 | Separate deadline eligibility from delivery on the owning context. Admit an expired backlog one callback at a time, cancel across stop/restart, then compare permanent shutdown with starting on a closed context. | `core/timer/operations.h`, `senders.h`, `details/TimerService.h`, `details/timer.h`, `details/timer_wait.h`, `core/scheduling/details/EventSource.h`, `details/queue.h` |
| **28. Signals and notifications** | 07, 15–16, 25–27 | Register waiters, cancel one, emit a one-shot signal and observe incremental wakeup and late subscription. Start another round with a fresh Signal; there is no reset API. | `core/synchronization/Signal.h`, `details/notification.h`, `notification_state.h`, `CompletionEvent.h` |

**Exit question:** for a pending timer or signal wait, list who owns the operation, who may complete it, how cancellation is registered, and which synchronization step prevents a second completion or access after destruction. For a timer, distinguish an eligible deadline, a queued cancellation, a paused context and permanent shutdown. Which paths deliver a terminal result, and which only release ownership?

### Stage VII — Account for memory and explain a full run, Lessons 29–31

The mechanisms are already understood. Now examine their storage and optimization policies, preserving the behavior established by the earlier lessons.

| Lesson | Prerequisites | Experiment and observable result | Source endpoint |
| --- | --- | --- | --- |
| **29. Frame and timer allocation policies** | 01, 04, 14, 17, 26–27 | Trace four independent resources: frame creation, execution environment, context timer heap and delay registration. Observe allocation at construction/connect/start and release at completion or final queue destruction. | `FrameAllocatorScope.h`, promise `operator new/delete`, `execution/environment.h`, `ReturnStorage.h`, `core/scheduling/Context.h`, `core/timer/operations.h`, `details/timer.h` |
| **30. Frame reuse and thread lifetime** | 11, 26, 29 | Observe repeated same-sized frame allocation and cross-thread reclamation. Trace cache bins, resource provenance and thread-local cache retirement. Use counts and ownership traces before interpreting benchmark timings. | `core/coroutine/details/frame.h`, existing frame-lifetime verification and benchmark probes |
| **31. Capstone: trace the whole backend** | 01–30 | Drive a frame context with a cooperative budget while a worker produces a shared result. Branch, retry locally and wait for a signal. Observe timer registration before either expiry or cancellation, join, and permanently shut down. Annotate every queue hop, completion and owner release. | Follow `async.h` through `core` and `execution`, ending at queue removal and frame/operation destruction |

**Exit question:** given the capstone trace, explain the source path responsible for each event and predict how changing a scheduler, poll budget, failure point or stop request changes the trace. Account for every live frame and operation through shutdown.

## Walkthrough: Lessons 05–13

**05:** `owner.start()` executes the first body step and queues a callback borrowing the frame. Only `queue.pollOne()` executes the second step. The separate shared-pointer experiment shows that the queue owns its callable and captured data until invocation returns. Keep this owning relationship distinct from the callback's non-owning coroutine handle.

**06:** draw three boxes: `IntegerSender` (description), `Operation` (receiver plus value), and `RecordingReceiver` (destination). Connect builds the middle box; start calls through to the last. A copyable sender can describe another execution, but a particular operation is started once.

**07:** select each `Channel` and predict which counter changes. The error integer is payload on a different channel, not a successful negative answer. The final two cases execute void and multi-value completions. `just()` is a small preview of the factory opened in the next lesson.

**08:** the pipe builds nested descriptions; it does not invoke the lambdas. Both continuations execute during one start because each upstream stage completes inline. There is no context or thread handoff here. Adding another `then` adds a logical step, not automatically a queue boundary.

**09:** both copied executors point to the context's queue. Readiness changes from two to one to zero as the caller pumps it. The IDs show that posting is not the same as launching a worker.

**10:** `schedule(scheduler)` produces a sender; connect produces its operation; start enqueues its callback. The receiver is pending until poll. Its `get_env()` returns a small property object so nested operations can ask for a scheduler. The sender's completion-scheduler query answers a different question: where that sender delivers values.

**11:** outstanding work keeps `runUntilIdle` waiting even when the ready queue is empty. The callback's explicit gate establishes when a second callback is waiting in that queue; a timeout alone is not used to infer scheduling. Releasing the gate permits `workFinished()` and idle exit. In the separate stop/restart experiment, stop preserves queued work and restart admits it. Join establishes when worker-written observations may be read safely.

**12:** a budget admits callbacks. One callback can start the entire familiar inline pipeline, and the budget does not count its individual continuations. A time allowance is checked between callbacks; the slow admitted callback completes even after the allowance has expired. The short busy interval demonstrates that rule, not useful throughput.

**13:** `sync_wait` connects a receiver with its own run-loop scheduler, starts, and pumps that loop on its caller. This lets a coroutine yield without creating a worker. It cannot drive arbitrary unrelated contexts: work explicitly scheduled elsewhere still needs that context driven. Success returns an optional tuple, error rethrows, and stopped returns an empty optional. Do not generalize stopped to an exception.

## Walkthrough: Lessons 14–18

**14:** match value, void and move-only results to Lesson 03's teaching storage. A child is consumed by `TaskAwaiter`, while a root is connected through `AwaitableOperation`. Reading a result and releasing a frame are separate responsibilities. The tests exercise the same result kinds on both paths and transport an exception.

**15:** compare the two routes written in the file's source map. Awaiting an Awaitable uses a child frame and continuation. Awaiting a sender builds an adapter receiver and operation state; its result is stored for `await_resume`. The event sequence exposes connect/start/delivery between the parent's before/after events.

**16:** the three inputs force completion inline, on a joined producer during start, and on a producer after suspension setup. The first two continue through `await_suspend` returning false; the last uses publication to resume. Atomics publish stored data and select one continuation path. The test supplies no scheduler, so only that last case continues on the completing thread. A scheduler environment changes the routing, which is the next lesson.

**17:** the child's environment inherits the home executor. Completing a schedule sender on another context queues a resume callback back home; polling only the foreign context cannot finish the parent. After home runs, unchanged child routing transfers directly to the parent, then root dispatch notifies its receiver. Directly starting a root with an environment enters the body inline; `as_sender` or `starts_on` adds initial scheduling.

**18:** count two callbacks for a real coroutine yield: the scheduled operation completes, then its adapter queues coroutine resumption. Those are two callbacks even if an unrestricted poll executes both. The child then changes its executor with `reschedule_on`. Its saved return executor still identifies home, so final completion queues the parent's return there. The recorded IDs make each transition visible.

## Walkthrough: Lessons 19–24

**19:** read the three-row table in the source before tracing the operations. `starts_on` schedules producer entry and supplies that scheduler upstream. `continues_on` transfers the stored outcome downstream. `on` schedules the producer and restores the downstream receiver's scheduler afterward. Ordinary continuations execute where completion is delivered; coroutine resumption has the additional routing seen in Lesson 17.

**20:** `then` returning `just(42)` returns a sender as an ordinary value. Running that nested sender requires another execution. `let_value` instead creates and starts its nested operation. Its retained input permits a nested scheduled callback to borrow the shared pointer by reference until completion. The observer verifies release when the outer operation's scope ends.

**21:** on success, branch two completes first and values still arrive as 20, 22. In the error variation, branch one errors first and the join remains pending until branch two terminates. That manual sender deliberately ignores stop requests; cancellation cannot force arbitrary work to terminate. Keep the join owner alive until both callbacks have been consumed.

**22:** both subscribers observe the identical move-only payload in shared completion storage, and a late subscriber sees that same identity without another producer call. A marker inside the cached payload stays live through completion and expires when all owners leave scope. Turn borrowed shared results into owned outputs before their storage can disappear.

**23:** the prefix ran before failure, so local recovery does not undo or repeat it. An intervening value continuation is skipped. `upon_error` maps error to value; `let_error` creates recovery work that can remain queued. A single manual poll makes that distinction visible without introducing another worker mechanism.

**24:** the attempt vector is 1, 2, 3, while construction/destruction counters show one live attempt state at a time. A limit of two preserves the error before the third attempt can succeed. The larger failure-only case exercises the iterative drive loop; many immediate failures must not produce one recursive start chain per attempt.

## Walkthrough: Lessons 25–31

**25:** compare pre-stop with a root already waiting on a signal. Stopped skips the code after its await. For split, distinguish active subscriptions from reusable sender handles: the sender handle itself counts as a consumer. The three sharing cases cancel one subscriber, cancel all consumers, or cancel both subscriptions while retaining the reusable handle. Only the case with no remaining consumer handles requests upstream stop.

**26:** the first event trace puts cancellation inside the registration constructor. Registration gates defer completion until setup can safely finish. The production experiment then controls cancellation versus notification ordering, including a notification already queued when cancellation wins. Its receiver deletes its own operation. Registration and queued ownership keep callbacks safe through that deletion, and the terminal guard prevents a second notification.

**27:** the context owns a timer service and its min-heap; no implicit timer thread exists. Duration begins at start. Time passing makes a deadline eligible; `getReadyCount` observes that eligibility without running the receiver. Follow four experiments:

1. A zero-duration timer belongs to `context`; polling `other_context` cannot deliver it. The owning context's `pollOne` supplies the actual receiver call and thread.
2. Two expired timers require two admissions. The one-callback budget leaves the second pending; ready work is not bulk-delivered behind that budget.
3. A one-hour timer remains outstanding even when `pollOne` returns zero. `stop` pauses dispatch; cancellation queues a stopped delivery, and `restart` lets the pump deliver it exactly once.
4. `shutdown` permanently closes the context and abandons the existing timer's receiver without calling its terminal methods. The weak marker expires. A new timer started through the retained scheduler is instead rejected with immediate stopped completion; restart does not reopen it.

The active path is `start -> TimerRegistration -> TimerService -> TimerQueue`, with registration and cancellation serialized by the context transaction. Delivery follows `pollOne -> Queue -> EventSource::poll -> TimerService::poll -> Callback::invoke -> receiver`. Shutdown detaches ownership within the transaction and abandons it outside the lock. `runUntilIdle` can wait until a future deadline; these experiments use zero-duration or one-hour waits to avoid sleeping or asserting exact wall-clock timings. Revisit Lesson 13 to identify the private context that can drive `sync_wait(delay(...))`.

**28:** this Signal is one-shot. Canceling one waiter removes it without notifying the others. Set queues the first active waiter; its dispatch advances to the next. A late waiter observes the already-set signal. A fresh Signal starts another independent round; no reset call exists.

**29:** the frame resource records allocation while body entries are still zero. The execution resource is queried later and used by the small PMR vector; it cannot relocate the existing frame. Finishing releases that frame through its recorded resource with the original size and alignment. Discarding another unstarted task releases its frame without entering its body. The second experiment revisits Lesson 27 with separate heap and registration resources:

| Resource selection | What it controls | Observable lifetime |
| --- | --- | --- |
| `FrameAllocatorScope{frames}` | Coroutine frame storage | Allocated before lazy body entry; released through its recorded resource |
| `with_allocator(work, &execution)` | The allocator queried from that execution environment | The teaching sender explicitly uses it for a temporary PMR vector |
| `Context{&timer_heap}` | The shared timer heap, including any Debug container proxy | Start grows the heap; capacity survives delivery/shutdown and is returned when the queue dies |
| `delay(duration, &registrations)` | That delay's registration | Connect allocates it; delivery or abandonment releases it |

The timer receiver supplies a scheduler environment, allowing `delay` to find the context's service. Connect changes only the registration resource's count; start changes the heap resource's count. After polling, the registration is balanced while the empty heap still owns storage. Final context/queue destruction balances the heap resource too. These are non-owning resource associations: each resource must outlive every frame, operation or retained queue that can use it. No setting in the table globally redirects all allocations.

**30:** repeated frame addresses may appear, but no test treats address reuse as a semantic promise. The XML property records the observation. Ownership checks instead require every payload to expire, including a frame moved out of its creator thread and a TLS-held task destroyed after cache retirement. Consult cache bins and the retired direct-delete path before drawing conclusions from timing.

**31:** root entry queues a producer; its joined worker completes a shared result and performs a local two-attempt retry. Both branches return to the frame context; one waits for a signal, then registers its timer on that context. A second gate observes registration after the signal-delivery callback returns, before the next timer delivery. Polling the producer cannot deliver the frame timer. Every frame poll admits at most one callback. The zero-duration success trace returns 41; the one-hour case first demonstrates an empty poll with outstanding work, then cancels the timer and skips the join's coroutine continuation. Both traces include `caller: signal -> branch: wait timer`; only the cancellation trace then adds `caller: stop`. The final marker, empty queues and explicit `shutdown` calls account for ownership and permanent closure. Reconstruct that trace from the source map before changing one input.

### How each lesson should teach

Each executable lesson follows the same learning loop:

1. **Predict:** state one concrete question and show the small scenario before opening its machinery.
2. **Observe:** run a deterministic experiment. Expose values, hook counts, thread IDs, ownership or event order with assertions.
3. **Explain:** connect each observed transition to the relevant code. Introduce one main mechanism; mark optional previews explicitly.
4. **Vary one condition:** change readiness, completion order, executor or failure position while retaining the familiar scenario.
5. **Read the real implementation:** name the invariant and source entry points that explain the result. Reduce reliance on teaching types as the course advances.

Keep a lesson's declarations, definitions, experiment and English explanation together in its `.cpp`. Each lesson is an independently runnable test; prerequisite references describe reading order, not execution dependencies. Keep helper code local and visible. For concurrency, establish order through explicit gates and bound waits. For performance, explain measurements after correctness and lifetime are understood.

The final criterion is a source-level explanation of standalone: how work is represented, started, queued, resumed, completed, cancelled and reclaimed. Passing a lesson's assertions is the observable check for that explanation.
