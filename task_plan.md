# Receiver Refactor And Docs Sync Plan

## Goal

把项目文档同步到当前仓库真实实现，确保以下事实一致：

- 当前主线只有 `src/receiver` 的 DPDK 路径
- 公共头统一位于 `include/rxtech`
- `src/receiver` 已完成目录收平与模块边界重整
- `output` 已并入 `finalize`
- `runtime` 已清理旧 `xdp_/xsk_` 语义
- 权威验证结果来自 `ssh kds` 上的最新隔离目录复验

## Current Status

- [completed] 更新项目根级工作记忆文件：`task_plan.md` / `findings.md` / `progress.md`
- [completed] 更新仓库规则文档：`AGENTS.md`
- [completed] 更新主用户文档：`README.md`
- [completed] 更新历史技术方案文档：`docs/设计方案/AF_XDP_DPDK_准备与分工.md`
- [completed] 更新当前实现说明：`docs/当前接收端代码实现与执行逻辑详解.md`

## Key Decisions

- 文档只描述当前已落地实现，不再把 AF_XDP 作为当前代码主线。
- `include/rxtech` 是唯一公共头入口，不再保留模块级兼容公共头。
- `output` 不再单独作为模块叙述；相关结构已并入 `finalize` 边界。
- 对外验证结果统一引用 `ssh kds` 隔离目录的最新构建与测试结果。

## Next Mainline

- [planned] 在 Linux 服务器上做持续接收联调，确认两阶段状态面板与当前 PRT 覆盖输出符合真实链路观察
- [planned] 基于真实 sender 流量，核对 `current PRT` 覆盖与 `data/cpi_0002_complete/cpi_0002_packet_manifest.csv` 的顺序一致性
- [planned] 若真实链路确认无误，再补更细的结果层摘要，例如“最近完整 PRT”与“当前 PRT 接收中/完整”边界提示
- [planned] 继续把主线验证集中在 `src/receiver` 的 DPDK 路径，不在当前阶段重新引入 AF_XDP 或未落地的四通道假设
