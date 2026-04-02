# rx_tech_demo

`rx_tech_demo` 是一个 Linux-only 的雷达接收端演示工程。当前主线位于 `src/receiver`，默认叙述和权威验证都以 DPDK 接收路径为准。

## 当前定位

- 当前目标是把真实网卡收包、UDP payload 提取、协议解析、轻量统计和旁路落盘串成一条稳定接收链。
- 当前已经落地的 Phase 3 重点包括：
  - section 化配置和精简 CLI
  - 协议参数化 (`ProtocolSpec`)
  - sidecar 语义化指标
  - 热路径骨架 (`storage` / `admit` / `output` / `finalize`)
- 当前并不宣称已经完成完整业务接收模块；它仍是分阶段演进中的主线 demo。

## 目录概览

```text
src/
  receiver/
    app/
    core/
    ingress/
    protocol/
    runtime/
    sidecar/
    storage/
    admit/
    output/
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

持续接收模式的终端行为：

- 状态输出不再滚动打印单行摘要，而是按周期重绘中文状态面板。
- 状态面板会显示时间戳、运行时长、链路层统计、协议层统计和结果层统计。
- 当没有业务协议流量时，面板会明确显示“当前未检测到链路流量”或“当前仅检测到 ARP 探测”，而不是把这种情况表现为错误。

## 服务器构建与测试

执行环境：Linux 服务器。

项目规则要求：Windows 侧只做代码编辑和文档更新，权威构建与测试必须在 Linux 服务器完成。

进入服务器工作目录：

```bash
ssh kds
cd /home/devuser/WorkSpace/rx_tech_demo
```

构建：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/build_server_shared_cache.sh
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

本轮 Phase 3 收尾改动已在 Linux 服务器隔离目录完成复验：

- 构建通过
- unit tests 通过：11/11
- integration tests 通过：1/1
- 新增 sender 脚本通过 `python3 -m py_compile`

## 输出文件

启用 capture 时，接收端会持续写出：

- `capture_packets.bin`
- `capture_index.csv`

启用 `raw_record` 时，接收端还会在判决、过滤和协议解析之前，把原始接收帧异步写入：

- `/data/rx_tech_demo/raw_frames/*.rawbin`

默认策略：

- 落盘目录位于机械硬盘 RAID10 数据盘 `/data`
- 目录总保留量上限为 `5GB`
- writer 线程通过有界 ring 从热路径异步取数，不在判决路径上直接写机械盘

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

如果你要手工联调，建议先把配置里的 `[runtime]` 改成更适合观察的值，例如：

```ini
[raw_record]
enabled = true
output_dir = /data/rx_tech_demo/raw_frames
max_total_bytes = 5368709120

[runtime]
duration_seconds = 30
max_burst = 64
cpu_cores = [16]
run_until_stopped = false
status_interval_seconds = 1
```

然后在服务器上启动：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf --duration 30
```

### 3. 持续接收，直到手工停止

如果需要持续等待外部雷达时序软件送流，可以直接开启持续接收模式：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf --run-until-stopped --status-interval 1
```

注意：

- 持续接收模式下，使用 `Ctrl+C` 停止。
- 状态面板会按 `status_interval_seconds` 或 `--status-interval` 指定的周期重绘。
- 无业务流量时，状态面板会显示“当前未检测到链路流量”或“当前仅检测到 ARP 探测”。
- 外部 sender 由独立软件负责模拟规定好的雷达时序；主线联调不要求在仓库内自建 sender 脚本。
- 仓库中的 `tools/raw_eth_sender.py` 和 `tools/rxtech_protocol_sender.py` 仍保留为辅助工具，但不是当前 README 的主线操作步骤。

### 4. 查看接收结果

运行结束后检查：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
ls -l results/dpdk_single_face
head -n 5 results/dpdk_single_face/capture_index.csv
```

如果链路正常，状态面板和最终中文汇总里的 `解析有效包`、`数据包`、`已落盘` 应该增长，并且 `capture_index.csv` 会出现对应的 `cpi/channel/prt/packet_index` 记录。

如果启用了 `raw_record`，还应检查 `/data/rx_tech_demo/raw_frames` 下是否生成 `.rawbin` segment 文件，并确认目录总占用不会超过 `5GB`。

## 验证边界

- 不要把 Windows 构建、IDE 静态分析或 dry-run 当成权威验证。
- 不要把历史 AF_XDP 结果当成当前主线完成度证明。
- 只有在 Linux 服务器上完成构建、测试和真实链路联调，才能宣称该层级已验证。
