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
