# AGENTS.md

## Project Structure

当前仓库的真实主线是 Linux-only、`datagram-first` 的接收端实现。

- 正式 CLI 入口：
  - `src/receiver/app/main_dpdk.cpp`
  - `src/receiver/app/main_socket.cpp`
- 当前正式运行后端：
  - `rx_receiver_dpdk`
  - `rx_receiver_socket`
- 当前协议主入口：
  - `src/receiver/protocol/udp_datagram_pipeline.cpp`
- 当前主循环与运行装配：
  - `src/receiver/core/owner_loop.cpp`
  - `src/receiver/runtime/receive_runner.cpp`
- 当前主要可观测性产物：
  - `events.jsonl`
  - `summary.json`
  - `summary.txt`

顶层目录职责：

- `include/rxtech`
  - 公共接口、配置和共享数据结构
- `src/receiver/app`
  - CLI 入口与启动装配
- `src/receiver/runtime`
  - 配置、运行器、capture/raw_record 路径
- `src/receiver/ingress`
  - DPDK / socket / replay 等后端接入
- `src/receiver/protocol`
  - datagram 协议处理主线与 legacy frame adapter
- `src/receiver/core`
  - CPI 生命周期与主循环协调
- `src/receiver/sidecar`
  - 状态、指标、日志、summary 输出
- `tests/unit`
  - 单元测试
- `tests/integration`
  - 集成测试
- `docs`
  - 活动文档与评审记录

更细的目录规则应尽量放在更接近目标目录的位置；离当前目录更近的规则优先于本文件。

## Development Startup Commands

本地 Windows 只用于读代码、改代码、改文档，不作为权威验证环境。

服务器相关工作流：

- 默认入口：`ssh kds`
- 需要服务器验证、同步到服务器、远程拉代码构建、或 `ssh kds` 不通时的 fallback 流程时，直接使用技能 [$server-test-via-kds](C:\Users\Klein\.codex\skills\server-test-via-kds\SKILL.md)

当前构建基线：

- 语言标准：`C++17`
- 构建系统基线：`CMake 3.16.x`
- 推荐生成器：`Ninja`

当前服务器工具链现实约束：

- server `cmake 3.16.5` 不支持 `cmake --preset`
- `CMakePresets.json` 可以作为参数来源，但不要假设服务器能直接执行 `cmake --preset ...`
- server `g++ 7.3.0` 属于当前兼容性下限，不要默认 `std::filesystem` 可用

## Test Commands

权威构建、测试、运行验证必须在 Linux 服务器完成。

当前主要服务器构建方式：

- 手动展开 `linux-server-werror` 对应参数
- 或使用仓库内活动脚本

当前高频验证入口：

- unit tests：`tests/unit`
- integration tests：`tests/integration`
- socket 运行验证：`rx_receiver_socket`
- DPDK 运行验证：`rx_receiver_dpdk`

具体命令、工作目录、同步策略、fallback 规则，不在本文件重复维护，统一以 [$server-test-via-kds](C:\Users\Klein\.codex\skills\server-test-via-kds\SKILL.md) 和活动项目文档为准。

## Style And Architecture Constraints

- 当前真实协议主线是 `UdpDatagramPipeline`，不要把 `PacketPipeline` 当作当前主线。
- `PacketPipeline` 当前只应视为 legacy frame adapter；只有明确需要 frame-native 辅助路径时才触碰它。
- 新逻辑优先放入职责最小、边界最清楚的模块，不要把 `app`、`runtime`、`protocol`、`core`、`sidecar` 职责混写到一个文件里。
- 新增路径、目录和文件操作时，优先采用兼容当前服务器工具链的实现，不要只按本地较新标准库能力写代码。
- `socket` 和 `dpdk` 的能力边界要明确区分：
  - `socket` 是正式后端，但不提供 raw frame
  - `dpdk` 是当前真实网卡主路径
- 文档描述必须服从当前真实实现，不要把历史路径、计划路径或理想架构写成已实现事实。
- 遇到服务器现实与文档不一致时，以服务器实际结果为准，并回写到项目文档。

## Prohibited

- 不要在 Windows 上宣称本项目“已构建通过 / 已测试通过 / 已运行验证通过”。
- 不要把服务器工作区当作代码真源。
- 不要默认假设 server 有较新 CMake / GCC / 标准库能力。
- 不要默认假设 `devuser` 拥有任意 root 权限。
- 不要把没有真实 sender / 真实链路参与的验证表述成“真实闭环验证”。
- 不要把局部验证、假数据验证或 fake backend 验证表述成真实数据面成功。
- 不要在 `main` 上堆积实验性改动。
- 不要把未来计划、理想终态或待做事项写成已完成实现。

## Definition Of Done

一个改动只有在以下条件同时满足时，才能对外宣称完成：

1. 代码和文档修改已经落回本地仓库并纳入版本控制。
2. 已按当前项目实际边界说明完成了哪些验证层级：
   - 构建
   - 启动
   - 功能
   - 回归
3. 权威验证结论来自 Linux 服务器，而不是 Windows 本地推断。
4. 若发现新的服务器约束、权限边界、工具链坑点或构建前提，已回写到 `AGENTS.md` 或活动文档。
5. 输出结论与实际证据一致，没有扩大宣称范围。

若缺少真实 sender、真实链路、真实网卡数据面或对应环境前提，必须明确写出“完成到哪一层、尚未完成哪一层”。
