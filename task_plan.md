# DPDK Replay Receiver Plan

## Goal

在仓库内直接构建一条最短闭环：

- 从 `data/cpi_0002_complete/cpi_0002_replay_manifest.json` 读取样本
- 按 manifest 顺序重放控制表包和数据包
- 使用 `receiver0` 的 DPDK 接收链路收包
- 只做轻量协议解析和统计
- 以“成功识别控制表包和数据包”为当前阶段成功标志

## Current Phase

- [in_progress] Phase 1: 把计划切换到 replay sender + DPDK 轻量解析闭环
- [completed] Phase 2: 为 replay manifest 解析与发送路径补失败测试
- [in_progress] Phase 3: 实现最小 replay sender
- [pending] Phase 4: 收缩 receiver 主线到 DPDK ingress + parser + validator + 轻量统计
- [pending] Phase 5: 本地代码验证并推送远端
- [pending] Phase 6: 服务器拉取、构建、测试、闭环验证

## Key Decisions

- 当前目标不是完整接收端生命周期管理，而是尽快形成“可发、可收、可解析”的接收代码。
- sender 直接内置在本仓库，不依赖其他仓库或历史脚本。
- sender 输入以 `cpi_0002_replay_manifest.json` 为准，不自行推导包序。
- receiver 成功标志暂不要求完整的 CPI admission/finalize/output。
- receiver 当前保留 `DPDK ingress + PacketParser + PacketValidator + 轻量统计` 即可。
- `AF_XDP` 相关主线演进暂时停止，只作为 `legacy` 保留。
- 项目以 Linux 服务器为唯一正式运行平台；不再新增任何 Windows 平台分支代码。

## Errors Encountered

| Error | Attempt | Resolution |
|-------|---------|------------|
| 任务目标多次从 AF_XDP/完整接收端切换到 DPDK replay 闭环 | 1 | 重新写计划文件，明确当前唯一目标是 replay sender + DPDK 轻量解析 |
| 多次出现 `.git/index.lock` 阻塞提交 | 1 | 每次 Git 操作前先检查并清理残留锁文件 |
| 服务器 `ctest` 经常在构建完成前抢跑 | 1 | 改为顺序执行：先构建，确认完成后单独再跑 `ctest` |
