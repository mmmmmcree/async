# Async 设计速读：从协议到状态机

日期：2026-10-24。本文从定义和语义展开理解设计，不以 lessons 为前置。

阅读顺序：先看第 1～5 节理解 execution，再看第 6～8 节把协程接上，最后看第 9 节确定重建边界。代码标为“语义伪代码”的地方只表达控制流，省略模板约束、值类别、定制分派和部分存储细节，不能直接复制为生产实现。

当前采用 **standalone core 的机制 + stdexec 的原生协议和算法**；未来标准库满足需求后替换集中接入层。目标是 C++26 execution，定义参考滚动工作草案，不能据此推断某个已安装标准库已经实现。实施顺序见 [统一实现重建路线](async-rebuild-plan.md)。

只读一条主线：**描述工作 → connect 创建一次操作 → start 推进 → receiver 接收完成**。组合算法包装这条链；调度器改变它在哪里推进；协程用编译器状态机表达同一类依赖。

## 1. 整个设计由谁负责

```text
用户程序：             描述任务、组合依赖、选择错误恢复方式
                         │
execution 协议与算法： sender → connect(receiver) → operation state → start
                         │                    │
本库 core：             queue / timer / signal / scheduler / context
                         │                    │
执行资源：              当前线程、运行循环、线程池等实际推进工作

协程语言机制：          编译器生成可挂起的状态机与 frame
协程库适配：            promise / awaiter 把这个状态机接到上述协议
```

这里没有“所有异步工作必须先进入一个全局调度器”的规定。同步完成、单线程事件循环和线程池工作都能使用同一套协议。

先抓住四个对象：**sender 描述如何创建操作；receiver 描述操作完成后做什么；operation state 保存这一次操作；scheduler 提供进入某个执行资源的 sender。**

依据：[异步操作定义](https://eel.is/c++draft/exec.async.ops)、[scheduler](https://eel.is/c++draft/exec.sched)。

## 2. 标准定义的是协议，不是一组必须继承的基类

`sender`、`receiver`、`operation_state`、`scheduler` 是 concept；`connect`、`start`、`set_value` 等是定制点对象（CPO）。通过类型、表达式和语义约束协作，不要求一套虚函数继承树。

### 2.1 最小执行契约

```cpp
// 语义伪代码。ex 表示集中接入的 execution 提供方。
auto sender = /* 工作描述 */;
auto op = ex::connect(std::move(sender), receiver);
ex::start(op);                          // 对这次操作至多调用一次

// 操作启动以后，最终恰好选择一个完成通知：
ex::set_value(std::move(receiver), values...); // 成功：0 个或多个值
ex::set_error(std::move(receiver), error);     // 失败：1 个错误对象
ex::set_stopped(std::move(receiver));          // 停止：0 个值
```

后三行是三种互斥的完成方式，不是依次执行的三步。`connect` 创建的操作拥有实际使用的 receiver；上面复用变量名只为展示接口关系。

| 对象 | 保存的主要内容 | 关键动作 |
| --- | --- | --- |
| sender | 参数、上游 sender、组合函数等工作描述 | 与 receiver 连接，创建一次操作 |
| receiver | 下一步行为及调用方 environment | 接收一种完成通知 |
| operation state | receiver、子操作、回调节点、临时结果等运行状态 | `start(op)` 启动一次操作 |
| scheduler | 某个执行资源的可复制访问入口 | `schedule(sch)` 创建调度 sender |
| environment | 查询得到的停止令牌、allocator、scheduler 等属性 | 提供操作执行所需的上下文 |

不是每个 receiver 都要实现所有错误类型。它必须支持所连接 sender 在该环境下**实际可能发出的全部完成签名**。

把定制点和对象成员对应起来，协议的形状就很小：

```cpp
// 语义接口骨架：省略标记类型、concept 约束和具体存储。
struct Receiver {
    Environment get_env() const noexcept;
    void set_value(Values...) && noexcept;
    void set_error(Error) && noexcept;
    void set_stopped() && noexcept;
};
struct Operation {
    Receiver receiver;       // 实际操作状态拥有它
    void start() & noexcept;
};
struct Sender {
    Operation connect(Receiver) &&; // 某些 sender 也支持 lvalue 连接
    // 此外按协议提供完成签名与 sender 属性。
};
struct Scheduler {
    ScheduleSender schedule() const; // 创建 sender 的阶段可以抛异常
};

// 在工作草案通常的成员定制路径上：
ex::connect(move(s), move(r));      // → move(s).connect(move(r))
ex::start(op);                     // → op.start()
ex::set_value(move(r), values...); // → move(r).set_value(values...)
ex::schedule(sch);                 // → sch.schedule()
```

成员存在只是接口检查的一部分；标记、环境、签名兼容性和生命周期语义也必须满足。例如 `noexcept` 的完成函数内部若要保存结果，应自行捕获保存失败并按协议报告，不能让异常逃出。此骨架不代替链接中的完整 concept 定义。

### 2.2 sender 的“返回类型”是完成签名集合

```cpp
// 例如：运行时可能成功给出 int，可能失败，也可能停止。
ex::completion_signatures<
    ex::set_value_t(int),
    ex::set_error_t(std::error_code),
    ex::set_stopped_t()
>;
```

这不是三个函数调用，也不是 `std::variant` 对象，而是编译期协议说明。一个 sender 还可以有多种成功签名，例如 `set_value_t(int)` 和 `set_value_t(std::string)`；不能普遍把 sender 简化成 `Sender<T>`。

签名可能依赖 environment。完整匹配关系是：**sender 类型 + environment 类型 → 可能的完成签名 → receiver 必须接受的调用**。`sender` concept 本身不是“这个 sender 与任意 receiver 都能运行”的证明。

依据：[sender concepts](https://eel.is/c++draft/exec.snd.concepts)、[receiver](https://eel.is/c++draft/exec.recv)。

## 3. 生命周期：构造、连接、启动、完成

```text
构造 sender 图       connect                 start             完成通知
描述工作       →     创建并保存 operation  →  启动操作    →     value/error/stopped
                     尚未启动                 可能当场完成        可以释放 operation
```

以 `just | then` 为例，构造管线不会调用 `then` 中的用户函数；`connect` 把操作状态接好；`start` 才触发这次执行。具体 sender 的构造和连接仍可执行准备工作、申请资源或抛异常，不能把“惰性启动”理解成这些步骤绝对没有副作用。

必须同时记住以下约束：

- `start(op)` 和完成通知不能把异常抛给调用方；`connect`、复制参数、分配存储等准备步骤则可能抛异常。
- `start` 允许同步完成；不能假设它返回后才会收到 receiver 通知。
- 已启动操作必须最终恰好完成一次；标准没有给出完成的时间上限。永远不触发事件不是一个可以跳过终止通知的错误处理策略。
- operation state 的存储必须覆盖操作的异步生命周期。局部 `op` 启动后立刻离开作用域，对尚未完成的操作不合法。
- 通用代码不能假定 operation state 可复制或移动；直接从 `connect` 的结果原位构造它，保持地址稳定。
- 完成通知可能当场销毁 operation state。完成调用后，不得继续访问它的成员；停止回调等需要依赖 receiver 环境的设施，应在通知前清理。

```cpp
// 语义伪代码：op 由某个稳定 owner 保存到完成。
auto op = ex::connect(std::move(source), terminal_receiver);
ex::start(op);
// 若尚未完成，这里不能直接销毁 op；owner 必须继续维持其生命周期。
```

对于简单的阻塞入口，可以先用提供方的 `sync_wait`，由它负责连接、启动及等待；自定义 Context 上的工作仍需要相应运行循环推进。草案的消费入口是 `std::this_thread::sync_wait`，并非 `std::execution::sync_wait`；本仓库接入层可统一导出提供方的入口。

依据：[operation state](https://eel.is/c++draft/exec.opstate)、[异步生命周期 §7～8](https://eel.is/c++draft/exec.async.ops#7)、[sync_wait](https://eel.is/c++draft/exec.sync.wait)。

## 4. `then` 的本质：把 receiver 包装一层

### 4.1 一条管线如何展开

```cpp
auto source = ex::just(21)
    | ex::then([](int n) { return n * 2; });
```

可以按下面的方式理解，不需要先看模板实现：

```cpp
// 语义伪代码；不是实际 ABI 或内部类型布局。
ThenSender {
    upstream = JustSender{21};
    function = multiply_by_two;

    connect(final_receiver) {
        return ThenOperation{
            child = connect(upstream,
                ThenReceiver{function, final_receiver})
        };
    }
}

ThenOperation.start() {
    start(child);
}

JustOperation.start() {
    set_value(then_receiver, 21);
}

ThenReceiver.set_value(21) {
    result = multiply_by_two(21);
    set_value(final_receiver, result);    // 42
}
```

`ThenReceiver` 还负责转发上游的 error/stopped；它们不调用 `multiply_by_two`。当该用户函数可能抛异常时，算法捕获异常并发送 `set_error(exception_ptr)`，相应错误也出现在输出签名中。

```cpp
// 单个非 void 结果的语义伪代码；void 结果则 invoke 后 set_value(r)。
void ThenReceiver::set_value(auto&&... xs) && noexcept {
    try {
        ex::set_value(std::move(downstream),
            std::invoke(std::move(function), FORWARD(xs)...));
    } catch (...) {
        ex::set_error(std::move(downstream), std::current_exception());
    }
    // 完成通知可能销毁包含本 receiver 的状态，此后不再访问成员。
}
```

组合图写作 `source | then(f) | then(g)`；连接时从最终 receiver 向上包装；完成时从 source 向下传递。普通 `then` 的后续函数可以直接在发出上游完成通知的线程中运行，不会自动增加一次入队。

### 4.2 `then` 和 `let_value` 的区别

```cpp
source | ex::then([](int n) { return ex::just(n + 1); });
// 把这个新 sender 当成普通值发送，不自动执行它。

source | ex::let_value([](int n) { return ex::just(n + 1); });
// 创建、连接并启动返回的子操作，等待子操作完成，再向下游完成。
```

`then` 是值变换；`let_value` 是依赖上游结果，再展开一段异步计算。后者需要保管上游结果、动态选择的子操作及其生命周期；这是算法提供方的责任，重建时直接复用原生实现。

依据：[then / upon_error / upon_stopped](https://eel.is/c++draft/exec.then)、[let_value / let_error / let_stopped](https://eel.is/c++draft/exec.let)。

### 4.3 `when_all`：多个子操作的汇合

`when_all(a, b)` 连接并启动多个子操作，全部完成后才能完成父操作；成功时合并它们的值。这表示并发结构，不承诺实际并行：如果子操作都同步完成，完全可以在同一线程依次推进。

出现错误或停止时，算法可以请求其他子操作停止，但仍要等它们完成后再通知父 receiver。不能先把父操作销毁，让仍在运行的子操作继续访问其中的状态。这也是为什么取消必须是协议，而不是简单释放内存。

依据：[when_all](https://eel.is/c++draft/exec.when.all)。

## 5. 调度、环境、错误：三个独立问题

### 5.1 scheduler 决定在哪里继续，Context 负责真正推进

```cpp
auto source = ex::schedule(scheduler)
    | ex::then([] { return 42; });
```

针对本库的 Queue/Context，可以把调度实现理解为：

```cpp
// 语义伪代码；回调节点嵌在地址稳定的 operation state 内。
ScheduleOperation.start() noexcept {
    queue.enqueue(this->callback);
}

Context.run_one() {
    callback = queue.take();
    callback();
}

ScheduleOperation.callback() noexcept {
    // 取决于这个 sender 的取消契约，先清理注册和必要状态。
    if (stop_requested) set_stopped(move(receiver));
    else                set_value(move(receiver));
    // 下游 then 可以在这里立即运行；通知后不再访问 operation。
}
```

`schedule(sch)` 的成功通知没有值，发生在该 scheduler 对应的执行资源上。scheduler 常常只是指向 Context 的轻量入口；它的复制不意味着复制队列或拥有线程。Context 的有效期必须覆盖相关 scheduler、sender 和 operation 的使用。

组合算法中，`starts_on(sch, source)` 控制 source 在哪里启动；`continues_on(source, sch)` 控制后续完成转移到哪里。切换到另一个线程并非所有 sender 的固有属性。

### 5.2 environment 是从调用方传入的查询接口

```text
上下文查询： final receiver → 包装 receiver → 上游操作
完成通知：   上游操作 → 包装 receiver → final receiver
```

receiver 的 `get_env()` 给出调用方的执行环境；包装器按各查询的转发规则传递或覆盖它。sender 的 `get_env()` 表示 sender 自身属性，例如 value completion scheduler；二者用途不同。

停止令牌表达“调用方请求取消”，allocator 表达“可用的分配策略”，scheduler 表达执行资源。查询到 allocator 不会让任意 `new`、`make_unique` 或协程帧自动使用该 allocator，具体设施必须显式接入。

### 5.3 error 和 stopped 都是完成，不是消失

| 算法 | 处理哪种上游完成 | 回调返回什么 | 输出行为 |
| --- | --- | --- | --- |
| `then` | value | 普通结果 | 发送新的 value |
| `upon_error` | error | 普通结果 | 将错误恢复为 value |
| `let_error` | error | sender | 运行恢复子操作，再转发其结果 |
| `upon_stopped` | stopped | 普通结果 | 将 stopped 恢复为 value |
| `let_stopped` | stopped | sender | 运行恢复子操作，再转发其结果 |

未匹配的通道通常原样转发。比如 `source | then(f) | then(g)` 中，source 发出 error，`f/g` 都不运行，但终端 receiver 仍必须收到 error。后续状态可能早已在连接阶段构造；“跳过函数”不等于“后续全部未注册”，更不等于“整条操作永不完成”。

结合现有 `exec::ext::from_expected`，语义是：

```text
expected<T, error_code> 有值 → set_value(T) → 后续 then
expected<T, error_code> 错误 → set_error(error_code) → 跳过 then
                                               └─ let_error 可恢复为 value
```

直接 `then([] { return expected<...>{...}; })` 只会发送一个 expected 值，不会自动拆成两个通道。`exec::ext` 就是进行这种业务结果适配的位置。错误码形式的工作函数可以是 `noexcept`，但整条管线是否可能产生 `exception_ptr`，还取决于后续函数及存储操作；不要把局部保证扩大为全图保证。

停止请求是协作协议；它不会自动撤销所有回调、销毁 operation 或关闭 Queue。设施需要定义取消与正常完成竞争时谁获胜，并只发出一次终止通知。

依据：[scheduler](https://eel.is/c++draft/exec.sched)、[schedule](https://eel.is/c++draft/exec.schedule)、[starts_on](https://eel.is/c++draft/exec.starts.on)、[continues_on](https://eel.is/c++draft/exec.continues.on)、[get_env](https://eel.is/c++draft/exec.get.env)、[恢复算法](https://eel.is/c++draft/exec.let)。

## 6. 协程是编译器生成的另一种操作状态机

execution 管线把状态显式放进 operation state；协程由编译器把跨挂起点的状态放进 coroutine frame。两者都需要外部执行资源推进；单独写 `co_await` 不会创建线程或运行循环。

```cpp
Task<int> work() {
    int n = co_await read_number();
    co_return n + 1;
}
```

下面是主路径的语义结构，`AWAIT` 表示第 7 节的展开；它不是可编译的普通函数改写：

```cpp
work_call() {
    frame = ALLOCATE_FRAME();              // 可能按规则省略分配
    COPY_PARAMETERS_INTO_FRAME();
    CONSTRUCT_PROMISE_IN_FRAME();
    result_object = promise.get_return_object();
    initial_await_resume_entered = false;

    // 以下逻辑属于可挂起状态机；不是在普通函数栈上一路执行。
    try {
        AWAIT_INITIAL(promise.initial_suspend());
        // AWAIT_INITIAL 在调用其 await_resume 前立即将上述标志置 true。
        int n = AWAIT(read_number());
        promise.return_value(n + 1);
        DESTROY_BODY_LOCALS();
    } catch (...) {
        // initial await 尚未进入 await_resume 时的异常向调用方传播。
        if (!initial_await_resume_entered) throw;
        promise.unhandled_exception();
    }
    AWAIT(promise.final_suspend());

    // 调用者在首次向其交还控制时取得 result_object。
    // 若 final await 保持挂起，frame 等待合法的 destroy() 才释放。
}
```

这是便于阅读的轮廓：创建阶段失败有独立清理路径；若执行最终不保持挂起，也可以走自动销毁路径。完整边界规则和展开见 [协程语义伪代码](../standalone/tests/lessons/README.md#coroutine-semantic-pseudocode)，无需按 lessons 顺序学习。

`promise` 不是线程，也不是 `std::promise`。它是协程返回类型选定的协议对象，决定返回对象、初始策略、最终策略和结果保存方式。

- `co_return value`：调用 `return_value(value)`；`co_return;` 调用 `return_void()`。同一 promise 不能同时声明两者；`co_return` 的 void 表达式形式先求值再调用 `return_void()`。
- `co_yield value`：等价于 `co_await promise.yield_value(value)`；该隐式 await 不额外经过 `await_transform`。
- `final_suspend()`：函数体结束、局部变量清理后进入。可以保存 frame 并通过最终 awaiter 恢复父协程；它不是 `co_return` 已经销毁了 frame。
- `coroutine_handle<P>`：指向该 frame 的非拥有句柄。`resume()` 推进状态机；`destroy()` 销毁 frame；两者必须在合法状态下调用，不能对 final-suspended 协程再次 `resume()`。

依据：[协程定义](https://eel.is/c++draft/dcl.fct.def.coroutine)、[co_return](https://eel.is/c++draft/stmt.return.coroutine)、[co_yield](https://eel.is/c++draft/expr.yield)、[句柄恢复](https://eel.is/c++draft/coroutine.handle.resumption)。

## 7. 一次 `co_await` 到底展开成什么

```cpp
// 语义伪代码。ordinary co_await E；initial/final/yield 有转换例外。
awaitable = TRANSFORM_IF_PRESENT(promise.await_transform, E);
awaiter = SELECT_OPERATOR_CO_AWAIT_OR_SELF(awaitable);
h = HANDLE_OF_CURRENT_COROUTINE();        // 类型为 coroutine_handle<Promise>

if (!bool(awaiter.await_ready())) {
    SAVE_CONTINUATION_AFTER_THIS_AWAIT();
    MARK_CURRENT_COROUTINE_SUSPENDED();    // 调用 await_suspend 前已逻辑挂起

    // 仅执行下列与返回类型匹配的一种：
    void:   awaiter.await_suspend(h);        HAND_BACK_CONTROL();
    bool:   if (awaiter.await_suspend(h))    HAND_BACK_CONTROL();
            else                            CONTINUE_CURRENT_COROUTINE();
    handle: next = awaiter.await_suspend(h); RESUME_NEXT_AND_HAND_BACK(next);
}

// await_ready 为 true、bool false，或后来被恢复时，到达这里。
result = awaiter.await_resume();          // 可能返回 void，也可能抛异常
```

`HAND_BACK_CONTROL` 表示本次恢复调用结束；未来恢复时从保存的继续点执行，**不会重新执行本次 `await_ready/await_suspend`**。表中把 bool 写成控制结构只是方便说明，实际是编译器选择对应分支。

| 条件或返回形式 | 当前协程如何继续 |
| --- | --- |
| `await_ready() == true` | 跳过 `await_suspend`，执行 `await_resume` |
| ready 为 false，suspend 返回 `false` | 恢复当前协程，立即进入 `await_resume` |
| ready 为 false，suspend 返回 `true` 或 `void` | 没有隐式恢复；由外部安排继续执行 |
| ready 为 false，suspend 返回另一个 handle | 恢复那个 handle；当前协程仍需另行恢复 |
| `await_suspend` 抛异常 | 恢复当前协程并重新抛出该异常，跳过本次 `await_resume` |

`void/true` 并不保证协程在 `await_suspend` 返回时仍未恢复：该函数可以发布 handle，其他线程可能已经恢复乃至销毁 frame。因此发布后不能继续依赖 awaiter 成员仍然存活。

对于**一次动态求值**，ready 正常到达时求值一次；suspend 在 ready 为 false 时至多一次；resume 可能因销毁或异常而不执行。同一源代码行在循环中可以被求值多次。

`await_resume` 提供 `co_await` 表达式的结果，可以返回另一个 awaitable，但不会自动递归等待它；还需另一次 `co_await`。`h.resume()` 的意思是从状态机继续点推进，执行过程中才走到这条 `await_resume`，不是把 `resume()` 直接绑定成调用某个 awaiter 方法。

最终 awaiter 返回父协程 handle，也只是在这里使用 handle 分支做控制转移；返回 `suspend_always` 则通过它的 void `await_suspend` 保持当前 frame 挂起、交还控制。这里不存在“空 handle 的 await_ready”；空 handle 不能拿来恢复，`noop_coroutine()` 是另一种有效的无操作句柄。

依据：[expr.await §3～5](https://eel.is/c++draft/expr.await#3)。

## 8. `co_await sender`：把 receiver 完成转成协程恢复

sender 不天然等于 awaiter。promise 的 `await_transform` 或 execution 的协程适配机制创建桥接 awaiter，再由该 awaiter连接并启动 sender。

```text
协程执行到 co_await sender
    → awaiter 保存当前协程 handle
    → connect(sender, 桥接 receiver)，operation 保存在 awaiter/frame 中
    → start(operation)
    → sender 完成，receiver 保存 value/error/stopped
    → 立即继续，或安排恢复协程
    → await_resume 取出结果，后续协程代码继续
```

困难在于 `start` 可能同步完成，也可能与挂起过程并发竞争。以下 gate 是并发握手语义，不是一个普通 bool：

```cpp
// 语义伪代码。错误清理、值存储失败及 stopped 策略在此省略。
bool await_suspend(handle parent) {
    saved_parent = parent;
    op = CONNECT_IN_PLACE(sender, BridgeReceiver{this});
    start(op);                         // 可能在这里已经收到完成
    return gate.commit_suspend();      // 已完成则 false，否则提交等待
}

BridgeReceiver.complete(result) noexcept {
    STORE_RESULT(result);
    auto continuation = COPY_RESUME_TARGET();
    if (gate.publish_completion_and_claim_resume()) {
        RESUME_OR_ENQUEUE(continuation);
    }
    // 发布允许并发恢复/销毁；之后不得访问 awaiter/frame。
}

await_resume() {
    return TAKE_VALUE_OR_RETHROW_STORED_ERROR();
}
```

跨线程握手需要正确的原子状态与内存序；不能简单写成“receiver 总是 `parent.resume()`，await_suspend 总是 `return true`”。还必须处理连接失败、结果存储失败、恢复线程和 owner 的销毁责任。

另一方向也成立：可以把整个协程包装成 sender；连接时创建拥有 frame 的 operation，start 时恢复协程，协程结束时向外层 receiver 发出完成。这样协程体可用顺序语法编写，外部仍按同一 execution 协议组合。

协程语言本身没有独立的 stopped 返回通道；这是 promise/桥接协议的策略。当前 standalone 桥接会把 sender 的 `error_code` 保存成 `system_error` 异常，在 `await_resume` 时重新抛出；stopped 则沿任务停止路径完成，而非假装有一个正常返回值。因此 `from_expected` 的错误码协议不自动保证 `co_await` 不抛异常。

依据：[标准 awaitable 接入](https://eel.is/c++draft/exec.awaitable)、[标准 connect 的 awaitable 接入](https://eel.is/c++draft/exec.connect)。当前实现核对点：[sender_awaiter.h](../standalone/include/async/core/coroutine/details/sender_awaiter.h)、[awaitable.h](../standalone/include/async/core/coroutine/details/awaitable.h)。

## 9. 对本库重建意味着什么

| 层 | 本库要做的事 | 应复用的事 |
| --- | --- | --- |
| execution 接入 | 集中公共入口、隔离扩展、记录与标准的差异 | stdexec 协议、then/let/when_all 等原生算法 |
| Queue / Context | 节点排队、并发、唤醒、推进、关闭契约 | 标准线程和同步设施 |
| scheduler / timer / signal | 用 receiver 通知完成，处理取消、竞争和状态寿命 | 原生 completion/env 查询与组合算法 |
| coroutine | frame 所有权、promise、结果、继续点、sender 互操作 | 编译器协程状态机和公共适配协议 |
| `exec::ext` | expected/error_code 等附加策略 | 原生 then/let_error 等控制流 |

内存问题按**谁创建、谁持有、何时释放**检查：

- 已连接操作中的 callback 可以内嵌在 operation state，不一定需要额外分配一个 record。
- `post(fn)` 没有外部 owner 维持 operation，可能需要自行分配 record；Queue/Context 的 PMR 资源可为它提供统一分配策略，执行和丢弃两条路径都要释放。
- operation state 的存储由连接它的 owner 管理；不能因为 Queue 持有一个 resource，就假定所有 operation 也改用了它。
- coroutine frame 按语言规定的分配查找规则创建；需要通过 promise 的分配入口显式接入资源。稍后在 start 才注入的环境，不会追溯改变已经发生的 frame 分配。
- `memory_resource*` 是非拥有指针，资源必须活到其所有分配释放完成；跨线程释放还要求资源满足对应并发条件。

因此，接下来读代码时，每个类型只追问四件事：**它实现哪个协议入口？状态归谁拥有？谁推动它完成？value/error/stopped 如何向下传递？** 先沿一条 `schedule → then → terminal receiver` 追踪，再沿一条 `coroutine → await sender → receiver → resume` 追踪，就能覆盖主要设计。

## 10. 定义与实现索引

这些是查定义的入口，不要求顺序通读。eel.is 是滚动工作草案镜像；段落编号和接口可能随草案更新，本文按上述日期查阅。

- [execution 总入口](https://eel.is/c++draft/exec)、[异步操作/生命周期/完成签名](https://eel.is/c++draft/exec.async.ops)。
- [receiver](https://eel.is/c++draft/exec.recv)、[sender concepts](https://eel.is/c++draft/exec.snd.concepts)、[connect](https://eel.is/c++draft/exec.connect)、[operation state/start](https://eel.is/c++draft/exec.opstate)、[scheduler](https://eel.is/c++draft/exec.sched)。
- [then 与 upon 系列](https://eel.is/c++draft/exec.then)、[let 系列](https://eel.is/c++draft/exec.let)、[get_env](https://eel.is/c++draft/exec.get.env)。
- [协程函数的正式展开](https://eel.is/c++draft/dcl.fct.def.coroutine)、[co_await](https://eel.is/c++draft/expr.await)、[co_yield](https://eel.is/c++draft/expr.yield)、[co_return](https://eel.is/c++draft/stmt.return.coroutine)。
- [stdexec 上游](https://github.com/NVIDIA/stdexec)、[仓库依赖版本](../stdexec/deps.json)、[当前协议接入](../stdexec/include/async/core/protocol/operations.h)、[当前原生算法导出](../stdexec/include/async/execution.h)。

当前 stdexec 接入使用的标记名、扩展及查询形式不保证与滚动草案逐字一致。例如现有 `split` 来自 `<exec/split.hpp>` 扩展，不应在未来切换标准库时假定它一定能原样替换。正式实现只依赖提供方公共 API，不把本文伪代码当作另一套 execution 引擎的实现清单。
