# Findings

## Current Receiver Mainline

- 当前主线在 `src/receiver`，构建产物是：
  - `rx_receiver_dpdk`
  - `rxbench_dpdk`
- `src/receiver/CMakeLists.txt` 当前只把 DPDK ingress 链接进主应用公共层。
- `src/legacy` 仍保留旧 AF_XDP 代码和相关目标，但不应继续写成当前主线。

## Runtime Flow

- `app/main_dpdk.cpp` 通过 `run_app("dpdk", argc, argv)` 进入统一启动流程。
- `app/common/app_main_common.cpp` 负责：
  - 解析 CLI
  - 加载并合并配置
  - 创建 DPDK backend
  - 创建 `MetricsCollector`
  - 调用 `ReceiveRunner`
- `ReceiveRunner` 在启动时直接打开落盘文件：
  - `capture_packets.bin`
  - `capture_index.csv`
- 主循环不等待“暂停后统一保存”；通过校验的数据会在运行中实时写入上述文件。

## Packet Pipeline

- DPDK backend 从网卡批量取包，必要时自动应答 ARP。
- `UdpPayloadAssembler` 负责从以太网帧中提取 IPv4/UDP payload，并可重组 IP 分片。
- `SamplePacketParser` 当前解析的是 16 字节小端样本头，不是旧文档里的 `TPDX`/`DemoHeader` 模型：
  - 控制表 magic: `0x55AAFF00`
  - 数据包 magic: `0x55AAFF03`
- `SamplePacketValidator` 当前校验重点是：
  - 不接受 IP 分片状态下的业务包
  - 包体长度必须符合 2048/2032 字节约束
  - 数据包 `channel`、`packet_index`、`tail` 必须落在当前实现允许范围内
- `ProtocolSequenceInterpreter` 当前对数据包的 PRT/通道/包序采用“按 CPI 内到达顺序推导”的实现，而不是完全信任包头中的对应字段。

## Recording And Output

- 当前默认输出目录仍是 `results`，可通过 `--output` 或配置文件覆盖。
- 当前会持续写出两类落盘文件：
  - `capture_packets.bin`：顺序拼接的已接收 UDP payload
  - `capture_index.csv`：每包的 offset、length、时间戳、CPI、通道、PRT、packet index 等索引
- 终端还会输出：
  - 单行状态摘要
  - 中文人类可读汇总
  - `run_until_stopped=true` 时的周期性 `[status]` 快照

## Filtering Rules

- 当前支持三类前置过滤：
  - `allowed_source_ipv4`
  - `receiver_ipv4`
  - `allowed_dest_port`
- 被过滤掉的包计入 `filtered_packets`，不会进入解析成功统计，也不会写入落盘文件。

## Validation Boundary

- 当前仓库和项目规则都要求 Linux 服务器是唯一权威验证环境。
- 本次会话只完成代码核对和文档同步，没有在 Linux 服务器上重新做构建、测试或链路验证。
