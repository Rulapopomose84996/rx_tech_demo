# AF_XDP-Only Receiver Design

**Goal**

将 `rx_tech_demo` 从“多后端骨架工程”收敛为以 `AF_XDP` 为唯一主线的接收端 Demo，删除 `socket` 相关联调资产与入口，保留可复用的协议解析、重组、指标与报告能力，并在服务器 worktree 中直接推进实现与验证。

**Scope**

- 保留：
  - `benchmark_core` 中与协议、重组、指标、结果写出相关的通用能力
  - `af_xdp backend`
  - `rxbench_xdp` 作为唯一主入口
  - AF_XDP 自检、attach、bind、PoC、benchmark 脚本
- 删除：
  - `socket backend`
  - `rxbench_socket`
  - socket 配置、脚本、测试、文档、结果路径
  - 以 socket 为中心的联调说明
- 暂不处理：
  - `dpdk` 主线是否保留，先不删 `dpdk` 相关资产，避免把 AF_XDP 重构与 DPDK 去留绑在一起

**Architecture**

仓库结构收敛为“`benchmark_core + af_xdp backend + shared modes`”。`AF_XDP` 成为唯一可运行接收主线，负责真实网口收包；`benchmark_core` 继续承载 `DemoHeader` 解析、block 重组、per-port 指标和结果输出。运行模型统一为前台长时间手动停止，终端每 10 秒打印一次状态快照。

**Environment Assumptions**

- 服务器开发现场：`/home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver`
- 目标验证口优先 `receiver0`，因为当前 `receiver3` 为 `NO-CARRIER/DOWN`
- `libxdp` 通过共享缓存前缀注入环境：
  - `PKG_CONFIG_PATH=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix/lib/pkgconfig`
  - `LD_LIBRARY_PATH=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix/lib`

**Key Decisions**

- 不再维护 socket 作为兜底链路；后续联调、文档和脚本一律以 AF_XDP 为中心。
- 当前平台的 AF_XDP 现实目标是 `driver + copy`，不是 `zerocopy`。
- AF_XDP 绑定的 queue 必须与 sender 实际流量落点一致，否则会出现“网卡有流量但 XSK 为 0 包”的假阴性。
- 长时间模式的启动由用户手动在终端执行，不再通过 `tmux` 或后台隔离来隐藏输出。

**Planned Deliverables**

1. `socket` 相关代码、配置、脚本、测试、文档移除或从构建中下线
2. `rxbench_xdp` 成为唯一推荐入口
3. AF_XDP 配置与文档明确化：
   - 目标网口
   - queue 绑定方式
   - `driver/copy` 结论
4. 前台长时间模式与 10s 周期状态输出保留到 AF_XDP 主线
5. 服务器 worktree 中完成最小 PoC 和主线验证

**Current Risks**

- `receiver0` 当前真实流量存在，但尚未定位到具体 RX queue，AF_XDP 若绑定错误 queue 会稳定返回 0 包。
- 服务器当前 sysctl 与基线文档存在偏差，可能影响高吞吐 AF_XDP 稳定性。
- `receiver3` 当前不可用，意味着 AF_XDP 调试会触碰真实在线口 `receiver0`，需谨慎恢复现场。

**Validation Strategy**

- 环境验证：
  - `bash ./scripts/check_af_xdp_env.sh receiver0 0`
  - `bash ./scripts/build_af_xdp_bind_probe.sh`
  - `bash ./scripts/build_af_xdp_rx_poc.sh`
- 最小功能验证：
  - `sudo ./build_af_xdp_probe/af_xdp_bind_probe receiver0 <queue>`
  - `sudo ./build/src/apps/rxbench_xdp rx_only "" <output> receiver0 <queue> 2`
- 运行态验证：
  - 前台 `--until-stopped`
  - 10s 周期状态快照
  - 结果文件与终端输出一致
