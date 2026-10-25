# Async 统一实现重建路线

日期：2026-10-24。本文是后续重建的执行计划；目录、target 和 workflow 名称为目标设计，不表示当前已完成迁移。

原理与协议先看 [Async 设计速读：从协议到状态机](async-design-primer.md)：直接展开 sender/receiver、调度与协程，不需要从 lessons 开始。

## 1. 目标与范围

- 将 `asio/`、`stdexec/`、`standalone/` 三条生产实现收敛为仓库根目录下的 `include/async/`。
- 仓库自身就是 `async`，因此从父仓库看是 `async/include/async/`，不再额外创建一层 `async/`。
- 以 standalone core 的机制为起点，当前阶段直接复用 stdexec 的执行协议和通用组合算法，不再重写一套 execution 引擎。
- 过渡阶段只有 stdexec 这一项执行框架依赖；未来标准库 execution 达到所需能力后切换，最终只依赖标准库及必要的平台线程支持。
- 从第一阶段就隔离 stdexec 的公共入口和扩展，不让其私有实现渗入 core，为未来替换保留明确边界。
- 只保留一个根级 `tests/` 构建入口。正确性、编译契约、集成、lessons 和 benchmark 是其中的分类，不再各后端复制一份。
- benchmark 比较同一实现的不同提交，不再承担 Asio/stdexec/standalone 的横向排名。
- `main` 面向其他仓库消费，保持可构建、可测试和可安装；重建过程通过小分支逐步合入。

这里的“standalone core + stdexec execution”是实现职责的组合，不是将两套现有代码原样拼接：

| 来源 | 重建时的用途 | 合并边界 |
| --- | --- | --- |
| standalone core | 队列、定时器、通知、协程帧等机制及其边界测试 | 多后端选择和旧适配层 |
| stdexec | 当前唯一的执行协议和通用算法提供方 | 依赖其公共 API，不复制源码或依赖私有内部头文件 |
| Asio 后端 | 提取确实有价值的行为测试、历史问题和使用场景 | Asio runtime 及第二份实现 |

“当前复用 stdexec”与“最终零第三方依赖”是前后两个阶段。当前明确声明 stdexec 依赖；标准库满足需求后再移除。standalone 的自有 execution 算法作为历史参考保存在归档，不迁入正式实现与原生算法并存。

## 2. 先归档，再逐步替换

1. 清点当前工作区，包括未提交和未跟踪的文件；将需要保留的代码、文档及测试形成可追溯提交。
2. 创建归档分支，例如 `archive/pre-unified-2026-10-24`，可再建立同名时间点的 annotated tag。
3. 在归档记录中保存提交 SHA、编译器、构建选项、测试结果及历史性能报告。明确哪些旧行为需要保留，哪些属于本次允许修改的契约。
4. 从已归档的提交创建短期工作分支，分阶段向 `main` 合入；不重置或改写主分支历史。
5. 当新实现覆盖消费方实际需要的最小功能且通过验收后，再在一次可审查的迁移中切换正式 target 和 include 入口、删除三套旧实现。
6. 消费方固定已验证的提交或发布 tag；不要在重建期间依赖浮动的 `main` 获得可复现构建。

仅创建分支不会保存未提交和未跟踪内容；归档提交必须先包含这些需要保留的变更。

重建早期可以在工作分支暂存新旧目录，但不引入第二套长期 backend 开关。旧实现完成交接后只在归档历史中保留。

## 3. 目标目录与依赖方向

```text
async/                         # 当前仓库根目录
  CMakeLists.txt
  CMakePresets.json
  include/async/
    async.h                    # 公共总入口
    config.h                   # 统一配置；不散落后端判断
    core.h
    execution.h
    core/
      scheduling/
      timer/
      synchronization/
      cancellation/
      coroutine/
    execution/
      environment.h            # 必要的公共环境适配
      details/
        provider.h             # 集中接入 stdexec；未来切换标准库的边界
      extensions/              # exec::ext；本库扩展与标准算法分开
  src/                         # 仅在确有非模板运行时代码时建立
  tests/
    CMakeLists.txt
    common/
    unit/                      # 含并发、资源和故障路径
    compile/                   # concept、签名、头文件自包含、预期编译失败
    integration/               # 安装消费、协程与执行图组合
    lessons/
    benchmark/
      cases/
      tools/
  cmake/                       # 必要的安装、测试支持
  docs/
  .github/workflows/
```

依赖方向固定为：

```text
标准库 ──→ core 的纯机制（queue / callback / timer heap / 帧存储）
stdexec ─→ 集中 execution 接入层（协议 + 原生通用算法）
                       └─→ core 的 scheduler / timer sender / signal sender

协议 + 纯机制 → core 运行设施 → 协程与 sender 互操作
execution + core → exec::ext 和用户组合
公开接口 → tests / lessons / benchmark
```

纯队列和 timer heap 不依赖 stdexec、`then`、`when_all` 或协程；原生通用算法不依赖本库 `Context`。本库自己的运行循环复用 Queue，不再维护第二份队列；原生 `sync_wait` 的内部运行设施由提供方负责，不为复用 Queue 而重写它。

一个 `tests/` 模块不等于一个测试可执行文件：多个可执行目标可以服务不同编译选项、编译失败用例和并发测试，但统一由一个入口、公共支持代码和命名规则管理。

## 4. 实现顺序及每阶段验收

### 阶段 0：归档、行为清单与初始性能基线

先完成第 2 节的归档。根据当前消费方和 lessons 建立最小功能清单，按“必须保留 / 可改变 / 延后”分类。

运行可用的现有 verify 和代表性 benchmark，记录工具链与原始数据。旧三后端数据仅作为历史资料；只有语义、输入和计时范围一致的用例才可与新实现比较。

验收：归档能独立检出；需要保留的工作区变更均可在提交中找到；功能与行为变更清单可审查。

### 阶段 1：单库构建骨架、统一 tests 与最小 CI

- 建立一个正式导出 target，建议 `LCFAsync::async`，其实现形式由实际非模板代码需求决定。
- 先稳定普通头文件模式；设置目标级编译要求、include 路径和必要线程链接。
- 显式声明唯一的 stdexec 依赖，使用已安装或父项目提供的 target；不要默认在消费者配置时联网下载。
- 保留清晰的 tests / benchmark 开关，作为子项目被引用时默认不构建、不下载开发依赖。
- 配置根级 `tests/`、一个最小消费程序和最小 GitHub Actions 工作流。
- 定义 benchmark JSON 的版本字段、用例命名及结果上传规则；后续每个机制完成时立即加入对应性能用例。

验收：提供 stdexec 后，`add_subdirectory` 消费和离线配置成功；最小构建和测试在 CI 运行；没有三后端矩阵或默认联网下载。依赖版本有记录，头文件及 package 配置正确表达实际依赖。

### 阶段 2：协议、生命周期和资源契约

先集中接入原生协议、写使用契约和失败用例，再实现复杂行为：

- 原生 `sender`、`receiver`、`operation_state`、`scheduler` 及相关 CPO 的统一导出，不自定义第二套同名协议。
- 原生 `get_env`、停止令牌和 allocator 查询、completion signatures 及 cv/ref 传播规则。
- `connect` / `start` 的职责、operation state 地址稳定性、合法调用顺序、完成后访问限制。
- value / error / stopped 三通道；每次操作恰好一次终止通知；协作取消不等于销毁操作，也不等于关闭队列。
- 明确哪些阶段可能抛异常，哪些调用必须 `noexcept`；组合算法捕获用户回调异常时如何表达错误签名。
- 确定 PMR 资源的作用域、所有权、跨线程条件，以及构造失败和 shutdown 的释放路径。

命名空间保持 `lcf::async`、`lcf::async::exec` 和 `lcf::async::exec::ext`。原生协议集中在 execution 接入层，core 只使用本库的统一入口，不能在各文件散落 stdexec 私有类型或条件编译。

验收：最小原生 sender/receiver 与本库导出入口互操作；编译契约测试通过；无效 receiver、错误签名和不满足 noexcept 的操作不能误通过 concept；公共头文件可独立包含。

### 阶段 3：PMR、取消门控与纯队列机制

实现最小且可单独测试的机制：资源辅助 → callback → 取消注册门控 → Queue。不要先引入缓存、无锁结构或复杂类型擦除优化。

资源策略从这一步固定：

- `Context(resource)` 向 `Queue` 传递默认 resource，Queue 保存非拥有引用。
- 队列关联的 posted `Record` 默认使用该 resource；timer heap 随后使用同一队列资源。
- 分配时保存 resource；正常完成、回调抛异常、入队后丢弃、关闭后拒收和构造失败均正确回收。
- 若 Queue 本体及共享所有权存储也纳入该策略，在创建处使用 allocator-aware 分配；仅传构造参数不会改变对象本身的分配来源。
- 外部 operation state 仍由创建者管理；协程帧和 receiver 环境 allocator 的作用域单独写清，不假定 Queue 能管理所有内存。
- `post` 分配和回调销毁可能并发，队列结构锁不会自动保护 resource；统一资源必须满足实际并发访问条件。
- resource 存活期覆盖仍持有队列的 handle、相关记录及进行中的释放；不把 `Context` 析构等同于所有外部 handle 已销毁。

shutdown 需要分别规定：拥有的 posted callback 如何丢弃、外部 operation state 如何完成或解除登记，以及消费者何时可以销毁它。不能把所有 callback 一律 delete，也不能让必须完成的操作悄悄失联。

验收：多生产者入队、执行和关闭竞争、重入回调、构造失败、过对齐对象、分配/释放配对、取消注册竞争均有测试；尚未启用 timer 和 coroutine 也能独立验证队列。

### 阶段 4：最小 execution 与运行设施闭环

先使用原生 `just` / `just_error` / `just_stopped`、`then` 和 `sync_wait` 验证统一入口，再实现本库的 `Context`、`Scheduler`、`schedule` 及确有需求的 `RunLoop` 适配。

- 组合直接采用原生 operation state；不在每个 `then` 外再包一层分配或类型擦除。
- `Scheduler` 保持具体类型；需要时才提供普通回调 `Executor` 的类型擦除层。
- `Scheduler` 到 `Executor` 的擦除必须保留资源查询和调度能力，不能在 `post` 路径退回普通堆分配。
- `poll`、`run`、`runUntilIdle`、stop/restart/shutdown 共享队列语义，不各自维护状态机。
- `sync_wait` 的进度来源必须明确：它不会自动推进任意外部 Context。等待外部 scheduler 的测试应启动推进线程或使用明确支持的运行循环。

验收包含三个不同闭环：同步 `just → then → wait`、`schedule → connect/start → poll`、有独立 worker 推进的跨线程等待。覆盖 value/error/stopped、回调异常和关闭行为。

### 阶段 5：组合算法与结构化完成

按依赖开放原生算法入口，并逐项验证与本库 runtime 的组合：

1. `upon_error` / `upon_stopped` 等通道映射。
2. `let_value` / `let_error` / `let_stopped` 与本库异步子操作的生命周期。
3. 启动与完成位置切换算法，明确 scheduler 和 environment 如何传播。
4. `when_all` 连接本库 sender 时的取消、错误竞争及全部子操作收尾。
5. 只有消费方确实需要时，再实现共享、缓存、重复和更复杂的定制机制。

标准算法、stdexec 扩展和本库扩展逐项登记；不要根据 stdexec 是否导出某个名字就判断它属于 C++26 标准。完整标准符合性需要独立清单，不能仅凭存在同名函数宣称实现完成。

这些步骤不重写 stdexec 的 let/when_all 状态机；测试重点是本库 sender、scheduler 和 environment 对原生契约的履行。验收：move-only 输入和结果、cv/ref、一次完成、空组合、同步重入、错误竞争、停止传播及子操作存储释放有覆盖；新增入口同时增加契约测试和代表性 benchmark。

### 阶段 6：timer 与 signal

复用 Queue、资源策略、停止门控和 operation state，加入 timer heap、TimerService、`schedule_after` 及 `Signal`。

- timer heap 的分配、扩容失败、取消、过期与 shutdown 采用同一套资源和登记契约。
- 明确 timer 的先后顺序、取消与到期竞争，以及完成是否需要迁移 scheduler。
- 明确 Signal 的通知记忆、等待者生命周期、取消和重入语义。
- 时间计算和到期选择尽量提供可控时间输入；并发测试使用 barrier/latch 等协调，少依赖固定 sleep。

验收：取消/过期/关闭竞争、停止后 restart、已关闭队列、资源拒绝分配、通知与取消竞争均有测试；timer 和 signal 不再各自复制 callback 所有权逻辑。

### 阶段 7：协程及 sender 互操作

依次实现返回值存储与基本帧所有权 → initial/final suspend 和父子 continuation → 异常与 stopped → sender awaiter → scheduler/environment/stop/allocator 传播。sender 互操作只依赖原生公共协议，不重新耦合 stdexec 的私有协程辅助。

- 先覆盖完整生命周期，再增加帧缓存；自定义 PMR、过对齐、创建线程退出、跨线程销毁均属于明确契约。
- `co_await sender`、`Awaitable` 转 sender 和父子协程互操作共用 completion 规则。
- cancellation 不直接 destroy 仍有外部操作访问的帧；挂起、完成和 destroy 的职责必须可追踪。
- 将现有 lessons 按依赖迁移，每引入一个语言机制就配一份可执行示例，而不是最后才补课程。

验收：void/value/exception、同步完成/真正挂起、父子恢复、取消竞争、未启动销毁、已完成销毁、自定义资源及资源失败均有测试；没有遗留 stdexec 私有协程辅助依赖。

### 阶段 8：扩展、错误码工作流与必要的便利接口

在协议和组合算法稳定后迁移 `exec::ext::from_expected`、`then_expected`、确有需求的 retry 等扩展。

- `expected<T, std::error_code>` 的错误映射为 `set_error`，后续 value 分支跳过；恢复操作可以重新产生 value。
- “业务回调不再执行”与“整个操作不再完成”分开：短路后仍应向消费者交付 error/stopped，释放登记与存储。
- 需要避免创建后续任务时，使用 lazy factory；已构造的执行图不会因为运行时错误自动撤销构造行为。
- noexcept callable 不代表构造、connect 或整条任意组合链都不会抛异常，保证范围应写在接口文档中。

验收：零错误码的 unexpected、void、move-only 值、错误恢复后继续 then、未恢复错误跳过 then、上游 stopped 和原错误转发均被验证。

### 阶段 9：正式消费、旧目录退场与性能门禁收紧

- 完成安装、导出、版本配置、迁移说明和独立消费者测试，替换下游实际使用的接口。
- 删除旧三后端生产目录、重复 tests、比较 backend 的适配工具和失效文档链接。
- 普通头文件已稳定后，再按实际消费需求引入 C++ modules；模块导出复用同一份实现，不维护第二套逻辑。
- 检查导出 target、include 和安装产物，不残留 Asio、旧 backend target 或构建目录绝对路径；过渡阶段所需的 stdexec 依赖必须被正确声明并可由消费者提供。
- 根据已收集的噪声和稳定基线，将适合阻断的性能指标设为正式检查项。

验收：在已提供 stdexec 的干净环境中可以离线消费安装产物；至少一个真实下游迁移成功；公共 API、线程/停止/资源契约、lessons 和 benchmark 与实现一致。

### 后续：按能力验证标准库 execution

先把本库的自定义 scheduler/sender/receiver 与可用的标准算法做互操作验证，再切换集中接入层。未来标准库可用不等于 stdexec 的 CPO、concept、environment 与标准类型自动等价。

- 以配置时编译探测验证实际需要的协议、算法及互操作；不只检查 `<execution>` 是否存在或编译器是否接受 C++26 选项。
- 若提供标准库实现模式，provider 选择只在集中边界完成，整个构建保持一致；不要逐头文件混搭两套 CPO。
- 按实际规范逐个映射入口，例如标准的 `sync_wait` 位于 `std::this_thread`，并不是简单把整个 `stdexec` 命名空间换成 `std::execution`。
- 核对公开返回类型、错误签名、scheduler 查询、停止令牌和 allocator 的差异；必要的转换在边界显式完成。
- 本库扩展保留在 `exec::ext`，不能注入 `std`；标准缺少的能力仍由本库负责。
- 单独处理非标准依赖，例如当前 stdexec 后端的 `split` 来自 `<exec/split.hpp>`；需要保留时归入本库扩展，不能假定标准库有同名替代。
- 切换前先跑同一份行为与编译契约测试；切换完成后移除 stdexec 的 find/link/include，再在没有 stdexec 安装和缓存的环境中验证真正零第三方消费。

## 5. 测试和 benchmark 从何时开始

测试、性能记录和 CI 不属于最后一个实现阶段：阶段 1 建立骨架，阶段 3 起每个机制有自己的资源与性能检查，随后随算法和协程逐步扩大。

### 正确性层次

| 类别 | 主要检查 |
| --- | --- |
| compile | concept、completion signatures、cv/ref、自包含、负向编译用例 |
| unit | 队列、资源、算法、timer、signal、协程的正常和失败路径 |
| concurrency | 取消/完成/关闭竞争、跨线程释放、重入、恰好一次完成 |
| integration | execution + runtime + coroutine 组合、真实消费场景 |
| packaging | `add_subdirectory`、安装后 `find_package`、关闭测试离线消费 |
| lessons | 示例可编译可运行，文档控制流与测试相符 |

测试依赖和 benchmark 工具可以是显式启用、固定版本的开发依赖，但不得进入正式库的公共链接图或消费者配置过程。最终零第三方指库的消费者；过渡阶段的 stdexec 生产依赖要单独声明，不能与测试依赖混淆。

### 性能用例与记录

保留稳定的用例标识、输入规模、计时范围及 `suite_revision`。至少逐步记录：

- 空 poll、post 入队、callback 派发、跨线程投递。
- `just/then` 链、动态 let 子图、when_all、scheduler 切换。
- timer 注册/取消/到期、signal 通知与取消。
- 协程创建/销毁、父子恢复、sender awaiter。
- 默认 resource 与自定义 resource 下的分配次数、分配字节和峰值占用。

吞吐/延迟、分配统计和带检测器的正确性测试分开运行，避免插桩本身改变待测时间。性能优化不得删除异常、取消或生命周期测试来获得更好数字。

每次结果记录至少包含：

```text
commit_sha / base_sha / source_tree_dirty
schema_version / suite_revision / case_id / input_parameters
workflow_run_id / run_attempt / timestamp
compiler / standard_library / language_mode / optimization_flags
execution_provider / provider_revision
build_type / sanitizer / library_configuration
os / architecture / runner_image / cpu_model / thread_count
原始重复结果 / median / 离散程度 / 单位 / 有效样本数
分配次数 / 分配字节 / 峰值占用（适用时）
```

结果按 `commit SHA + environment + suite_revision + run_id` 保存；不能只覆盖 `latest.json`，也不能把不同环境的结果直接连成同一条趋势线。源码指纹可以辅助判断是否有实现变化，但不能代替提交和环境身份。

stdexec 的固定版本或提交也属于被测配置；依赖升级和未来切换标准库时保留前后配置，明确这次变化的来源，不能把它混入无法解释的历史趋势。

测试负载、输入或计时边界改变时更新 suite revision；不兼容结果标记为“不可比较”。新增用例没有基线时登记新基线，不能用缺失值或 0 冒充性能改善。

## 6. GitHub Actions 和回归判定

工作流放在 `.github/workflows/`。先用最少的 workflow/job 支撑清晰的职责，再按实际运行时间拆分。

| 触发 | 正确性任务 | 性能任务 |
| --- | --- | --- |
| pull request → main | 必需的编译、测试、安装消费；有支持的检测器检查 | 在同一 runner 顺序测 base/head，生成比较报告 |
| push → main | 验证被合入的实际提交 | 保存该提交的版本结果和趋势数据 |
| schedule / 手动 | 扩展工具链、长时间并发和压力测试 | 更多输入规模、重复次数及稳定环境复测 |

常规 push 工作流记录的是触发工作流的提交，不会自动为一次 push 中的每个历史提交逐一运行。若要求每个提交都有数据，应采用每提交推送/验证策略，或显式增加逐提交回放任务；同样不能为节省 CI 时间取消掉又声称这些提交已测量。

PR 需要记录 base、head 和实际测试的 checkout SHA；GitHub 事件提供的 SHA 不能不加区分地当作 PR 源码 head。默认构建合并结果时，也应把这个合并提交身份写入报告。

性能比较遵循：

1. correctness 通过后才运行正式 benchmark；Release 构建，固定参数、工具链和测试配置。
2. 同一台 runner 中顺序测试 base/head，预热并保留多次重复的原始结果；不要让两组 benchmark 同时争抢 CPU。
3. 根据指标方向计算差异：延迟越低越好，吞吐越高越好；结合绝对差、相对差和噪声判定。
4. 共享 hosted runner 初期以报告为主；需要强制时间门禁时使用稳定环境并建立可复现的噪声范围。
5. 对确定性的分配预算、结果正确性、资源泄漏和完成次数可尽早设置硬检查；不要为不同输入规模统一套一个分配上限。
6. 时间阈值按用例校准，并在可疑退化时复测；不虚构一个普适的 5% 或 10% 为科学门槛。
7. 预期的成本变化由评审说明原因并更新基线；基线变更与实现变更同样可追溯，不能静默覆盖。

CI artifact 用于短期原始数据和报告，不能假定永久保留。发布/主分支的长期记录放到明确的持久存储，例如独立 benchmark 数据分支或专用结果仓库；正式源码分支不持续提交大量机器结果。缓存只用于加速构建，不作为基线的唯一来源。

PR 的权限只满足构建、读基线和上传结果；向长期结果存储写入由可信的主分支工作流完成，不让来自 PR 的代码借高权限工作流发布结果。

多平台/编译器矩阵用于正确性；不要比较 Windows/MSVC 与 Linux/Clang 的绝对耗时并据此判断某次代码提交退化。检测器、Debug 和性能 Release 构建也分别记录。

## 7. 建议的提交顺序

每项可以拆为多个小提交，但按下面顺序验收，避免一次搬入全部源码再补契约：

1. 归档现状、行为清单、现有测试与性能报告。
2. 单库 CMake、统一 tests、最小 CI、性能记录 schema。
3. 原生 execution 统一接入、环境和完成契约、编译测试。
4. PMR 约定、callback、取消门控、Queue 和失败路径。
5. 原生最小 sender 组合、本库 Context/Scheduler、运行循环和等待闭环。
6. 原生通道恢复、动态子操作、调度切换和 when_all 的 runtime 集成。
7. timer、signal 及其资源与并发验证。
8. 协程、sender 互操作和逐课迁移的 lessons。
9. expected/error_code 等 `exec::ext` 扩展。
10. 真实下游验收、安装迁移、旧目录清理和必要的 modules 支持。
11. 稳定性能环境、长期结果存储和校准后的强制回归门禁。
12. 有实际可用的标准库实现时，验证 execution 互操作、替换提供方并移除 stdexec 依赖。

各阶段共同的完成条件：行为和资源契约有文档；正常、异常、取消、关闭路径有相应测试；新机制有代表性性能记录；consumer 依赖边界与当前阶段相符；不会留下无人负责的悬挂操作或未声明的分配路径。

## 8. 参考资料

以下是规范及工具资料的入口。标准工作草案和 stdexec 会继续变化，执行每个阶段时应记录实际采用的版本或提交；不要把 live draft 当作永久固定的 C++26 快照。

- [C++ execution 工作草案](https://eel.is/c++draft/exec)
- [标准的阻塞消费算法](https://eel.is/c++draft/exec.consumers)
- [NVIDIA stdexec](https://github.com/NVIDIA/stdexec)
- [Google Benchmark 用户指南](https://github.com/google/benchmark/blob/main/docs/user_guide.md)
- [GitHub Actions workflow 语法](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)
- [GitHub Actions 事件及 PR 的提交身份](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows)
- [GitHub Actions artifact 存储和保留期](https://docs.github.com/en/actions/how-tos/writing-workflows/choosing-what-your-workflow-does/storing-and-sharing-data-from-a-workflow)
