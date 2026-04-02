# rx_tech_demo

`rx_tech_demo` 是一个 Linux-only 的雷达接收端演示工程。当前主线位于 `src/receiver`，默认叙述和权威验证都以 DPDK 接收路径为准；`src/legacy/af_xdp` 仅保留为兼容/历史参考。

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
  legacy/
tests/
tools/
configs/
scripts/
docs/
```

## 当前运行链路

主线运行顺序是：

1. `rx_receiver_dpdk` / `rxbench_dpdk` 启动。
2. 解析 CLI，只接受 `--config`、`--dry-run`、`--help`。
3. 加载默认配置和 section 化配置文件。
4. 初始化 DPDK backend。
5. 批量收包，必要时应答 ARP。
6. 提取 IPv4/UDP payload，并在需要时完成 IP 分片重组。
7. 按当前协议头解析、校验、序列解释。
8. 把有效包写入 `capture_packets.bin`，并把语义索引写入 `capture_index.csv`。
9. 输出单行状态摘要和中文汇总。

## 配置与 CLI

当前 CLI 仅支持：

- `--config FILE`
- `--dry-run`
- `--help`

推荐配置样例：

- `configs/dpdk_single_face.conf`
- `configs/dpdk_receiver0_replay.conf`

当前示例配置已经按 section 组织，常用 section 包括：

- `[capture]`
- `[network]`
- `[dpdk]`
- `[runtime]`
- `[protocol]`
- `[log]`
- `[feedback]`

协议默认值：

- `udp_packet_size = 2048`
- `channels_per_prt = 3`
- `packets_per_channel = 9`

## 服务器构建与测试

执行环境：Linux 服务器。

项目规则要求：Windows 侧只做代码编辑和文档更新，权威构建与测试必须在 Linux 服务器完成。

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

当前 capture index 列为：

```text
cpi,channel,prt,packet_index,packet_kind,payload_len,valid
```

## 外部 Sender

仓库里现在有两类 sender：

- `tools/raw_eth_sender.py`
  - 适合验证原始以太网/UDP 收包路径或过滤规则。
  - 不保证符合当前协议解析要求。
- `tools/rxtech_protocol_sender.py`
  - 按当前 Phase 3 协议格式发控制表包和数据包。
  - 适合做真实接收解析联调。

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

### 2. 启动接收端

如果你要手工联调，建议先把配置里的 `[runtime]` 改成更适合观察的值，例如：

```ini
[runtime]
duration_seconds = 30
max_burst = 64
cpu_cores = [16]
```

然后在服务器上启动：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf
```

### 3. 在外部 Linux sender 主机上发送协议正确流量

下面的命令会发送 2 个 CPI、每个 CPI 1 个 PRT、每个 PRT 3 个通道、每通道 9 个包，和当前实现默认协议一致：

```bash
cd /path/to/rx_tech_demo
sudo python3 tools/rxtech_protocol_sender.py \
  --iface sender0 \
  --dst-mac 9c:47:82:e1:36:d0 \
  --src-mac 9c:47:82:e1:36:dc \
  --src-ip 172.20.11.222 \
  --dst-ip 172.20.11.100 \
  --src-port 30001 \
  --dst-port 9999 \
  --cpi-count 2 \
  --prt-count 1 \
  --channels-per-prt 3 \
  --packets-per-channel 9
```

注意：

- 这要求 sender 主机具备原始套接字权限，所以通常需要 `sudo`。
- `--src-ip`、`--dst-ip`、`--dst-port` 必须和接收端配置匹配。
- 第 9 个数据包会自动按当前协议要求写入 `476 * 4` 字节有效 IQ，并把剩余字节补零。

### 4. 查看接收结果

运行结束后检查：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
ls -l results/dpdk_single_face
head -n 5 results/dpdk_single_face/capture_index.csv
```

如果链路正常，终端摘要里的 `parsed_packets`、`data_packets`、`captured_packets` 应该增长，并且 `capture_index.csv` 会出现对应的 `cpi/channel/prt/packet_index` 记录。

## 验证边界

- 不要把 Windows 构建、IDE 静态分析或 dry-run 当成权威验证。
- 不要把 legacy AF_XDP 结果当成当前主线完成度证明。
- 只有在 Linux 服务器上完成构建、测试和真实链路联调，才能宣称该层级已验证。
