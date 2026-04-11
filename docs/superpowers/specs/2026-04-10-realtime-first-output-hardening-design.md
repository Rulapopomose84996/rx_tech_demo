# 实时优先输出退化加固设计

## 1. 背景

`rx_tech_demo` 当前已经完成 datagram-first 接收主线，`dpdk` 与 `socket` 两条 ingress 路径都汇入统一的协议解析、CPI 状态推进与 finalize 主线。

当前继续推进的主要阻碍不在 ingress 统一，而在 finalize 之后的异步输出链路：

- `OwnerLoop` 主线程负责实时接收与 CPI 推进
- `CpiStateCoordinator` 在 finalize 后将 `CpiOutput` 推入 `output_ring`
- `CpiConsumer` 异步消费后再通过 `recycle_ring` 归还 pool slot

这条链路在“慢消费者 / 输出侧变慢”的情况下，已经具备基础功能，但还没有把“实时性优先”设计原则明确固化为工程行为边界。

对于雷达实时系统，本阶段不应把“输出完整性”放在“接收主链实时性”之前。当前子项目的目标不是追求输出永不丢失，而是保证：

- 接收主线程绝不被输出侧反向拖慢
- 慢消费者不会演变成隐性背压
- 输出退化是受控、可观测、可配置的

## 2. 设计目标

本阶段主目标是把输出链路明确改造成“实时优先、零阻塞、允许退化”的工程闭环。

必须达成的目标：

- `OwnerLoop` 热路径绝不因为输出侧资源不足而等待
- `CpiStateCoordinator` 在 finalize 后只允许非阻塞输出
- 输出资源不足时允许丢弃 finalized CPI，但必须立即释放 pool slot
- 退化必须被统计、展示并纳入最终运行结论
- 退化结论可配置为 `degraded` 或 `error`

本阶段明确不做：

- 不引入主线程等待、限时等待、sleep/retry 形式的背压
- 不做多队列并发或更大规模异步框架重构
- 不把“能跑”表述成“已经达到最终 5Gbps 能力”
- 不把 DPDK 实网性能调优和本次输出链路加固混在同一个任务里

## 3. 核心原则

本设计的最高原则是：

> 接收主链实时性高于输出完整性；输出侧只能退化，不能反向阻塞主链。

落到代码行为上，这一原则具体意味着：

- finalize 后只允许尝试一次非阻塞 `output_ring.push()`
- push 失败时立即走退化路径，不重试、不等待、不让出时间片后继续等
- pool slot 在“正常消费回收”和“输出退化丢弃”两种路径下都必须最终可复用
- `degraded` / `error` 是运行结论，不是主线程行为开关

## 4. 当前问题

当前实现已经具备环形队列、上下文池和异步消费线程，但存在几个需要明确加固的点：

### 4.1 输出失败语义不够工程化

当前 `CpiStateCoordinator::finalize_active()` / `finalize_previous()` 在 `output_ring` 满时已经执行了立即释放池位，但运行语义还不够完整：

- 缺少清晰的“输出退化事件”统计口径
- 缺少统一的运行结论升级规则
- 缺少面向实时性的文档化边界说明

### 4.2 退化不可充分观测

当前 summary 和状态面板还没有把以下信息作为一等公民输出：

- 输出 drop 次数
- `output_ring` 满事件次数
- `recycle_ring` 压力事件
- 本次运行是否已从 `success` 退化到 `degraded/error`

### 4.3 配置层没有显式表达策略

当前系统行为更多由实现细节隐式决定，而不是由配置显式定义：

- 发生输出丢弃后是否只是 `degraded`
- 还是要把最终结论升级为 `error`
- ring 容量是否作为运行配置暴露

## 5. 目标架构

本阶段不引入新线程模型，而是在现有结构上固化实时优先边界。

目标数据流如下：

`OwnerLoop -> CpiStateCoordinator.finalize_* -> output_ring.try_push`

分支行为：

- `push` 成功：
  - `CpiOutput` 进入 `CpiConsumer`
  - consumer 处理完成后通过 `recycle_ring` 返回 `ReleaseToken`
  - owner 线程在主循环中 drain recycle 并释放池位

- `push` 失败：
  - 立即记录一次输出退化事件
  - 立即释放对应 pool slot
  - 更新运行结论状态
  - 主线程继续处理后续 datagram，不进入等待

这意味着输出链路被明确划分为两层：

- 主链：接收、协议处理、CPI 推进、finalize，必须实时优先
- 旁路：异步输出消费与回收，允许退化

## 6. 模块职责调整

### 6.1 OwnerLoop

`OwnerLoop` 继续只做流程协调，不承载背压策略判断。

本阶段对它的要求：

- 保持主循环轻量
- 继续在每个 datagram 前后做常数级 `drain_recycle`
- 不引入 sleep、等待、重试 push 等逻辑

### 6.2 CpiStateCoordinator

这里是本阶段的核心改造点。

需要明确提供“零阻塞输出退化”行为：

- finalize 后尝试一次非阻塞 push
- push 失败时：
  - 记录 `output_ring_full`
  - 记录 `finalized_output_drop`
  - 立即释放 pool slot
  - 根据配置把本次运行标记为 `degraded` 或 `error`

这里不允许：

- 自旋等待 ring 空位
- sleep 后重试
- 以“再等等 recycle”方式变相形成背压

### 6.3 CpiConsumer

`CpiConsumer` 仍然是异步消费者，不负责主链稳定性兜底。

本阶段要求：

- 能消费时尽快消费
- 慢速时只影响输出完整性，不影响接收实时性
- 维持已有消费后回收令牌返回机制

### 6.4 RxConfig

新增显式配置项表达策略，而不是把策略埋在代码里。

建议新增：

- `output_drop_policy`
  - 值域：`degrade` / `error`
  - 默认：`degrade`
- `output_ring_capacity`
  - 默认值使用保守基线
- `recycle_ring_capacity`
  - 默认值使用保守基线

本阶段不新增任何“等待多久”的配置，因为已明确禁止背压。

### 6.5 Metrics / Summary / StatusPanel

新增并统一以下观测字段：

- `finalized_outputs_total`
- `queued_outputs_total`
- `dropped_outputs_total`
- `output_ring_full_events_total`
- `recycle_ring_full_events_total`
- `run_degraded`

最终 summary 必须能回答三个问题：

- 接收主链是否保持实时运行
- 输出侧是否发生退化
- 本次运行的结果结论是 `success`、`degraded` 还是 `error`

## 7. 运行语义

本阶段引入明确的运行结论分级：

- `success`
  - 没有发生输出侧退化
- `degraded`
  - 主链保持实时
  - 但输出侧发生过丢弃或退化事件
- `error`
  - 主链仍然不阻塞
  - 但根据配置，输出退化已使本次运行结果不可接受

关键约束：

- `degraded/error` 只影响运行结论与观测，不改变主线程行为
- 即使配置为 `error`，也不能把系统切回等待式处理

## 8. 错误处理规则

### 8.1 output_ring 满

视为实时退化事件，而不是主线程阻塞条件。

处理规则：

- 立即记账
- 立即释放 pool slot
- 升级运行结论
- 主线程继续

### 8.2 recycle_ring 压力

这类事件也不能反向演变成主线程等待。

本阶段要求至少做到：

- 统计可观测
- 不让 finalize 丢弃路径依赖 recycle 才能释放 slot

### 8.3 consumer 慢

consumer 慢不是主链错误，它只是在输出侧形成退化风险。

系统应接受这一事实：

- 可以丢一部分 finalized 结果
- 不能牺牲主链实时性

## 9. 测试设计

本阶段必须按 TDD 推进。

最关键的不是补更多“功能 happy path”测试，而是锁死行为边界。

### 9.1 零阻塞退化测试

基于现有 `test_cpi_output_pipeline.cpp` 增加：

- ring 满时不会等待
- pool slot 会立即可复用
- dropped output 统计增加

### 9.2 慢消费者压力测试

基于现有 `test_slow_consumer_pressure.cpp` 增加：

- 慢消费者下主循环仍能完成
- 不会因为 pool 迟迟不归还而使整个链路停摆
- 可观测到输出退化而不是主链阻塞

### 9.3 运行结论测试

新增/补齐：

- `output_drop_policy=degrade` 时最终为 `degraded`
- `output_drop_policy=error` 时最终为 `error`
- 两种模式都不改变零阻塞行为

### 9.4 summary / panel 测试

补齐：

- 状态面板展示输出退化字段
- 最终 summary 展示 drop/full/degraded 结论

## 10. 实施顺序

本阶段按以下顺序落地：

### 任务 1：锁定零阻塞行为测试

- 先补失败测试
- 证明 ring 满时当前行为还没有被完整表达成显式契约

### 任务 2：实现 coordinator 的零阻塞退化路径

- 在 `finalize_active()` / `finalize_previous()` 固化非阻塞输出策略

### 任务 3：接入配置项

- 增加 `output_drop_policy`
- 增加 ring 容量配置

### 任务 4：补齐观测面

- metrics
- summary
- status panel

### 任务 5：更新文档

- `README.md`
- `docs/当前接收端代码实现与执行逻辑详解.md`
- 明确说明“实时优先、输出可退化、主链禁止阻塞”

## 11. 验证边界

本仓库是 Linux-only 工程。

因此：

- Windows 本地只负责代码和文档修改
- 权威构建、测试和运行态验证必须在 Linux 服务器完成
- 本阶段的服务器验证重点不是证明最终吞吐极限，而是证明：
  - 慢消费者下主链不阻塞
  - 输出退化可观测
  - 配置切换后运行结论符合预期

## 12. 结论

对于当前 `rx_tech_demo` 的下一阶段，最合理的推进方向不是继续扩入口，也不是直接承诺 5Gbps 终态，而是先把 finalize 之后的输出链路明确收敛为：

- 零阻塞
- 实时优先
- 可退化
- 可观测
- 可配置

这会为后续真实 DPDK 压测和更高吞吐优化提供稳定前提，因为系统首先要明确：

> 输出侧可以退化，但绝不能把接收主链拖入背压。
