# Findings

## Current Mainline

- 当前唯一产品化主线位于 `src/receiver`，入口后端为 `DPDK`。
- `src/legacy` 已删除，不再保留 AF_XDP 代码路径。
- 公共头文件统一位于 `include/rxtech`，不再保留 `src/receiver/*/rxtech` 兼容入口。

## Current Source Layout

- `src/receiver/app`
  - `main_dpdk.cpp`
  - `cli_args.cpp/.h`
  - `run_app.cpp/.h`
- `src/receiver/core`
  - `owner_loop.cpp`
  - `owner_loop_summary.cpp`
  - `status_panel.cpp`
  - `internal/`
- `src/receiver/ingress/dpdk`
  - `dpdk_backend.cpp`
  - `arp_responder.cpp`
  - `internal/`
- `src/receiver/protocol`
  - `protocol_sequence_interpreter.cpp`
  - `sample_packet_parser.cpp`
  - `sample_packet_validator.cpp`
  - `udp_payload_assembler.cpp`
- `src/receiver/runtime`
  - `rx_config.cpp`
  - `receive_runner.cpp`
  - `raw_frame_recorder.cpp`
- `src/receiver/storage`
  - `progress_tracker.cpp`
  - `slot_writer.cpp`
- `src/receiver/admit`
  - `cpi_admission.cpp`
- `src/receiver/finalize`
  - `cpi_finalizer.cpp`
- `src/receiver/sidecar`
  - `metrics.cpp`

## Boundary Changes Now Reflected In Code

- `app` 已收平，不再保留 `cli/` 和 `common/` 子目录。
- `core` 的状态面板、汇总辅助、过程态辅助已从 `owner_loop.cpp` 拆出。
- `output` 已不再作为独立模块保留；`CpiOutput` 等结构已并入 `include/rxtech/cpi_finalizer.h`。
- `runtime` 已清理旧 `xdp_/xsk_` 运行时语义，相关字段不再出现在 `RxConfig`、`BackendStats`、`RunSummary` 中。

## Runtime Facts

- 启动入口：`rx_receiver_dpdk`、`rxbench_dpdk`
- 主循环顺序：收包 -> UDP payload 提取 -> 过滤 -> 解析 -> 校验 -> 序列解释 -> 准入 -> slot 写入 -> 进度推进 -> finalize -> capture/summary
- 当前 capture index 表头：
  - `cpi,channel,prt,packet_index,packet_kind,payload_len,valid`
- 原始帧录制由 `RawFrameRecorder` 负责，配置位于 `[raw_record]`

## Validation Boundary

- Windows 仅用于阅读、编辑、提交和文档维护。
- 权威构建与测试结果必须来自 Linux 服务器。
- 最近一轮权威复验目录：
  - `/home/devuser/WorkSpace/rx_tech_demo_codex_validate_20260403`

## Latest Verified Results

- 服务器构建通过。
- unit tests 通过：14/14。
- integration tests 通过：1/1。

## Notes

- 本地 WSL 交叉构建可用于编译检查，但不能替代服务器运行结果。
- 旧 `xdp_*` 配置键当前会被忽略；已由 `test_runtime_legacy_xdp_config` 覆盖。
