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
