# 可观测性重构分支阶段总结

## 复盘模式

- 模式：`limited`
- 原因：
  - 已有明确的意图来源：可观测性重构设计 spec 与各阶段 implementation plan
  - 已有明确的实现来源：阶段 1 到阶段 5 的代码与测试提交
  - 已有执行证据：WSL fallback 下的构建、单测、fake integration、socket loopback 运行日志
  - 缺少权威 Linux 服务器验证与真实 10G 网口链路验证，因此不能把当前结果表述成最终完成

## 分支范围

- 分支：`codex/observability-logging-stage1`
- 基线快照提交：`0e91a33`
- 本轮可观测性相关提交：
  - `bd8c7a2` `feat: add stage1 event schema foundation`
  - `802e12d` `refactor: route structured logging through event logger`
  - `b71ce42` `feat: emit stage1 runtime events to events jsonl`
  - `4635393` `test: stabilize local stage1 regression checks`
  - `eee0983` `docs: add stage2 release observability plan`
  - `e69bfb2` `feat: add stage2 release observability artifacts`
  - `ab69005` `docs: add stage3 traffic panel plan`
  - `72c5e92` `feat: add stage3 traffic state and panel updates`
  - `624f70a` `docs: add stage4 capture policy plan`
  - `6e257b1` `feat: add stage4 capture policy config`
  - `6bba236` `feat: add stage4 capture policy pipeline`
  - `f71a0a7` `docs: add stage5 heavy recorder plan`
  - `7b483af` `feat: add stage5 heavy recorder observability`

## 已实现能力

### 阶段 1：事件基础设施

- 已落地统一事件模型：
  - `EventEnvelope`
  - `EventLogger`
  - `events.jsonl`
- 已把旧 `structured_log(...)` 兼容层改接到事件写入路径
- 已打通最小运行事件：
  - `run.started`
  - `run.stopped`
  - `run.failed`

### 阶段 2：release 收敛产物

- 已落地：
  - `run.header`
  - `status.snapshot`
  - `summary.json`
  - `summary.txt`
- `summary.json` / `summary.txt` 已由 `ReceiveRunner` 在运行目录中自动生成

### 阶段 3：流量状态与状态面板

- 已落地 `TrafficStateTracker`
- 已引入流量状态：
  - `idle`
  - `active`
  - `interrupted`
- 已打通事件名：
  - `traffic.first_seen`
  - `traffic.interrupted`
  - `traffic.resumed`
- 状态面板已从“链路判定”切换到“业务流状态”语义

### 阶段 4：capture 策略重构

- 已引入 `CapturePolicy`
- 当前默认 capture policy 为：
  - `first_effective_cpi`
- 已新增 `DebugCaptureWriter`
- 已把 `ReceiveRunner` / `OwnerLoop` 的 payload/index 写入切到 policy 路由
- `summary.json` 现在会输出：
  - `summary.capture.capture_policy`

### 阶段 5：重型专项录制收口

- `RawFrameRecorder` 已明确作为“重型专项录制”路径
- 已新增生命周期事件：
  - `raw_record.started`
  - `raw_record.stopped`
  - `raw_record.failed`
- `summary.json` / `summary.txt` / human summary 中已明确标出：
  - `raw_record_role = heavy_debug_recorder`
  - `原始帧录制定位：重型专项录制`

## 当前用户可见结果

在 WSL fallback 环境下，当前用户已经可以：

- 运行接收端并看到 `events.jsonl`
- 在运行目录中获得 `summary.json` 与 `summary.txt`
- 在无业务流量场景下看到 `status.snapshot.payload.traffic_state = idle`
- 在 fake backend 集成测试中验证：
  - 重型专项录制路径可工作
  - `raw_record.started/stopped` 事件可落盘
  - summary 文本中会明确标识“重型专项录制”

## 已验证证据

### 本地 WSL 构建与测试

已在 WSL fallback 环境运行并通过：

- `test_event_logger`
- `test_summary_renderer`
- `test_traffic_state_tracker`
- `test_debug_capture_writer`
- `test_owner_loop_summary`
- `test_rx_config`
- `test_metrics_exporter`
- `rxtech_integration_fake_tests`

### 本地 WSL 运行验证

已验证：

- `events.jsonl` 可生成
- `run.header` 可生成
- `status.snapshot` 可持续生成
- `summary.json` / `summary.txt` 可生成
- `summary.capture.capture_policy == "first_effective_cpi"`

## 尚未完成的权威验证

当前仍不能宣称以下事项已完成：

- Linux 服务器权威构建通过
- Linux 服务器权威 unit / integration 测试通过
- 真实 10G 网口路径收包验证
- sender 中断后的 `traffic.interrupted` / `traffic.resumed` 生产路径验证
- 首个有效 CPI 样本在真实业务流量下的有效性验证
- 重型专项录制在真实 DPDK 数据面下的保留与裁剪验证

## 风险与注意事项

- 当前分支验证依赖 WSL fallback，只能作为开发侧证明，不能替代服务器权威结论
- 当前仓库还有 1 个未提交的无关文档改动，不属于这轮可观测性提交链
- `RawFrameRecorder` 仍保留旧实现机制，本轮只补了角色定位与事件留痕，没有改写其内部 writer 线程模型

## 建议的下一步

- 优先恢复 `ssh kds` 或等价服务器入口
- 按服务器验证清单逐项完成权威验证
- 服务器验证完成后，再决定是否：
  - 推送当前分支
  - 整理 PR
  - 更新 README / 当前实现详解文档中的验证边界
