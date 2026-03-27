# Findings

## 2026-03-25

- `docs/发送端默认实现说明.md` 明确了发送端基线：单进程三线程、三口独立 UDP 流、单队列发送、`block-batch fast path`，已验证 `5.5+ Gbps / port`，三口映射固定为 `4010/4011/4012 -> 5010/5011/5012`。
- `docs/接收端适配说明.md` 明确接收语义：三口独立 socket/线程或队列、按 `(port, block_id)` 重组、依赖 `DemoHeader` 的 `magic/version/flags/stream_id/block_id/block_bytes/frag_idx/frag_count/frag_payload_bytes`，并要求每口统计吞吐、丢片、重组与错误。
- 当前仓库 README 表明项目仍处于“benchmark 骨架 + backend 占位 + 测试骨架”阶段，现有成果重点在 backend 框架、dry-run、结果输出以及 AF_XDP 最小闭环，不是三口协议接收成品。
- `src/benchmark_core/src/parser.cpp` 仅按 `packet.len > 0` 生成占位 `ParsedPacketMeta`，未解析任何真实协议字段。
- `src/modes/parse/src/parse_mode.cpp` 仅逐包调用 `parse_packet()` 并累加 parsed/drop 计数，没有 block 级重组或每口独立统计。
- `src/backends/socket/src/socket_backend.cpp` 目前是单 `recv_fd` 的通用 UDP backend，配置项仅支持单 `bind_address + udp_port + queue_id`，不具备三口 socket 编排。
- `src/benchmark_core/include/rxtech/metrics.h` 与 `src/benchmark_core/src/metrics.cpp` 当前指标偏 benchmark 摘要，缺少接收端文档要求的 `reassembled blocks / missing fragments / duplicate fragments / invalid header count / reassembly timeout count / per-port throughput`。
- `tests/unit` 已有 `test_parser.cpp`、`test_metrics.cpp` 等骨架，适合作为新增协议解析与重组测试入口；当前尚未看到针对 `DemoHeader`、重组器、三口配置的测试。
- `src/apps/common/app_main_common.cpp` 当前 dry-run 输出仍是单 `queue_id`/单 `packet_size` 视角，尚未暴露三口 endpoint 或重组超时等联调关键配置。
- `src/benchmark_core/src/bench_runner.cpp` 已具备 step 级结果写出能力，适合作为增加 per-port JSON/CSV 明细的承载点，不需要另起结果体系。
- 正式计划已写入 `docs/superpowers/plans/2026-03-25-sender-receiver-link-adaptation.md`，拆分为协议基线、重组、三口 socket、配置文档、服务器联调五个任务。
- 已新增 `src/benchmark_core/include/rxtech/demo_protocol.h`，在仓库内固化接收端当前依赖的 sender 协议基线，并将 `parser` 升级为真实 `DemoHeader` 字段解析。
- 已新增 `src/benchmark_core/include/rxtech/reassembly.h` / `src/benchmark_core/src/reassembly.cpp`，实现按 `(port_id, block_id)` 的 block 重组、乱序接收、重片识别和超时淘汰。
- `parse_mode` 已接入重组器，并通过 `MetricsCollector` 上报 per-port 包量、非法头、重组成功、缺片、重片和超时。
- `socket_backend` 已改为支持多接收端口；在 `enable_internal_traffic=true` 时会注入合法的单片 `DemoHeader` 报文，可用于三口本地 smoke。
- `RunSummary` 已新增 `per_port` 统计，并写入 `summary.json` 和 `per_port_summary.csv`；dry-run 现已打印三口 endpoint 与重组超时。
- 已新增 `configs/rx_sender_link.conf`、`scenarios/sender_link_smoke.yaml`、`scripts/run_sender_link_smoke.sh` 作为 sender-receiver 联调入口骨架。
- WSL 本地验证已通过：
  - unit: `test_parser` / `test_rx_config` / `test_merge_config` / `test_reassembly` / `test_metrics`
  - integration: `rxtech_integration_tests` / `test_socket_three_port`
  - app dry-run: `rxbench_socket --config ./configs/rx_sender_link.conf --scenario ./scenarios/sender_link_smoke.yaml --dry-run`

## AF_XDP Transition

- 服务器基线文档 `docs/设计方案/服务器环境基线 (V2.1).md` 明确数据面目标是 Intel X710 (`receiver0`~`receiver3`) + `i40e` 驱动，业务端口 `9999/udp` 已放行，CPU/NUMA 也已为高性能接收预留。
- 服务器当前硬件具备 AF_XDP 基本条件：ARM64、X710、多队列、`receiver0/1/2` 链路 `UP`，`receiver3` 当前 `NO-CARRIER/DOWN`。
- 服务器当前软件状态与文档基线存在偏差：`net.core.rmem_max=67108864`、`udp_mem=204800 409600 819200`、`netdev_max_backlog=20000`，均低于文档中为高吞吐接收记录的值。
- 服务器 AF_XDP 自检显示：
  - 内核配置项 `CONFIG_BPF / CONFIG_XDP_SOCKETS / CONFIG_BPF_JIT` 存在
  - `bpftool` 可用
  - `libbpf 0.8.1` 可用
  - `libxdp:not-found`
  - `receiver3` 驱动为 `i40e 2.25.9`
- `scripts/check_af_xdp_env.sh` 与 `scripts/build_af_xdp_bind_probe.sh` 在服务器上无执行位，但可通过 `bash ./scripts/...` 继续使用。
- 服务器仓库当前没有 `.worktrees/` / `worktrees/` 目录，且 `.worktrees/` 尚未被 `.gitignore` 忽略。
- `receiver0` 当前真实 sender0 流量 RSS 落在 `queue 22`，不是 `queue 0`。
- `rxbench_xdp` 在 `receiver0 + queue 22 + mode=parse` 上已验证：
  - `parsed_packets > 0`
  - `invalid_header_count = 0`
  - `reassembled_blocks > 0`
- 当前运行时问题根因已定位：
  - 周期状态输出逻辑存在，但 CLI 未将输出流接到 `stdout`
  - receiver 反馈报文仅在状态快照时发送，默认 10s 一次，时效性不足
  - 反馈 JSON 字段不完整，缺少 sender 直接需要的接收数据量字段
  - `sendto()` 返回值未检查，且未绑定源地址/接口，导致反馈链路无可观测性
- 当前服务器主工作区运行时修复已落地：
  - 前台 CLI 已调用 `runner.set_status_output(&std::cout)`
  - 反馈 JSON 当前字段至少包含 `rx_packets / rx_bytes / rx_mib / dropped_packets / loss_rate / queue_id / gbps`
  - 反馈目标和源地址已通过配置项控制：
    - `feedback_host`
    - `feedback_bind_host`
    - `feedback_port`
    - `feedback_interval_seconds`
- 已通过 loopback 验证收到的反馈报文示例：
  - `{"type":"receiver_feedback","rx_packets":273681,"rx_bytes":393917962,"rx_mib":375.669,"dropped_packets":0,"loss_rate":0,"queue_id":22,"gbps":3.15134}`

## 2026-03-26 AF_XDP Optimization Baseline

- 当前 AF_XDP 主实现集中在 `src/backends/af_xdp/src/xdp_backend.cpp`，仍是单 socket / 单 UMEM / 单 queue 模型。
- `recv_burst()` 每次调用都会先执行 `poll(..., 100)`，即使 ring 内已有包也先走系统调用路径；这更偏“保守省 CPU”而不是“低延迟高吞吐”。
- `release_burst()` 在归还 frame 时要求一次性 reserve 整个 `burst.packets.size()`，失败时用 busy loop + `recycle_completion(count)` 重试，存在主线程自旋风险。
- `configure_umem()` 只在初始化时向 fill ring 预填 `kFillRingSize=256` 个 frame，但 UMEM 总 frame 数是 `4096`；当前实际可用 receive 深度明显低于已分配 UMEM 容量。
- `recv_burst()` 在每个 descriptor 上都调用 `steady_clock_now_ns()`，属于每包时间戳开销；如果上层只需要 burst 级时间语义，这里有可裁剪空间。
- 当前 ring 参数全部写死为 `256`，没有按 NIC 队列压力、场景包长或用户配置调优的入口，优先级高于做更多统计字段。
- 现阶段 AF_XDP 后端没有独立的单元测试覆盖 ring 回填策略、poll 策略或 budget 边界；后续逐步优化时需要补最小可回归测试。
- 用户确认本轮目标约束：
  - 主要目标是单核心下逼近 `5.5 Gbps`，并降低丢包。
  - 允许最多使用 `1~2` 个 CPU 核，但优先观察“1 个核心能做多少”。
  - 需要比较两种工作形态：`纯接收` 与 `接收+解析`，以判断后续是否需要把解析拆出主线程。

## 2026-03-26 AF_XDP Optimization Phase 1

- 已新增 AF_XDP 调优配置项：
  - `xdp_rx_ring_size`
  - `xdp_tx_ring_size`
  - `xdp_fill_ring_size`
  - `xdp_completion_ring_size`
  - `xdp_frame_size`
  - `xdp_frame_count`
  - `xdp_poll_timeout_ms`
- `XdpBackend` 已从固定常量改为按 `RxConfig` 计算实际 ring/frame 参数，并做最小边界保护与 2 的幂对齐。
- `recv_burst()` 已改为 `peek RX ring -> 空时按配置决定是否 poll -> per-burst timestamp`，去掉原先“每次先 `poll(100ms)`、每包单独取时间戳”的热路径开销。
- `release_burst()` 已去掉“整批 reserve fill ring 失败后 busy loop”的策略，改为把 frame 地址先放入 `pending_fill_addrs`，再按当前可用槽位尽力回填，避免主线程在 fill ring 上自旋。
- 当前实现仍是单线程接收+处理模型；这轮优化只是在不拆线程的前提下，先把接收后端固定成本降下来。
- 服务器验证结果：
  - `ssh kds` 可用
  - `/home/devuser/WorkSpace/rx_tech_demo` 构建成功
  - `build/tests/unit` 下 `test_rx_config`、`test_merge_config` 通过
  - `./build/src/apps/rxbench_xdp --config ./configs/af_xdp_receiver0.conf --dry-run` 通过
- 服务器构建仍会报 AF_XDP/libbpf 旧接口弃用警告，但本轮未引入新的编译错误；后续若切 `libxdp` 再统一处理。
- `configs/af_xdp_receiver0.conf` 已固化吞吐向参数：
  - `xdp_rx_ring_size=1024`
  - `xdp_fill_ring_size=2048`
  - `xdp_completion_ring_size=2048`
  - `xdp_frame_count=4096`
  - `xdp_poll_timeout_ms=0`
- 服务器 `--dry-run` 已确认新配置文件可用。
- `cpu_cores` 当前只进入 `RxConfig`，AF_XDP backend/runner 并未实际做线程绑核；要做单核对照，必须在启动命令外层用 `taskset` 或同等手段。
- 服务器当前没有现成的高吞吐本地 sender 工程；仓库内只有 `tools/raw_eth_sender.py`，它适合作为 ingress 路径 smoke，不适合作为 `5.5 Gbps` 单核性能结论依据。
- 服务器上可见的额外本地发流口是 `enP1s24f0`（`ngbe`，NUMA 1，本地 CPU `16-31`），理论上适合做“同 NUMA 的本机发流”候选，但当前会话无无密码 `sudo`，无法完成需要 root 的实跑验证。
