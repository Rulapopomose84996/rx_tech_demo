# AF_XDP Receiver Transition Plan

## Goal

放弃当前基于内核 UDP socket 的低性能接收路径，转向 AF_XDP 设计；先在服务器上核验硬件/软件支持、完成隔离工作区与环境整备，再推进 AF_XDP PoC 与接收链路实现。

## Current Phase

- [completed] Phase 1: 读取服务器基线与现状，确认 AF_XDP 转向前提
- [in_progress] Phase 2: 在服务器上建立隔离 worktree 并修复环境前置条件
- [pending] Phase 3: 完成 AF_XDP 支持性检查与最小 PoC
- [pending] Phase 4: 输出 AF_XDP 设计/实现方案并开始服务器侧实现
- [pending] Phase 5: 记录验证结果与后续联调入口

## Key Decisions

- 服务器环境是 AF_XDP 主战场；后续改动优先直接在服务器工作区完成，不再走本地构建→推送→拉取链路。
- 先验证 AF_XDP 环境和最小 PoC，再决定是否继续沿用 `receiver3` 作为专用实验口，或切换到 `receiver0` 贴近真实 sender0 流量。
- 必须先满足 `using-git-worktrees`：`.worktrees/` 被 `.gitignore` 忽略后再在服务器侧创建隔离 worktree。
- 当前 socket 路径保留作参考，但不再作为主要设计方向。

## Errors Encountered

| Error | Attempt | Resolution |
|-------|---------|------------|
| `rg.exe` 在当前环境启动失败（拒绝访问） | 1 | 改用 `Get-ChildItem` / `Get-Content` 等 PowerShell 方式继续检查代码 |
| Windows 下直接构建命中现有 `bench_runner.cpp` 兼容性错误 | 1 | 按项目约束切换到 WSL 构建与测试，不在 Windows 原生编译路径上继续排查 |
| 服务器 `.worktrees/` 尚未被 `.gitignore` 忽略 | 1 | 进入服务器仓库后先修正 `.gitignore`，提交后再创建 worktree |
| 服务器 AF_XDP 自检脚本执行权限缺失 | 1 | 改用 `bash ./scripts/...` 方式执行，避免被文件权限阻断 |
