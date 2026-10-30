# Async Core MVP 开发顺序

本文只规定首个可运行里程碑的范围、文件和实施顺序。运行时类型、协议和行为契约以 [CORE_DESIGN.md](CORE_DESIGN.md) 为准；本文代码是目标用法草图，不表示接口已经实现或编译。

## 1. 交付目标

MVP 要搭好符合框架分层的最小运行骨架，使 `Context` 能驱动 execution work，并由调用方明确控制推进时机：

```text
Context 显式配置资源和逻辑目标
    → 获取 SchedulerHandle 并适配为标准 scheduler
    → schedule | then
    → connect/start，将稳定 operation record 发布到 EventLoop
    → Context::pollOne() / Context::poll() 执行 record 并完成 receiver
    → close 停止准入，调用方继续 poll 直到已接纳工作 drain
```

MVP 的验收核心是 execution work 确实由 `Context::pollOne()` 或 `Context::poll()` 推进并完成，不以 `sync_wait` 或后台 Worker 代替这条路径。EventLoop 是单 owner、非等待式组件；MVP 不创建执行线程。

## 2. 范围与边界

| MVP 包含 | 留给后续阶段 |
| --- | --- |
| 一个内部手动驱动 EventLoop；`pollOne`、`poll(PollBudget)`、关闭和 drain | Worker/Workers、等待式 `runOne/run`、线程创建与 join、多执行域映射 |
| 稳定逻辑队列身份；多个 serial key 可映射到唯一 loop | thread-bound、多 worker 静态映射、共享/隔离约束、外部 render/gfx loop 组合 |
| mutex MPSC inbox、intrusive record/ready、owner-local 消费 | 专有 SPSC producer lane、队列公平汇合和其他低延迟入口 |
| 显式借用的 `std::pmr::memory_resource` | allocator 覆盖/组合、hook 协议、可报告错误的无异常分配协议 |
| `ScheduleRecord`、`SchedulerHandle`、标准 scheduler adapter 和稳定 `ScheduleOperation` | timer service、`then_expected`、Task、frame policy、sender/coroutine 桥接 |
| provider 必需的 scheduler/sender/operation、stop 和环境查询契约 | timer awaitable、协程 Environment 的通用组合与扩展 query |

MVP 只接受已明确支持的配置；不支持的 queue mode 或资源能力必须明确拒绝。adapter 要符合选定 execution provider 的资源相等、完成签名、completion scheduler 和跨线程使用契约。`eFull`、`eClosed`、已请求停止和完成中销毁均须有明确且至多一次的完成路径。

## 3. 用户端验收路径

宿主提供并保持 `memory_resource` 存活。正常成功路径由一个 record 执行 `then`，再完成 receiver：

```cpp
namespace async = lcf::async;
namespace ex = async::exec;

constexpr std::uint32_t k_cpu_work = 1;

struct Receiver
{
    using receiver_concept = ex::receiver_t;
public:
    void set_value(int value) && noexcept
    {
        *m_result_opt_p = value;
        *m_completed_p = true;
    }
    template <typename Error>
    void set_error(Error &&) && noexcept { *m_completed_p = true; }
    void set_stopped() && noexcept { *m_completed_p = true; }
public:
    std::optional<int> * m_result_opt_p;
    bool * m_completed_p;
};

auto context_info = async::core::ContextInfo{}
    .setMemoryResource(memory_resource)
    .addSerialQueue(k_cpu_work);

async::Context context{context_info};
context.start();

auto scheduler = async::adapters::as_scheduler(
    context.getSchedulerHandle(k_cpu_work));
static_assert(ex::scheduler<decltype(scheduler)>);

std::optional<int> result_opt;
bool completed = false;
auto work = ex::schedule(scheduler)
    | ex::then([]() noexcept { return 42; });
auto operation = ex::connect(std::move(work), Receiver{&result_opt, &completed});
ex::start(operation);

assert(not completed);
assert(context.pollOne() and completed);
assert(result_opt == 42);
context.close();
```

再以 `context.poll()` 替换单次 `pollOne()`，并提交多个 operation，验证批量推进和显式 `PollBudget`。两种 poll 都不等待新工作；一次无工作返回不代表业务 root 已全部结束。`close()` 停止准入，不代替调用方推进 callback；释放 Context 前须由 root owner 确认已接纳工作全部完成。

另以测试自有线程和受控 gate 验证跨线程 connect/start、多 producer 发布、单 owner poll 和 close/drain 竞争。测试保持 operation 与 receiver 存活至完成，并验证完成 callback 可以销毁 operation。

## 4. MVP 文件结构

按实现需要创建文件，不为未来阶段预建空文件。类文件采用 PascalCase；协议、自由函数和聚合文件采用 snake_case；execution 沿用 provider 命名。

```text
include/async/
├── async.h                              # Core 与 adapters 聚合入口
├── core.h                               # 不依赖 execution 的 Core 入口
├── execution.h                          # 已有 provider/algorithm 聚合入口
├── core/
│   ├── protocol.h                       # Core 协议聚合入口
│   ├── protocol/
│   │   ├── ScheduleRecord.h             # 执行/停止终结入口与稳定 record
│   │   └── executor.h                   # EnqueueResult、CPO、executor_c
│   ├── scheduling/
│   │   ├── SchedulerHandle.h            # 非拥有提交句柄与逻辑身份
│   │   ├── LogicalQueue.h               # QueueToken 与 serial 逻辑目标
│   │   ├── BasicEventLoop.h             # poll、预算、关闭和推进
│   │   └── queue/
│   │       ├── MutexInbox.h             # 通用 MPSC 发布入口
│   │       └── IntrusiveReadyQueue.h    # owner-local ready 队列
│   └── context/
│       ├── ContextInfo.h                # PMR 与逻辑目标配置
│       └── BasicContext.h               # EventLoop 组合、poll 和生命周期
├── adapters/
│   ├── Scheduler.h                      # Core executor 到标准 scheduler
│   ├── ScheduleSender.h                 # ScheduleSender/Operation
│   └── environment.h                    # provider 必需 query 的绑定
└── execution/                           # 已有 provider 协议及算法

modules/async.cppm                       # 与头文件 API 同步导出
tests/verify/
├── protocol_test.cpp                    # Core concept、CPO 与分层
├── event_loop_test.cpp                  # 发布、poll、close/drain
├── context_test.cpp                     # Context poll 入口
├── scheduler_adapter_test.cpp           # provider scheduler/operation 契约
└── mvp_execution_test.cpp               # Context → execution 完整路径
tests/benchmark/
└── runtime_benchmark.cpp                # MVP 性能基线，后续按能力扩展
```

测试按可观察契约拆分，不要求一个头文件对应一个测试文件。新源文件接入现有 verify/benchmark target；benchmark 不加入默认构建或 CTest。

## 5. 开发顺序

四个连续增量构成同一个 MVP。每一步都保留可构建状态，接口草图以设计文档为准；跨层签名先用最小 provider 编译用例冻结。

| 顺序 | 实现内容 | 完成条件 |
| --- | --- | --- |
| M.a 协议与最小配置 | `ScheduleRecord`、enqueue CPO/`executor_c`、`EnqueueResult`、`QueueToken`、`ContextInfo`、显式 PMR；确定 provider 定制方式和接纳/拒收/关闭状态转换 | Core 可单独包含；合法/非法 executor 定制可区分；未知逻辑 key 和不支持配置有确定行为 |
| M.b 手动执行骨架 | `MutexInbox`、intrusive ready、`BasicEventLoop`、`PollBudget`/`PollReport`、单 owner `pollOne/poll/close` | 多 producer 发布由单 owner 消费；poll 非阻塞，pollOne 最多执行一个 record；已接纳工作可排空 |
| M.c execution 贯通 | `BasicContext`、逻辑 key 到唯一 loop 的映射、`SchedulerHandle`、scheduler/sender/operation adapter、必需 provider environment query | `schedule | then` 经 connect/start 发布 record，只由 Context poll 执行并完成 receiver；provider concept 和完成域成立 |
| M.d 生命周期与基线 | 补齐拒收、停止、关闭、完成中销毁和跨线程竞争路径；完善 MVP 回归与 runtime benchmark | V01–V04、V07、V08、V09 的 MVP 子集通过；保存单条、批量、多 producer、空 poll 和端到端路径基线 |

实现过程中重点处理发布后立即被 owner 消费的竞争：`eAccepted` 后不能再次读取可能已被完成 callback 销毁的 operation。`eFull/eClosed` 不转移 record 所有权，由 adapter 依完成契约处理。queue/loop 锁内不执行 receiver；关闭不丢失已接纳工作。执行预算在 record 边界检查，时间预算是软限制，不会抢占长 callback。

## 6. MVP 退出条件

- 用户构造 `Context` 和 scheduler，start execution operation 后，单独调用 `Context::pollOne()` 或 `Context::poll()` 能推进其工作并完成 receiver。
- `pollOne` 非阻塞且最多推进一个 record；`poll` 按记录数/软时间预算推进；二者空闲时及时返回。
- 单 owner 驱动约束、多 producer 发布、逻辑身份、拒收/停止/关闭和 operation 最后访问规则均有可重复验证。
- `close` 停止新准入；调用方继续 poll 直到已接纳 operation drain 后才释放运行资源。
- Core 与 execution provider 保持分层，标准 scheduler adapter 满足当前 provider 实际检查的协议。
- verify 覆盖上述契约；benchmark 能重复测量 MVP 关键路径并记录配置，作为后续优化基线。

后续阶段、依赖和验证方向见 [CORE_DESIGN.md](CORE_DESIGN.md) 的能力演进路线与验收矩阵。

## 7. 首轮验证命令

沿用仓库现有 CMake preset 和显式运行 target。verify 在 Debug 配置运行；benchmark 单独以 Release 构建和运行，不加入默认构建或 CTest：

```text
cmake --preset clang
cmake --build build/clang --config Debug --target async_verify_run
cmake --build build/clang --config Release --target async_benchmark_run
```

benchmark target 将结果写入对应测试构建目录的 `results/Release/latest.json`。保存基线前记录 commit、provider/编译器、构建配置、平台、producer 数、容量和 workload；后续比较前另存基线，避免覆盖。完整的性能比较原则见设计文档第 10.2 节。
