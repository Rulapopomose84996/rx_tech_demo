# rx_tech_demo

`rx_tech_demo` 是一个 Linux-only 的雷达接收端演示工程。当前唯一主线位于 `src/receiver`，默认叙述和权威验证都以 DPDK 接收路径为准。

## 当前定位

- 当前目标是把真实网卡收包、UDP payload 提取、协议解析、轻量统计和旁路落盘串成一条稳定接收链。
- 当前并不宣称已经完成完整业务接收模块；它仍是分阶段演进中的主线 demo（Phase 3）。
- 当前代码已经完成 Phase 3 架构重构：
  - 公共头统一到 `include/rxtech`
  - `src/receiver` 模块实现文件已收平到模块根目录
  - `app` 入口层已收平
  - **新增模块化设计**：引入 PacketPipeline、CpiStateCoordinator、DataOrderTracker、CaptureSink、RuntimeStatusReporter 等独立组件
  - **职责分离**：数据处理流水线、状态管理、监控报告等逻辑已拆分为独立模块
  - **主循环精简**：OwnerLoop 从臃肿的 1000+ 行缩减至约 120 行，仅保留流程控制逻辑

## 目录概览

```text
include/
  rxtech/
src/
  receiver/
    app/                      # 应用入口：CLI 解析、启动装配
    core/                     # 核心协调器：OwnerLoop、CPI 状态协调
      internal/
        cpi_state_coordinator.h       # CPI 状态协调器（新增）
        owner_loop_runtime_state.h    # 运行时状态管理（新增）
        owner_loop_summary.h          # 循环摘要逻辑
    ingress/
      dpdk/                   # DPDK 后端：收包、ARP 应答
        internal/
    protocol/                 # 协议处理模块（新增流水线设计）
      packet_pipeline.cpp/h           # 包处理流水线（新增）
      data_order_tracker.cpp/h        # 数据顺序追踪器（新增）
      udp_payload_assembler.cpp       # UDP payload 组装
      sample_packet_parser.cpp        # 包解析器
      sample_packet_validator.cpp     # 包校验器
      protocol_sequence_interpreter.cpp # 序列解释器
    runtime/                  # 运行时管理
    sidecar/                  # 监控模块（新增）
      internal/
        runtime_status_reporter.h/cpp # 运行时状态报告器（新增）
        status_panel.h/cpp            # 状态面板写入器（新增）
      metrics.cpp                     # 指标收集
    storage/                  # 存储管理：CPI 上下文池、Slot 写入
    admit/                    # CPI 准入控制
    finalize/                 # CPI finalize 判定
    output/                   # 输出模块（新增）
      internal/
        capture_sink.h/cpp            # 捕获数据沉（新增）
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
5. **创建模块化组件**：PacketPipeline、CpiStateCoordinator、DataOrderTracker、CaptureSink、RuntimeStatusReporter
6. 批量收包，必要时应答 ARP。
7. **通过 PacketPipeline 处理每个数据包**：
   - UdpPayloadAssembler：提取 IPv4/UDP payload，完成 IP 分片重组
   - PacketParser：按当前协议头解析
   - PacketValidator：校验 channel / packet_index / tail / payload 长度
   - ProtocolSequenceInterpreter：序列解释
8. **数据包包过滤和指标收集**
9. **对于业务协议包**：
   - DataOrderTracker：监控数据顺序完整性
   - CpiStateCoordinator：CPI 准入决策和状态管理
   - CaptureSink：格式化输出到 capture 文件
10. **周期性状态报告**：RuntimeStatusReporter 按秒刷新中文状态面板
11. 把有效包写入 `capture_packets.bin`，并把语义索引写入 `capture_index.csv`。
12. 持续接收模式下按秒刷新中文状态面板：未见业务协议流量时只保留链路层单面板；见到业务协议流量后切换到包含协议层与结果层的扩展面板。
13. 结果层除全局 CPI / PRT / 通道计数外，还会显示"当前 PRT 覆盖"，用于区分"当前 PRT 仍在接收中"与"解析顺序异常"。
14. 程序结束时，RuntimeStatusReporter 构建最终 RunSummary 并渲染人类可读的总结报告

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

最近一轮 Phase 3 架构重构与模块化改造已在 Linux 服务器隔离目录完成复验：

- 验证目录：`/home/devuser/WorkSpace/rx_tech_demo_codex_validate_20260403`
- 构建通过
- unit tests 通过：14/14
- integration tests 通过：1/1
- **架构改进验证**：
  - OwnerLoop 代码量从 1000+ 行缩减至约 120 行
  - 新增 5 个独立模块：PacketPipeline、CpiStateCoordinator、DataOrderTracker、CaptureSink、RuntimeStatusReporter
  - 数据处理流水线职责清晰，单一职责原则得到贯彻
  - 状态管理从核心循环解耦，迁移到 CpiStateCoordinator
  - 监控报告逻辑独立为 Sidecar 模块，不侵入热路径

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

持续接收状态面板的当前口径：

- 未见业务协议包时，只显示统一的链路层状态与链路层统计
- 见到业务协议包后，显示协议层统计、全局结果统计，以及当前 PRT 的逐通道包覆盖
- `全局通道数` 表示全局去重后的通道集合，不表示某一个 PRT 在某一时刻已经收齐多少个通道

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
