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

### Linux 服务器权威验证（2026-04-12）

已完成的服务器侧动作：

- 本地分支 `codex/observability-logging-stage1` 已确认推送到 `gitea`
- 服务器工作区 `/home/devuser/WorkSpace/rx_tech_demo` 已切换到：
  - 分支：`codex/observability-logging-stage1`
  - 提交：`067818e386fcb40a8f9cbadb03ef3393241f0ccf`
- 已在服务器按 `linux-server-werror` 对应参数手动展开配置：
  - 原因：服务器 `cmake 3.16.5` 不支持 `cmake --preset`
  - 实际配置方式：`cmake -S . -B build-preset-werror -G Ninja -DBUILD_TESTS=ON -DBUILD_REPLAY=OFF -DCMAKE_BUILD_TYPE=Debug -DRXTECH_THIRD_PARTY_CACHE=/home/devuser/WorkSpace/ThirdPartyCache -DRXTECH_WARNINGS_AS_ERRORS=ON`

服务器权威结论：

- 初次权威构建曾被 `src/receiver/sidecar/structured_logger.cpp` 的 `std::filesystem` 用法阻断
- 服务器编译环境：
  - `cmake 3.16.5`
  - `g++ 7.3.0`
- 已修复方式：
  - 将 `structured_logger.cpp` 的目录创建逻辑改为不依赖 `std::filesystem` 的兼容实现
  - 为 `test_event_logger` 补充“镜像日志父目录不存在时自动创建”的回归用例
- 修复后重新在服务器完成：
  - `linux-server-werror` 全量构建通过
  - 关键 unit 闸口通过：
    - `test_event_logger`
    - `test_summary_renderer`
    - `test_traffic_state_tracker`
    - `test_debug_capture_writer`
    - `test_owner_loop_summary`
    - `test_rx_config`
    - `test_metrics_exporter`
  - `rxtech_integration_fake_tests` 通过
  - `socket_loopback` 5 秒固定时长运行通过
- 本次 socket loopback 权威运行产物：
  - 运行目录：`results/20260412_113036_socket_loopback`
  - 已生成：
    - `events.jsonl`
    - `summary.json`
    - `summary.txt`
    - `capture_index.csv`
    - `capture_packets.bin`
- 本次 socket loopback 权威运行观察：
  - 有 `run.header`
  - 有持续 `status.snapshot`
  - 无真实业务流量，因此 `traffic.*` 事件缺失符合预期
  - `summary.txt` / `summary.json` 可解析，后端为 `socket`

本次未安排的验证：

- 未安排真实 sender 配合
- 因此未开展真实 10G 网口链路接收验证
- 未开展 `traffic.interrupted` / `traffic.resumed` 的真实生产链路验证
- 未开展 `first_effective_cpi` 在真实业务流量下的验证
- 未开展 `raw_record` 在真实 DPDK 数据面下的专项验证

## 尚未完成的权威验证

当前仍不能宣称以下事项已完成：

- 真实 10G 网口路径收包验证
- sender 中断后的 `traffic.interrupted` / `traffic.resumed` 生产路径验证
- 首个有效 CPI 样本在真实业务流量下的有效性验证
- 重型专项录制在真实 DPDK 数据面下的保留与裁剪验证

## 风险与注意事项

- 当前分支验证依赖 WSL fallback，只能作为开发侧证明，不能替代服务器权威结论
- 2026-04-12 已发现并修复服务器 `g++ 7.3.0` 下的 `std::filesystem` 兼容性问题，但这也说明后续路径相关代码必须继续遵守该服务器工具链基线
- 当前仓库还有 1 个未提交的无关文档改动，不属于这轮可观测性提交链
- `RawFrameRecorder` 仍保留旧实现机制，本轮只补了角色定位与事件留痕，没有改写其内部 writer 线程模型

## 建议的下一步

- 等真实 sender 就位后，再执行真实 10G 网口与 DPDK 链路验证
- 在后续功能开发中，把服务器 `cmake 3.16.5 + g++ 7.3.0` 视作真实兼容性下限，避免再次引入仅在较新工具链可用的标准库路径能力
- 服务器验证完成后，再决定是否：
  - 推送当前分支
  - 整理 PR
  - 更新 README / 当前实现详解文档中的验证边界
