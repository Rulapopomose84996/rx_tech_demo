# AF_XDP Receiver Runtime Plan

## Goal

在 AF_XDP 主线已跑通的基础上，修复接收端运行时可观测性和发送端反馈链路，要求前台手动模式下稳定输出 10 秒状态快照，并向 sender 周期反馈接收量与丢包率，且不影响接收线程主路径。

## Current Phase

- [completed] Phase 1: 确认 AF_XDP 主线和 queue 22 的真实接收链路
- [completed] Phase 2: 修复前台状态输出与 sender 反馈通道
- [pending] Phase 3: 梳理 AF_XDP 当前实现的高优先级优化方向并给出设计
- [in_progress] Phase 4: 按优先级逐步优化 AF_XDP 主路径
- [pending] Phase 5: 更新 sender 对接文档并统一运行口径
- [pending] Phase 6: 提交并同步本地/服务器工作区，收敛到干净状态

## Key Decisions

- 直接在服务器主工作区修复运行时问题，不再进入 worktree。
- 运行时状态输出和反馈通道的修复不能影响 AF_XDP 接收线程主路径。
- sender 需要的最小反馈字段至少包含：接收数据量、接收报文数、丢包率、接收速率。
- sender 对接文档 `docs/发送端对接说明_AF_XDP.md` 必须同步更新。
- AF_XDP 下一阶段先聚焦接收主路径效率与稳定性，再处理可观测性扩展。
- 优化设计需要以当前 `src/backends/af_xdp/src/xdp_backend.cpp` 的单 socket、单 UMEM、同步 poll/recycle 模型为基线。
- P0 第一批优化先不拆线程，优先做 ring 参数配置化、peek-first 接收、per-burst 时间戳和非阻塞 fill 回填。

## Errors Encountered

| Error | Attempt | Resolution |
|-------|---------|------------|
| `rg.exe` 在当前环境启动失败（拒绝访问） | 1 | 改用 `Get-ChildItem` / `Get-Content` 等 PowerShell 方式继续检查代码 |
| Windows 下直接构建命中现有 `bench_runner.cpp` 兼容性错误 | 1 | 按项目约束切换到 WSL 构建与测试，不在 Windows 原生编译路径上继续排查 |
| 服务器 `.worktrees/` 尚未被 `.gitignore` 忽略 | 1 | 进入服务器仓库后先修正 `.gitignore`，提交后再创建 worktree |
| 服务器 AF_XDP 自检脚本执行权限缺失 | 1 | 改用 `bash ./scripts/...` 方式执行，避免被文件权限阻断 |
| 反馈链路在真实 sender 环境下超时 | 1 | 定位为接收端周期输出未接到 stdout、反馈发送周期过长、反馈字段不完整且发送结果无观测 |
