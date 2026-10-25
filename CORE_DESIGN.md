# Async Core 设计参考

本文是 async runtime 开始实现前的设计约束和参考资料。它描述目标和边界，不表示相关功能已经实现。实现过程中如果需要改变这里的方案，应先更新依赖图、资源契约和验收条件，再修改代码。

## 1. Core 要解决什么问题

async 的 core 是一个可嵌入、可由调用方驱动的协程运行时。它不负责定义 sender/receiver 算法，也不应要求使用者采用某个线程模型。它需要提供以下能力：

1. 在调用方指定的线程或 scheduler 上推进 ready operation。
2. 将后台工作、I/O 或 timer 的完成恢复到指定 scheduler。
3. 让调用方可以区分通信/驱动开销和最终业务回调开销。
4. 对 async 自己拥有的动态存储给出可追踪的资源来源。
5. 支持取消、关闭、到期和完成之间的竞争，并保证已启动操作恰好完成一次。
6. 允许 coroutine task、timer、queue 和后续 I/O 后端使用同一套静态协议。
7. 允许 execution 层把 core operation 适配成 sender，但 core 不依赖 execution。

core 不提供硬实时保证。`poll_one` 可以限制一次从 ready queue 取出的调度记录数量，但不能抢占已经开始运行的 C++ 函数、协程段、析构函数或用户回调。用户 awaitable 也可能通过 `await_suspend` 直接恢复另一个 coroutine handle，所以只有 core 自己定义的 scheduler 边界可以提供执行范围约束。

## 2. 不变量

以下不变量比具体类名更重要：

- core 的头文件不能包含 `async/execution/*`、stdexec 或 `<execution>`。
- execution 适配器可以依赖 core；反向依赖会形成架构倒置。
- 公共 core 类型不通过继承复用行为。优先使用模板参数、concept、CPO、ADL 和组合对象。
- `std::pmr::memory_resource` 是唯一预留的运行时虚函数边界。它是标准的资源接口，不是 async 自己定义的基类。
- queue 节点、timer 节点和 operation state 默认由调用方嵌入并借用，不因统一性而强制堆分配。
- 需要自行拥有生命周期的记录，例如 `post(fn)` 的闭包，必须明确使用哪个 resource 分配，并在完成、取消、拒收和关闭路径回收。
- 跨 scheduler 恢复必须通过目标 scheduler 的 enqueue/post 协议。不能把一个目标 scheduler 的 handle 直接作为当前 `await_suspend` 的对称转移结果。
- 完成可能同步发生。receiver 或用户回调可以销毁 operation；完成调用返回后不得继续访问已完成 operation 的成员。
- stop、cancel、close 和 destroy 是不同动作。丢弃节点不能代替完成通知。

## 3. 依赖分层

建议的依赖方向如下：

```text
公共配置/词汇类型
        |
        +--> async/core/memory
        +--> async/core/cancellation
        +--> async/core/scheduling
        +--> async/core/timer
        +--> async/core/coroutine

std::execution/stdexec --> async/execution/*
async/core/* + async/execution/* --> async/adapters/*
```

更具体的目录职责：

| 层 | 责任 | 可以依赖 | 不应依赖 |
| --- | --- | --- | --- |
| `async/config.h` | 公共宏和可见性 | 标准头 | execution、运行时实现 |
| `core/memory` | `memory_resource` 引用、对象分配辅助、资源策略 | `<memory_resource>` | queue、timer、execution |
| `core/scheduling` | ready queue、scheduler、event loop、wake/wait | memory、标准同步设施 | sender 算法、具体 coroutine promise |
| `core/timer` | clock、timer node、deadline index | memory、intrusive node | queue 的实现细节、sender |
| `core/cancellation` | stop state、registration、取消竞态 | memory、标准 stop vocabulary | execution 算法 |
| `core/coroutine` | task、promise、frame allocation、continuation | scheduling、memory、语言 coroutine | stdexec 算法 |
| `execution` | sender/receiver、组合算法和 ext | stdexec 或标准 execution | core 的底层实现细节 |
| `adapters` | 把 core operation/scheduler/task 接到 execution 协议 | core、execution/protocol | 被 core 反向包含 |

`core.h` 只聚合纯 core 入口。`execution.h` 只聚合 execution 入口。独立的 `adapters/` 层可以同时包含两者，但不能被任一底层层级反向包含。`async.h` 可以作为总入口聚合 core、execution 和 adapters；纯 core 用户应能只包含 `async/core.h` 编译。

如果某个定义同时被 core 和 adapters 需要，应先判断它是不是运行时本身的词汇：scheduler handle、enqueue record、timer operation 和 cancellation state 可以上提到 `core`；sender、receiver、completion signatures、`connect` 和 `start` 只能留在 `execution`。不能因为 adapters 需要某个 sender 类型，就把 execution protocol 放进 core。

## 3.1 CPO 和 ADL

### CPO

CPO（Customization Point Object，定制点对象）是一个对象，它的 `operator()` 提供稳定调用入口，再在内部按规定顺序寻找具体类型的定制实现。典型形式是：

```cpp
namespace lcf::async::core {

struct enqueue_t {
    template<class Scheduler, class Record>
    constexpr auto operator()(Scheduler& scheduler, Record& record) const
        noexcept(/* 由定制实现决定 */)
        -> decltype(/* 选择的 enqueue 定制 */);
};

inline constexpr enqueue_t enqueue{};

} // namespace lcf::async::core
```

调用方只写 `core::enqueue(scheduler, record)`，不需要知道实现是 scheduler 的成员函数、同命名空间的非成员函数，还是内部 fallback。标准库的 `std::ranges::begin` 和 execution 的 `connect`、`start`、`set_value` 都采用这一类协议。

CPO 的价值是把扩展点集中在一个对象上：可以使用 concepts 限制合法类型，可以统一处理 `noexcept` 和返回类型，也不会把一组同名自由函数全部暴露给普通重载解析。CPO 不等于虚函数；满足协议的类型仍然由模板静态分发。

### ADL

ADL（Argument-Dependent Lookup，参数相关查找）是 C++ 的非限定函数查找规则。调用：

```cpp
enqueue(scheduler, record);
```

时，编译器除了当前作用域，还会根据 `scheduler` 和 `record` 的类型查找它们所属命名空间中的 `enqueue`。因此第三方类型可以在自己的命名空间提供定制函数，而不需要修改 async 的头文件。

ADL 适合做协议的窄入口，但裸露的非限定调用容易被无关同名函数干扰。core 应把 ADL 放在受约束的 CPO 实现内部，先用 concepts/检测表达式确认候选，再调用；公共代码直接调用 CPO 对象。execution 的 sender CPO 继续由标准库/stdexec 提供，core 自己只定义 runtime CPO，例如 `enqueue`、`cancel` 或 `poll_one`，两组 CPO 不混用。

### async 的协议归属

| 协议 | 定义位置 | 例子 | 允许谁依赖 |
| --- | --- | --- | --- |
| core runtime CPO | `core` | `enqueue`、`poll_one`、timer 操作、取消请求 | core、adapters |
| execution CPO | `execution`/提供方 | `connect`、`start`、`set_value`、`set_error` | execution、adapters |
| bridge adapter | `adapters` | core scheduler -> execution scheduler/sender | adapters |

## 4. 调度记录和 event loop

### 4.1 最小调度单位

ready queue 的元素不是 coroutine handle，而是一个具有明确生命周期的 operation/record。一个 record 至少需要一个静态可调用入口：

```cpp
template<class Record>
void run_record(Record&) noexcept;
```

实现可以是 intrusive node 加模板函数；若一个异构队列确实需要类型擦除，边界处使用 `void*` 加函数指针即可，不建立 `operation_base` 虚基类。

`poll_one` 的契约应是：非阻塞地取出至多一个 ready record，执行该 record 的入口，然后返回实际执行数量。record 内部可以恢复协程并运行到下一个挂起点，因此这不是“一次 `resume()`”或“一次 `co_await`”的承诺。

如果需要把终结操作的开销从通信操作中分开，完成路径必须显式把终结 record 放入目标 event loop；不能因为当前线程恰好相同而隐式 inline。这样调用方可以分别测量：

```text
worker/I/O completion -> loop enqueue/wake -> loop final record
```

event loop 的构造应组合 queue、timer index、clock 和 wake policy：

```cpp
template<class ReadyQueue, class TimerIndex, class Clock, class WakePolicy>
class basic_event_loop;
```

queue 不反向持有 timer service，timer index 不调用 callback，event loop 负责锁、等待、关闭、预算和两者之间的协调。

### 4.2 并发模型

第一版应明确采用“单 consumer、多 producer”还是“单线程 queue”。event loop 所在线程是唯一 consumer，后台线程只 enqueue 和 wake。这样可以把复杂度集中在 enqueue/wake 的内存序和关闭竞态，而不需要一开始实现通用 MPMC queue。

若 record 嵌入 operation state，enqueue 不分配内存。若 `post` 接收临时闭包并需要在调用返回后继续存活，则建立显式 owning record：它保存调用时的 resource、大小和对齐，完成前不释放。

### 4.3 PollBudget 和 PollReport

归档 standalone 的：

```cpp
PollReport poll(PollBudget budget = {});
```

相比 Asio 的 `poll_one()`/`poll()` 只返回执行数量，这组 API 更适合由上层控制 event loop。归档的 `PollBudget` 同时包含最大 callback 数量和最大持续时间，`PollReport` 返回实际 callback 数量、耗时，以及数量/时间限制是否达到。它把“这次驱动做了多少工作”作为可观察结果暴露给调用方，适合渲染帧、主循环或服务端的软预算。

建议保留这一组语义：

- `poll_one()` 是最小的非阻塞调度原语，最多取出一个 ready record；
- `poll(PollBudget)` 在每个 record 前检查数量和时间预算，调用 `poll_one()`，完成后更新 report；
- `m_callbacks` 只统计已经开始并返回的 record，不统计入队数量；
- `m_elapsed` 包含本次驱动中的 queue/timer 维护、record 执行和必要清理；
- 时间限制是软边界。一个已经开始的 callback、同步协程段或析构函数不能被抢占，可能使最终耗时超过预算；
- 没有 ready record 时提前返回，不能为了达到时间预算而等待；
- `poll()` 的计时应通过可替换的 `Clock` 或诊断策略实现，正常低开销路径不能被强制的高精度计时拖慢。

`PollReport` 的字段和计数规则属于运行时 API；具体的计时实现、timer 维护批量和队列批量仍属于可优化策略。benchmark 必须分别测量空轮询、达到 callback 上限、达到时间上限、callback 超预算和清理成本，不能只看总任务耗时。

### 4.4 参考实现

- Asio `io_context::poll_one` 的定义是“最多执行一个 ready handler”，这与 record 粒度的契约相符，但它没有归档 `PollBudget/PollReport` 这样的组合预算和可观测结果，handler 内部仍可继续同步调用其他代码：<https://think-async.com/Asio/asio-1.30.2/doc/asio/reference/io_context/poll_one.html>
- Asio `post` 明确保证函数对象入队，返回前不会在当前线程调用：<https://think-async.com/Asio/asio-1.30.2/doc/asio/reference/post/overload1.html>
- Asio 的 intrusive `op_queue` 只保存 next 指针并负责 push/pop/destroy，可参考其无额外节点分配的结构：<https://github.com/chriskohlhoff/asio/blob/asio-1-30-2/asio/include/asio/detail/op_queue.hpp>
- libuv 的 event loop 将 timer、pending callbacks、I/O poll、close callback 分成阶段；它还明确 `UV_RUN_ONCE`/`UV_RUN_NOWAIT` 的阻塞差异：<https://docs.libuv.org/en/v1.x/design.html>、<https://docs.libuv.org/en/v1.x/loop.html>
- libuv `uv_async_send` 会合并多次通知，所以通知计数不能当作 callback 计数：<https://docs.libuv.org/en/v1.x/async.html>
- cppcoro 的 `static_thread_pool::schedule_operation` 展示了嵌入 coroutine handle 和 next 指针的 awaiter；它适合作为静态 operation 的参考，不应照搬其默认资源行为：<https://github.com/lewissbaker/cppcoro/blob/master/include/cppcoro/static_thread_pool.hpp>

## 5. Timer 的职责和实现选择

### 5.1 需求

timer 需要支持：

- 单次 deadline 和可选重复周期；
- 插入、取消、重新安排；
- 读取下一个 deadline 供 event loop 计算 wait timeout；
- 到期后转移到 ready queue，而不是在 timer index 内直接执行用户代码；
- 到期与取消只有一个胜者；
- event loop 可以限制一次维护和转移的数量，避免一个时间点有大量 timer 时形成无界工作。

timer index 的 API 应接近：

```cpp
insert(timer_node&) -> bool        // 是否改变 next deadline
remove(timer_node&)                // O(log n) 或策略规定的复杂度
next_deadline() const noexcept
expire(now, sink)                  // 只产生到期 node，不执行回调
```

节点由调用方嵌入，包含 deadline、index 或 bucket link、状态位和一个完成入口。index 不持有 `Queue&`，也不自行睡眠。

### 5.2 初始实现建议

第一版使用侵入式二叉 min-heap：

- `timer_node` 中保存 heap index，取消和重排可在 O(log n) 内完成；
- heap 的 backing array 使用调用方提供的 `pmr::memory_resource`，或者由固定容量策略提供；
- `expire` 只弹出到期节点并交给 event loop；
- event loop 再把节点放入 ready queue；
- 使用注入的 `Clock`，测试可使用 fake clock；
- 不在 timer callback 内持有 heap 锁，也不在锁下调用 receiver。

当 workload 主要是大量短周期 timer 并且取消率很高时，再评估分层 timing wheel。它可以把部分操作降到近似 O(1)，但需要处理 tick 精度、跨桶迁移和时间回绕，第一版不应同时实现两套。

### 5.3 参考实现

- Asio `timer_queue` 使用 heap index、intrusive timer data 和 ready operation queue；取消会移除 timer 并将完成操作放入 operation queue：<https://github.com/chriskohlhoff/asio/blob/asio-1-30-2/asio/include/asio/detail/timer_queue.hpp>
- libuv timer 使用二叉 min-heap，节点同时有 heap/queue 状态；到期 timer 先进入临时 ready queue，再调用 callback：<https://github.com/libuv/libuv/blob/v1.48.0/src/heap-inl.h>、<https://github.com/libuv/libuv/blob/v1.48.0/src/timer.c>
- libuv 文档明确重复 timer 是相对 event loop 的 `now` 重置，不会自动扣除 callback 开销：<https://docs.libuv.org/en/v1.x/timer.html>
- Seastar 的 `timer_set` 使用侵入式 bucket 结构，并针对“经常取消或重排”的 timer 优化；这是 timing wheel/分层桶的参考：<https://github.com/scylladb/seastar/blob/master/include/seastar/core/timer-set.hh>

### 5.4 数据结构必须由 benchmark 决定

二叉 heap 只是便于验证语义的初始实现，不应因为理论复杂度就固定为最终实现。候选结构包括：

| 结构 | 适合的 workload 假设 | 需要实测的代价 |
| --- | --- | --- |
| intrusive binary min-heap | deadline 分布广、需要准确取消/重排 | O(log n) 更新，连续到期时可能形成 burst |
| ordered tree/map | timer 数量小、实现简单、需要有序遍历 | 节点局部性和每节点额外指针成本 |
| 分层 timing wheel/bucket | 大量近似 tick、短周期 timer、取消率高 | tick 精度、跨桶迁移、时间回绕和边界复杂度 |
| calendar queue/自适应桶 | deadline 分布稳定、规模较大 | 分布变化时需要重建或调整桶宽 |

benchmark 至少覆盖 active timer 数量、插入/取消/重排比例、deadline 分布、同一 tick 的到期数量、重复 timer、fake clock 推进幅度、跨线程取消和 event loop 预算。记录插入、取消、到期转移、next deadline 查询的吞吐和 p50/p99 延迟，以及每个 timer 的字节数、上游分配次数和 wake 次数。只有在目标 workload 上有数据，才选择 heap、bucket 或其他结构；公共 `timer_index` 协议不应暴露具体容器类型。

## 6. 内存资源和分配策略

### 6.1 为什么只用 `memory_resource` 不够

`std::pmr::memory_resource` 解决的是“从哪里取得一块指定大小和对齐的存储”，并不决定：

- 哪些对象应共享一个资源；
- 对象何时释放；
- 是否使用 arena、pool、slab 或 freelist；
- 一个资源是否可以跨线程访问；
- coroutine frame 如何在函数调用时取得资源；
- 不同类型的 operation 是否需要不同的容量和回收策略。

因此 async 不应再定义一个带虚函数的 `allocation_strategy` 基类。建议把两个问题分开：

```text
memory_resource       实际分配/释放的字节
allocation policy     资源绑定、对象构造、回收时机和容量规则
```

policy 是模板参数或具体值类型。例如：

```cpp
template<class Policy, class T, class... Args>
T* allocate_object(Policy&, Args&&...);

struct resource_ref {
    std::pmr::memory_resource* value; // non-owning
};
```

`resource_ref` 只是轻量的非拥有指针和检查，不是继承层次。所有分配出的 record 必须保存释放所需的 resource、字节数和对齐，或由 policy 保证这些值可重建。

### 6.2 推荐的资源类别

| 资源/策略 | 适用对象 | 优点 | 限制 |
| --- | --- | --- | --- |
| `synchronized_pool_resource` | 多线程共享的固定大小 operation | 标准实现、减少上游分配 | 有同步成本，仍需明确生命周期 |
| `unsynchronized_pool_resource` | 单线程 event loop 的 record | 无锁、适合 loop 本地 | 不能被多个线程同时访问 |
| monotonic/arena | 一批 operation 同时建立、整体销毁 | 分配快，释放简单 | 不适合逐个取消并立即回收 |
| 固定块/slab policy | 同尺寸 timer 或 queue node | 可预测、无碎片 | 容量耗尽策略必须明确 |
| recycling/free-list policy | 高频短命 record | 可复用同尺寸块 | 跨线程回收需要 owner 或同步 |
| `new_delete_resource` | 显式兼容模式 | 最简单 | 不能作为隐藏默认，且不提供隔离 |

严格模式下 event loop 构造时必须收到 resource 或容量策略；便捷构造可以显式命名为 `with_default_resource`，不能在深层 operation 内悄悄调用 `get_default_resource()`。

### 6.3 coroutine frame 的特殊性

协程 frame 可能在 coroutine 函数调用时就分配，早于 `connect`、`start` 或 receiver environment 的注入。因此不能在 `start()` 中设置 PMR 后声称已经控制 root frame。

可行的入口是：

1. 协程函数显式接受 frame allocation context，使 promise 的 `operator new(size, context, ...)` 能捕获 resource。
2. 提供 task factory，在调用 coroutine 函数前绑定 frame policy。
3. 对子协程创建明确传播 context；不能依赖跨线程 TLS scope。
4. 对编译器消除 frame 的情况不作“必定调用 allocate/deallocate”断言。

promise 中只组合具体策略和 `ReturnStorage<T>` 一类的 detail helper。不要建立所有 task 都继承的 `promise_base`。

### 6.4 参考实现和取舍

- 标准 `memory_resource` 规定 `allocate/deallocate/is_equal` 及对齐语义；`polymorphic_allocator` 只是把资源传给 allocator-aware 对象：<https://eel.is/c++draft/mem.res>、<https://eel.is/c++draft/mem.poly.allocator>
- 标准 pool resource 区分 synchronized 和 unsynchronized，并允许通过 upstream resource 分批取得内存；这支持“调用方控制上游、策略控制局部复用”的组合：<https://eel.is/c++draft/mem.res.pool>
- Asio 的 associated allocator 让每个 handler 提供自己的 allocator，且保证异步 operation 对 handler 的释放先于 handler 调用；这是 operation/handler 资源传播的成熟参考：<https://think-async.com/Asio/asio-1.30.2/doc/asio/overview/core/allocation.html>
- Asio 的协程 awaitable 有独立的 frame recycling 机制，说明 coroutine frame 策略和 handler allocator 不能混为一个接口：<https://github.com/chriskohlhoff/asio/blob/asio-1-30-2/asio/include/asio/impl/awaitable.hpp>
- libuv 只提供进程级 `uv_replace_allocator`，并要求 allocator thread-safe；它适合说明全局替换的边界，不满足 async 对每个 loop/operation 的隔离需求：<https://docs.libuv.org/en/v1.x/misc.html>
- LLVM 的 `BumpPtrAllocator` 适合批量生命周期，`RecyclingAllocator` 展示了固定尺寸对象的回收缓存；两者都应作为 policy 的实现参考，而不是 async 公共基类：<https://llvm.org/docs/ProgrammersManual.html#pointers-and-allocations>、<https://llvm.org/doxygen/classllvm_1_1RecyclingAllocator.html>
- Boost.Context 的 stack allocator 说明“分配器作为值类型策略”可以控制上下文栈的 allocate/deallocate，而无需虚拟基类：<https://www.boost.org/doc/libs/1_85_0/libs/context/doc/html/context/stack.html>
- EASTL 的 allocator 是可替换的具体 allocator 对象，并支持对齐参数；可参考其调试名称和显式分配接口：<https://github.com/electronicarts/EASTL/blob/master/include/EASTL/allocator.h>

### 6.5 分配优化必须由 benchmark 决定

资源模型确定“所有 async-owned storage 都有明确来源”，但不预先确定唯一最快的 policy。以下策略都应作为可替换实现进行比较：

- 直接调用传入的 `memory_resource`；
- `unsynchronized_pool_resource` 用于单线程 loop 本地的固定尺寸 record；
- `synchronized_pool_resource` 或线程安全 slab 用于多线程共享路径；
- monotonic/arena 用于一批 operation 同时建立、整体释放的 scope；
- 针对 timer node、queue record、coroutine frame 的固定块 recycling/free-list；
- inline storage 加 upstream resource 的小对象优化。

allocator benchmark 至少应分离以下成本：一次分配/释放、构造/析构、对齐分配、构造失败回收、跨线程释放、资源共享竞争、upstream 访问次数、峰值保留内存和缓存局部性。测试 workload 应包含空 loop、单个 operation 高频重复、并发 enqueue、timer burst、协程 frame 创建/销毁、取消和关闭 drain；不能只用一个 `allocate/deallocate` 微基准决定策略。

优化结果需要同时报告吞吐、p50/p99 延迟、每个 operation 的字节数和上游分配次数。若一个策略只在单线程或固定 timer 数量下更快，应通过模板 policy 或显式配置选择，不改变基础资源契约，也不能把它偷偷设为所有场景的默认实现。benchmark 结果应作为提交和 CI 的版本数据，用来发现劣化，而不是把一次机器上的最快结果写成 API 语义。

### 6.6 资源契约必须覆盖的路径

实现每一个 owning 类型时，必须列出以下路径的分配/释放：

```text
正常完成
同步完成
await_suspend 失败
connect/start 失败
取消
event loop 拒收
event loop 关闭
receiver 在完成回调中销毁 operation
构造函数抛出
跨线程释放
```

“Context 有一个 PMR 参数”不能推出进程内所有分配都由它控制。用户回调、值类型内部存储、异常对象、第三方 sender 算法、线程和系统同步对象都要单独声明边界。

## 7. Coroutine task 和 awaitable 扩展

### 7.1 task 的边界

`task<T, FramePolicy>` 应是具体 promise 的返回对象。promise 组合：

- `ReturnStorage<T>`；
- continuation handle；
- scheduler/context 引用；
- stop token 或 cancellation registration；
- frame policy 的释放信息。

这些成员不需要公共 promise 基类。公共操作使用 detail 模板和 CPO。

`final_suspend` 的默认策略应将 continuation 入目标 queue，而不是无条件返回 handle 进行对称转移。这样跨 scheduler 时不会在当前线程直接执行另一个 scheduler 的链。低延迟 inline continuation 可以作为显式 policy，但不能与严格 event-loop 边界混用。

语言规则的原始参考：

- 协程 frame 的 allocation function 查找、promise `operator new/delete` 和 allocation failure：<https://eel.is/c++draft/dcl.fct.def.coroutine>
- `co_await` 对 `await_ready`、`await_suspend` 返回 bool/void/handle 的控制流：<https://eel.is/c++draft/expr.await>
- 在非挂起点线程恢复 coroutine handle 的限制和并发恢复的数据竞争：<https://eel.is/c++draft/coroutine.handle.resumption>

cppcoro 的 task 展示了 continuation、final awaiter 和取消注册的实际组织，可以借鉴生命周期顺序，但它的默认 frame/线程策略不能直接作为 async 的 PMR 契约：<https://github.com/lewissbaker/cppcoro/blob/master/include/cppcoro/task.hpp>。

### 7.2 awaitable 扩展

扩展点优先使用标准语言协议：

```cpp
operator co_await
promise_type::await_transform
await_ready
await_suspend
await_resume
```

core 可以在 promise 中提供模板 `await_transform`，把带有 async 标记的 awaitable 映射为 scheduler-aware awaiter；普通 awaitable 默认保持语言语义。扩展是模板适配器，不是 awaiter 基类。

`std::expected<T, std::error_code>` 的 `noexcept` 函数链放在 `exec::ext`，转换为 sender 的 value/error completion channel。它不应改变普通 coroutine 的 `await_resume` 语义：普通 `co_await` 返回 expected 后，函数体仍会继续执行，短路由 sender 的错误通道负责。

## 8. Cancellation、关闭和完成

取消至少有三层：

1. 请求取消：设置 stop state 并通知 registration。
2. 操作响应取消：timer/I/O 从 pending index 移除，或者产生 canceled completion。
3. loop 关闭：拒收新操作，处理或明确完成已有 operation，最后释放资源。

registration 需要处理“注册与 request_stop 同时发生”以及析构期间回调正在运行的情况。callback 不应在 queue/timer 内部锁下调用用户代码。

标准 `stop_token`/`stop_callback` 的模型是基础参考：<https://eel.is/c++draft/stoptoken>。Asio 的 per-operation cancellation slot 说明取消能力可以通过 associated query 传播，而不需要 operation 继承基类：<https://think-async.com/Asio/asio-1.30.2/doc/asio/overview/core/cancellation.html>。cppcoro 的 registration 文档特别强调，registration 析构可能等待另一个线程正在执行的 callback：<https://github.com/lewissbaker/cppcoro/blob/master/include/cppcoro/cancellation_registration.hpp>。

完成协议应保证：一个已 start operation 恰好调用一次 value/error/stopped。关闭时如果不能继续运行 callback，必须产生停止/关闭完成，或者明确规定调用方执行 drain；不能只删除 queue node。

## 9. Core 与 execution 的桥接

core 可以先独立提供：

```text
core scheduler handle
enqueue(record)
poll_one()
post(record)
timer operation
coroutine task
cancel/stop
```

execution 层再实现：

```text
schedule sender
connect(sender, receiver) -> concrete operation_state
start(operation_state)
continues_on / starts_on 的适配
exec::ext::from_expected
```

上述两组 API 之间的转换放在独立的 `include/async/adapters/`。例如 `adapters/schedule_sender.h` 可以同时包含 core scheduler 和 `execution/protocol.h`，把一次 core enqueue 映射为 sender operation；`core/scheduling` 和 `execution/protocol.h` 互相不可包含。若未来发现某个 bridge 所需类型是纯运行时概念，应将该类型上提到 `core`，而不是让 core 引入 execution。

stdexec/libunifex 的 run loop、task 和 environment 只能作为 bridge 实现参考；它们本身是 execution 库，不是 core 的完整 I/O runtime。

## 10. 建议的实现顺序

1. **资源词汇和对象分配辅助**：`resource_ref`、对齐分配、构造失败回收、非拥有语义。
2. **intrusive ready queue**：单 consumer、多 producer 的明确模型；先不加入 timer 和 coroutine。
3. **event loop 协调**：queue、wake/wait、关闭、`poll_one` 和一个 record 的验收。
4. **timer index**：fake clock、插入/取消/到期与 queue 的转移；验证到期/取消竞争。
5. **operation 生命周期**：同步完成、完成中销毁、拒收、关闭和 resource 配对释放。
6. **cancellation**：stop state、registration、timer/queue 响应和停止完成。
7. **coroutine task**：frame policy、continuation queue、final suspend 和跨 scheduler 恢复。
8. **execution bridge**：schedule sender、connect/start、completion channel 和 scheduler 适配。
9. **owning post、批量预算和性能策略**：在基础生命周期稳定后加入，避免先用闭包类型擦除掩盖所有权问题。

每一步都应有 verify：先验证状态/所有权/线程归属，再测吞吐。benchmark 至少区分空轮询、一次 enqueue+wake、一次 timer 到期、一次 coroutine resume、最终回调和清理成本。

## 11. 尚待实测的选择

这些问题不能只凭资料决定，应在实现和 benchmark 后确认：

- ready queue 是否需要无锁 MPSC；第一版 mutex+intrusive queue 是否已满足目标。
- timer heap 的动态 backing array 是否足够，还是需要固定容量/分层 timing wheel。
- root coroutine frame 的公开创建 API 是显式 context 参数还是 task factory。
- completion record 是否允许 inline continuation，及其与 `poll_one` 契约的关系。
- stop registration 是自定义 intrusive state，还是在满足资源/生命周期边界的路径上复用 `std::stop_callback`。
- 资源跨线程释放的默认要求是 synchronized resource，还是由 scheduler owner 回收。
- exception-enabled 与 `expected/error_code` 受限路径如何分别声明，不能把局部 `noexcept` 扩大成整个 pipeline 的保证。

这些未决项不应通过新增继承层次或让 core 依赖 execution 来掩盖。应保持依赖方向、资源来源和完成时序可验证。
