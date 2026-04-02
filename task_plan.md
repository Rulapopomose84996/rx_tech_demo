# Documentation Sync Plan

## Goal

把规划文件和项目文档统一到当前仓库代码事实，避免继续把旧的 AF_XDP benchmark 叙述写成现状。

本次同步范围：

- `task_plan.md`
- `findings.md`
- `progress.md`
- `docs/当前接收端代码实现与执行逻辑详解.md`
- `README.md`
- `AGENTS.md`

## Current Phase

- [completed] Phase 1: 读取现有规划文件和目标文档，确认哪些内容已经偏离当前代码
- [completed] Phase 2: 在独立工作树 `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-docs-current-impl` 上建立文档修改分支
- [completed] Phase 3: 基于当前 `src/receiver` 实现重新梳理主线接收链路、解析规则、落盘行为和验证边界
- [completed] Phase 4: 回写规划文件、README、AGENTS 和代码逻辑说明文档
- [completed] Phase 5: 做一致性检查，确认文档口径与当前代码实现一致

## Key Decisions

- 当前主线以 `src/receiver` 中的 DPDK 接收链路为准。
- `src/legacy` 下的 AF_XDP 代码与相关脚本、配置只作为兼容/历史参考，不再写成当前主线。
- 文档重点描述已经落地的事实：
  - DPDK 收包
  - IPv4/UDP payload 提取与分片重组
  - 样本协议解析与校验
  - 实时落盘到 `capture_packets.bin` / `capture_index.csv`
  - 终端摘要与统计
- 本次是文档同步，不额外改代码行为。
- 本次会明确说明验证边界：Windows 会话中只做代码阅读和文档更新，不把本地结果写成权威验证。

## Errors Encountered

| Error | Attempt | Resolution |
|-------|---------|------------|
| 主工作区存在未提交改动，不适合直接改文档 | 1 | 新建独立工作树和分支 `codex/docs-current-impl` 进行修改 |
| 现有文档仍以 AF_XDP benchmark 框架为主叙述 | 1 | 直接按当前代码重写关键章节，不在旧叙述上零碎修补 |
