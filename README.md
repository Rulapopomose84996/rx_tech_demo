# rx_tech_demo

`rx_tech_demo` 是一个 Linux-only 的雷达接收端演示工程。当前唯一主线位于 `src/receiver`，默认叙述和权威验证都以 DPDK 接收路径为准。

## 当前定位

- 当前目标是把真实网卡收包、UDP payload 提取、协议解析、轻量统计和旁路落盘串成一条稳定接收链。
- 当前并不宣称已经完成完整业务接收模块；它仍是分阶段演进中的主线 demo。
- 当前代码已经完成一次结构收口：
  - 公共头统一到 `include/rxtech`
  - `src/receiver` 模块实现文件已收平到模块根目录
  - `app` 入口层已收平
  - `output` 已并入 `finalize`

## 目录概览

```text
include/
  rxtech/
src/
  receiver/
    app/
    core/
      internal/
    ingress/
      dpdk/
        internal/
    protocol/
    runtime/
    sidecar/
    storage/
    admit/
    finalize/
tests/
tools/
configs/
scripts/
docs/
```

## 当前运行链路

主线运行顺序是：

1. `rx_receiver_dpdk` / `rxbench_dpdk` 启动。
2. 解析 CLI，支持 `--config`、`--dry-run`、`--run-until-stopped`、`--duration`、`--status-interval`、`--help`。
3. 加载默认配置和 section 化配置文件。
4. 初始化 DPDK backend。
5. 批量收包，必要时应答 ARP。
6. 提取 IPv4/UDP payload，并在需要时完成 IP 分片重组。
7. 按当前协议头解析、校验、序列解释。
8. 把有效包写入 `capture_packets.bin`，并把语义索引写入 `capture_index.csv`。
9. 持续接收模式下按秒刷新中文状态面板，结束后输出中文汇总。

## 配置与 CLI

当前 CLI 参数完整列表：

- `--config FILE`
- `--dry-run`
- `--run-until-stopped`
- `--duration SECONDS`
- `--status-interval SECONDS`
- `--help`

覆盖规则：

- `--run-until-stopped` 会覆盖配置里的 `run_until_stopped = true`
- `--duration SECONDS` 会覆盖配置里的 `duration_seconds`
- `--status-interval SECONDS` 会覆盖配置里的 `status_interval_seconds`

推荐配置样例：

- `configs/dpdk_single_face.conf`
- `configs/dpdk_receiver0_replay.conf`

当前示例配置已经按 section 组织，常用 section 包括：

- `[capture]`
- `[raw_record]`
- `[network]`
- `[dpdk]`
- `[runtime]`
- `[protocol]`
- `[log]`
- `[feedback]`

`[runtime]` 常用键包括：

- `duration_seconds`
- `max_burst`
- `cpu_cores`
- `run_until_stopped`
- `status_interval_seconds`

`[raw_record]` 常用键包括：

- `enabled`
- `output_dir`
- `file_prefix`
- `ring_slots`
- `writer_batch_size`
- `max_frame_bytes`
- `segment_bytes`
- `max_total_bytes`

协议默认值：

- `udp_packet_size = 2048`
- `channels_per_prt = 3`
- `packets_per_channel = 9`

## 服务器构建与测试

执行环境：Linux 服务器。

项目规则要求：Windows 侧只做代码编辑和文档更新，权威构建与测试必须在 Linux 服务器完成。

进入服务器工作目录：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
```

构建：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
```

单元测试：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure
```

集成测试：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/integration
ctest --output-on-failure
```

查看命令行参数：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_dpdk --help
```

## 当前已验证结果

最近一轮结构重构与命名收口已在 Linux 服务器隔离目录完成复验：

- 验证目录：`/home/devuser/WorkSpace/rx_tech_demo_codex_validate_20260403`
- 构建通过
- unit tests 通过：14/14
- integration tests 通过：1/1

## 运行产物目录约定

- `results/`
  - 当前主线运行结果目录
  - 每次运行会自动生成一个时间戳目录
  - 目录名格式：`YYYYMMDD_HHMMSS_xxx`
  - 例如：`results/20260403_103012_dpdk_single_face/`
- `data/`
  - 样例、参考、离线分析数据的保留目录
  - 不作为当前主线运行输出目录
- `stdout/`
  - 历史遗留目录
  - 当前主线不再使用

当前运行产物策略：

- capture 输出会进入时间戳运行目录
- `raw_record` 会在配置根目录下再创建同名时间戳运行目录
- 如果 `log_output=file`，日志文件会写入对应时间戳运行目录

## 输出文件

启用 capture 时，接收端会持续写出：

- `capture_packets.bin`
- `capture_index.csv`

启用 `raw_record` 时，接收端还会在判决、过滤和协议解析之前，把原始接收帧异步写入：

- `/data/rx_tech_demo/raw_frames/*.rawbin`

当前 capture index 列为：

```text
cpi,channel,prt,packet_index,packet_kind,payload_len,valid
```

## 接收运行

### 1. 先在接收端服务器做 dry-run

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf --dry-run
```

重点确认：

- `interface=receiver0`
- `receiver_ipv4=172.20.11.100`
- `allowed_source_ipv4=172.20.11.222`
- `allowed_dest_port=9999`
- `protocol_channels_per_prt=3`
- `protocol_packets_per_channel=9`

### 2. 固定时长接收

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf --duration 30
```

### 3. 持续接收，直到手工停止

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf --run-until-stopped --status-interval 1
```

### 4. 查看接收结果

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
ls -l results/dpdk_single_face
head -n 5 results/dpdk_single_face/capture_index.csv
```

现在应改为先查看最新时间戳目录，例如：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
ls -lt results
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d | sort | tail -n 1)"
ls -l "${LATEST_RUN_DIR}"
head -n 5 "${LATEST_RUN_DIR}/capture_index.csv"
```

## 验证边界

- 不要把 Windows 构建、IDE 静态分析或 dry-run 当成权威验证。
- 不要把历史 AF_XDP 结论当成当前主线完成度证明。
- 只有在 Linux 服务器上完成构建、测试和真实链路联调，才能宣称该层级已验证。
