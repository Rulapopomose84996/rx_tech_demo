# DESIGN

## 分层

- benchmark core: 配置、场景、指标、报告、调度
- backends: socket / AF_XDP / DPDK
- modes: rx_only / parse / spsc
- apps: 各后端独立入口

## 设计原则

- 公共后处理与公共指标必须独立于后端
- 后端只负责收包与释放
- 模式层只处理统一 `PacketDesc`
- 工程验证以服务器原生构建为准，不以本地编译结果作为依据

## 构建与验证约束

- Windows 工作区不做本地编译
- 提交后通过 `ssh kds` 到服务器验证
- 服务器工作区使用 `/home/devuser/WorkSpace/rx_tech_demo`
- 共享第三方依赖缓存使用 `/home/devuser/WorkSpace/ThirdPartyCache`
