# 接收端观测与日志层重构设计

## 1. 背景与目标

当前接收端的观测能力由状态面板、最终人类可读总结、结构化日志、capture 落盘四部分并行组成，但它们之间缺少统一事实源与清晰职责边界，表现为：

- 同类信息重复展示，实时面板、最终总结、日志文件之间语义重叠
- `capture_packets.bin` / `capture_index.csv` 默认持续全量落盘，体积增长不受业务目的约束
- release 与 debug 的差异过于粗糙，缺少稳定的 release 级精简运行留痕
- 状态面板依赖累计统计判断“业务流量是否存在”，无法表达“流量已中断”
- 交互式终端同时承载状态面板与其他输出，存在首次运行时交叠刷屏风险

本次设计目标是把当前观测能力重构为“日志中心型”架构，以高性能日志库承载统一事件流，再从同一事件流派生：

- release 精简运行日志
- 固定头部与最终摘要
- debug 诊断产物
- 实时状态面板

本设计只定义当前阶段可落地的观测与日志层重构，不把接收端描述为完整终态产品。

## 2. 设计原则

### 2.1 单一事实源

结构化事件日志是唯一权威事实源。状态面板、固定头部、最终摘要、debug 样本都从统一事件模型与聚合状态派生，不再各自维护独立语义。

### 2.2 观测分层

不同输出层解决不同问题：

- release 解决“本次运行在什么配置和环境下发生了什么关键事件”
- debug 解决“如何定位协议、顺序、退化和样本问题”
- 面板解决“此刻系统当前状态如何”
- 摘要解决“本次运行最终结论是什么”

### 2.3 热路径克制

接收主链路只上报领域事实和轻量计数，不在热路径中拼接大段文本、做复杂分析或写高体积调试输出。

### 2.4 结构化优先

文本是视图，结构化事件才是原始数据。所有关键状态变化必须先以结构化事件形式表达，再决定是否派生文本视图。

### 2.5 目的性产物

落盘产物必须服务明确目的。默认禁止“因为已经实现所以持续全量录制”的模式。

## 3. 方案选型

本轮比较过三种路线：

1. 日志中心型
2. 摘要中心型
3. 抓包中心型

最终采用“日志中心型”，原因如下：

- 最适合把 release、debug、面板、摘要统一到一套事件模型上
- 最利于后续脚本分析、跨运行对比、结果归档
- 可以把当前 capture 的职责降为可选 debug 产物，而不是主结果形态

## 4. 技术选型

### 4.1 高性能日志库

采用 `spdlog` 作为主日志基础设施，目标能力包括：

- 异步日志队列
- 多 sink 输出
- 按级别过滤
- JSONL 主事件流
- 滚动文件
- 可控 flush 策略

当前仓库已存在 `spdlog` 可选依赖与 `structured_logger.cpp`，本轮重构应将其从“可选结构化日志后端”提升为“观测子系统基础设施”。

### 4.2 为什么不是业务代码直接依赖 spdlog

不推荐让 `core/protocol/runtime` 直接面向 `spdlog` 写日志。最佳实践是：

- 业务层只发领域事件
- 观测层内部统一适配 `spdlog`
- 业务层不感知 sink、滚动、JSON 渲染、文本格式等后端细节

## 5. 目标架构

### 5.1 分层结构

目标结构如下：

- `L0 结构化事件流`
  - `events.jsonl`
  - 唯一事实源
- `L1 release 运行日志`
  - `L0` 的关键事件子集
- `L2 debug 扩展日志`
  - 在 `L1` 基础上追加诊断事件与调试样本
- `L3 派生视图`
  - 状态面板
  - 固定头部
  - 最终摘要

### 5.2 模块边界

建议在现有 `src/receiver/sidecar` 下演进为观测子系统，新增或重组下列模块：

- `event_schema`
  - 定义事件名、公共字段、payload 结构
- `event_logger`
  - 统一事件写入入口
- `sink_factory`
  - 管理 `spdlog` async logger、sink、滚动策略
- `status_aggregator`
  - 维护运行期聚合状态
- `traffic_state_tracker`
  - 管理业务流状态机
- `status_panel_renderer`
  - 负责交互式终端状态面板渲染
- `summary_renderer`
  - 输出 `summary.json` 和 `summary.txt`
- `debug_artifact_writer`
  - 输出首个 CPI 样本、reject/gap 样本等
- `run_context_snapshot`
  - 生成固定头部与运行上下文信息

### 5.3 与现有文件的关系

建议重构方向如下：

- `src/receiver/sidecar/structured_logger.cpp`
  - 升级为 `event_logger + sink_factory`
- `src/receiver/sidecar/runtime_status_reporter.cpp`
  - 从“直接拼 summary 与 panel”调整为“调度快照、驱动派生视图”
- `src/receiver/sidecar/status_panel.cpp`
  - 保留纯渲染职责，不再定义业务状态语义
- `src/receiver/core/owner_loop_summary.cpp`
  - 从文本拼接中心下沉为 `summary_renderer` 的一部分

## 6. 事件模型

### 6.1 事件类型

事件分为两类：

- 边界事件
  - 表达明确发生的状态变化或关键动作
- 快照事件
  - 表达某一时刻聚合状态

### 6.2 公共字段

所有事件统一包含以下公共字段：

- `ts_wall`
- `ts_monotonic_ns`
- `run_id`
- `backend`
- `build_mode`
- `schema_version`
- `event`
- `level`
- `payload`

### 6.3 release 关键事件

release 模式保留下列事件：

- `run.header`
- `run.started`
- `config.resolved`
- `backend.ready`
- `traffic.first_seen`
- `traffic.interrupted`
- `traffic.resumed`
- `pipeline.degraded`
- `pipeline.error`
- `status.snapshot`
- `artifact.emitted`
- `run.stopped`
- `run.failed`

### 6.4 debug 追加事件

debug 模式在 release 基础上追加：

- `debug.reject_sample`
- `debug.sequence_gap_detail`
- `debug.first_cpi_captured`
- `debug.parser_sample`
- `debug.state_transition_reason`
- `debug.counter_rollup`

### 6.5 事件设计约束

- 一条事件只表达一个事实
- 数值字段保持原子化，不拼成解释句
- 高频诊断必须限流
- 事件 schema 需要版本化

## 7. release / debug 分层策略

### 7.1 release

release 默认保留：

- 固定头部
- 关键阶段事件
- 周期性关键快照
- 最终摘要
- 严重错误

release 不保留：

- 每包解析细节
- 高体积样本
- 长预览 hex
- 高频 reject 逐条输出

### 7.2 debug

debug 在 release 基础上增加：

- 限流后的异常样本
- 顺序/gap 细节
- 首个有效 CPI 样本
- 供脚本分析的诊断原材料

debug 的目标不是“全量都打”，而是“只为定位问题补充必要证据”。

## 8. 固定头部与最终摘要

### 8.1 固定头部

固定头部由首条 `run.header` 事件派生，并以两种形式存在：

- 机器可读：`events.jsonl` 第一条事件
- 人类可读：`run_header.json` 与摘要顶部文本头

建议至少包含：

- 运行时间
- `run_id`
- 主机名
- 平台/内核
- Git commit 或工作区标识
- backend 类型
- build mode
- 关键协议配置
- 关键 ingress 配置
- 关键 output / logging 配置
- sender 或输入源标识
- 产物目录

### 8.2 最终摘要

最终摘要从聚合状态派生，不直接依赖临时打印文本。建议同时输出：

- `summary.json`
- `summary.txt`

用于表达：

- 本次运行最终状态
- 关键性能指标
- 关键退化/错误统计
- 当前验证边界内可宣称的事实

## 9. capture / debug 产物策略

### 9.1 产品定位调整

默认取消“持续全量 capture”的产品定位。当前 `capture_packets.bin` / `capture_index.csv` 不再作为常规运行产物主路径。

### 9.2 产物分级

定义三类产物：

- A 类：运行级产物
  - `events.jsonl`
  - `summary.json`
  - `summary.txt`
  - `run_header.json`
- B 类：debug 样本产物
  - `first_cpi_payload.bin`
  - `first_cpi_index.csv`
  - `first_cpi_manifest.json`
  - `reject_samples.jsonl`
  - `sequence_gap_samples.jsonl`
- C 类：重型录制产物
  - 原始帧滚动录制
  - 短时窗口全量 payload capture

### 9.3 默认策略

- release
  - 仅输出 A 类产物
- debug
  - 默认输出 A 类产物
  - 可选输出 B 类产物
- 重型录制
  - 必须显式启用
  - 必须带体积与时长限制

### 9.4 首个有效 CPI 样本

debug 默认推荐保留“首个有效 CPI 样本”，而不是全量持续 payload capture。

建议样本包含：

- `first_cpi_payload.bin`
- `first_cpi_index.csv`
- `first_cpi_manifest.json`

样本必须自解释，manifest 需要说明：

- 样本录取原因
- CPI 标识
- 是否完整
- 覆盖到的 channel / prt
- 总字节数
- 起止时间

### 9.5 原始帧录制定位

`RawFrameRecorder` 保留，但重新定义为“重型专项调试能力”，仅用于：

- 链路层异常定位
- DPDK/驱动问题取证
- 需要保留二层原始证据的专项场景

默认关闭，不进入常规结果产物路径。

## 10. 实时状态面板设计

### 10.1 面板定位

状态面板是“当前运行态视图”，不是全程复盘工具。

### 10.2 流量状态机

引入明确状态机：

- `idle`
- `active`
- `interrupted`

状态迁移：

- 首次收到有效业务包：`idle -> active`，发 `traffic.first_seen`
- 超过中断阈值未收到有效业务包：`active -> interrupted`，发 `traffic.interrupted`
- 中断后再次收到有效业务包：`interrupted -> active`，发 `traffic.resumed`

“有效业务包”定义为通过主协议校验并进入业务处理链路的业务数据，不包括 ARP、过滤报文或无效样本。

### 10.3 面板显示块

面板只保留五块：

- `运行`
  - 运行状态
  - 运行时长
  - `run_id`
- `业务流`
  - 当前流量状态
  - 首次检测时间
  - 最近一次有效流量时间
  - 最近一次中断时间
  - 最近一次恢复时间
- `性能`
  - 当前窗口吞吐
  - 累计有效包
  - 累计丢弃
  - CPU
- `健康`
  - gap
  - 退化次数
  - 错误次数
- `进度`
  - 当前活跃 CPI / PRT
  - 覆盖概览

面板不再展示：

- 大段链路层明细
- 高频调试细节
- 最终复盘性质字段

### 10.4 终端输出规则

交互式终端只能有一个前台 writer。状态面板是终端主区域唯一 writer，其他事件输出只能：

- 写文件
- 写单独 sink
- 或经由统一渲染器进入独立事件区

禁止多个模块直接向同一交互式 TTY 随机写入。

## 11. 代码架构调整建议

### 11.1 是否应该独立日志层

建议独立，但独立的是“观测子系统”，不是与项目语义脱节的通用 logging helper。

### 11.2 推荐边界

- `ingress / protocol / core / runtime`
  - 只上报领域事件与轻量计数
- `sidecar/observability`
  - 统一管理事件写入、状态聚合、视图派生、debug 产物
- `spdlog`
  - 仅作为观测子系统内部基础设施

### 11.3 最佳实践

- 业务代码不直接依赖 `spdlog`
- 结构化事件优先于文本日志
- 面板、摘要、debug 产物共享同一聚合状态
- 高频日志异步化、限流、可降级
- 日志队列满时优先丢 debug 事件，不阻塞主链路
- 当前状态与累计结果分离表达

## 12. 落地顺序建议

建议按阶段实施，避免一次性大改：

### 阶段 1：事件基础设施落地

- 定义事件 schema
- 引入统一 `event_logger`
- 用 `spdlog` async logger 替换当前 `structured_logger`
- 打通 `events.jsonl`

### 阶段 2：release 级观测收敛

- 实现 `run.header`
- 实现 `status.snapshot`
- 生成 `summary.json` / `summary.txt`
- 保留 release 精简事件集

### 阶段 3：流量状态机与面板重构

- 引入 `traffic_state_tracker`
- 改造状态面板为当前态视图
- 解决首次运行交叠与流量中断表达问题

### 阶段 4：debug 产物策略重构

- 移除默认全量 capture 路径
- 引入首个有效 CPI 样本产物
- 将解析分析逻辑更多外移至脚本

### 阶段 5：重型专项录制收口

- 保留原始帧录制
- 增加启停事件、上限声明、专项定位文档

## 13. 风险与边界

- 事件 schema 一旦对外用于脚本分析，后续变更需要版本兼容
- 异步日志队列必须明确降级策略，避免反向拖垮接收主链路
- 流量中断阈值需要结合协议节奏和运行场景定义，不能仅靠主观常数
- 本轮只定义日志与观测层重构，不等同于完成真实闭环链路验证

## 14. 验收标准

设计完成后的实现应至少满足：

- release 模式下存在稳定、精简、可归档的结构化事件流
- debug 模式下具备首个有效 CPI 样本与受控诊断输出
- 状态面板能正确表达“未检测到 / 正常 / 中断”
- 面板与日志不再发生语义分裂
- 默认运行不再持续全量写 `capture_packets.bin`
- 固定头部与最终摘要可直接用于后续数据分析与运行对比
