# rx_tech_demo

`rx_tech_demo` 是一个 Linux-only 的雷达接收端演示工程。当前唯一主线位于 `src/receiver`，默认叙述和权威验证仍以 DPDK 接收路径为准；同时仓库现在提供一个最小 Linux socket ingress，用于在不改主线的前提下验证第二种接入策略。

## 当前定位

- 当前目标是把真实网卡收包、UDP payload 提取、协议解析、轻量统计和旁路落盘串成一条稳定接收链。
- 当前并不宣称已经完成完整业务接收模块；它仍是分阶段演进中的主线 demo（Phase 3）。
- 当前统一主线位于 `src/receiver`，通过 `IRxBackend` 同时接入两种 ingress 策略：
  - `dpdk`：当前权威的真实网卡主路径
  - `socket`：最小 Linux UDP socket ingress，用于在不改 `PacketPipeline -> OwnerLoop -> CpiStateCoordinator -> CpiOutput` 主线的前提下验证第二种接入方式
- 当前代码已经完成 Phase 3 的模块化收平：
  - 公共头统一到 `include/rxtech`
  - `src/receiver` 模块实现文件已收平到模块根目录
  - `app` 入口层包含两个可执行入口：`main_dpdk.cpp` 和 `main_socket.cpp`
  - 热路径模块拆分为 PacketPipeline、CpiStateCoordinator、DataOrderTracker、RuntimeStatusReporter、CpiConsumer 输出流水线等独立组件
  - OwnerLoop 只保留流程协调，不再承载大块协议与状态细节

## 目录概览

```text
include/
  rxtech/
src/
  receiver/
    app/                      # 应用入口：CLI 解析、main_dpdk/main_socket、启动装配
    core/                     # 核心协调器：OwnerLoop、CPI 状态协调
      internal/
        cpi_state_coordinator.h       # CPI 状态协调器（新增）
        owner_loop_runtime_state.h    # 运行时状态管理（新增）
        owner_loop_summary.h          # 循环摘要逻辑
    ingress/
      dpdk/                   # DPDK 后端：收包、ARP 应答
        internal/
      socket/                 # Linux UDP socket 后端：最小可用 ingress 适配
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
    output/                   # 输出相关：CpiConsumer、SlowConsumer harness
tests/
tools/
configs/
scripts/
docs/
```

## 当前运行链路

主线运行顺序是：

1. `rx_receiver_dpdk` 或 `rx_receiver_socket` 启动。
2. 解析 CLI，支持 `--config`、`--dry-run`、`--run-until-stopped`、`--duration`、`--status-interval`、`--help`。
3. 加载默认配置和 section 化配置文件，随后准备本次运行的时间戳输出目录。
4. 校验配置并初始化所选 backend：
   - `dpdk`：当前权威主运行路径
   - `socket`：最小 Linux UDP socket ingress，仅作为第二种接入策略接入统一主线
5. **创建模块化组件**：PacketPipeline、CpiStateCoordinator、DataOrderTracker、RuntimeStatusReporter，以及基于 SPSC ring 的 CPI 输出流水线。
6. 批量收包。
   - `dpdk` 路径会在 ingress 层按需应答 ARP
   - `socket` 路径用 `recvfrom()` 收 UDP 包，并在后端内部包装成最小 Ethernet/IPv4/UDP synthetic frame，保证上层流水线无需区分后端
7. **通过 PacketPipeline 处理每个数据包**：
   - UdpPayloadAssembler：提取 IPv4/UDP payload，完成 IP 分片重组
   - PacketParser：按当前协议头解析
   - PacketValidator：校验 channel / packet_index / tail / payload 长度
   - ProtocolSequenceInterpreter：序列解释
8. **数据包过滤和指标收集**
9. **对于业务协议包**：
   - DataOrderTracker：监控数据顺序完整性
  - CpiStateCoordinator：CPI 准入决策、slot 写入、进度推进和 finalize
  - CpiConsumer：后台消费 finalized CPI，并通过 recycle ring 归还上下文池位
10. **周期性状态报告**：RuntimeStatusReporter 按秒刷新中文状态面板
11. 把有效包的 UDP payload 直接写入 `capture_packets.bin`，并把语义索引直接写入 `capture_index.csv`。
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
- `configs/socket_loopback.conf`

当前示例配置已经按 section 组织，常用 section 包括：

- `[capture]`
- `[raw_record]`
- `[network]`
- `[dpdk]`
- `[socket]`
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

`[socket]` 常用键包括：

- `bind_ip`
- `bind_port`
- `rcvbuf_bytes`
- `nonblocking`
- `batch_timeout_ms`

socket 路径的当前配置规则：

- `socket_bind_ip` 为空时回退到 `receiver_ipv4`
- `socket_bind_port` 为 `0` 时回退到 `allowed_dest_port`
- `backend=socket` 时，实际绑定地址和端口不能为空，端口必须小于等于 `65535`
- `socket_bind_ip` 与 `receiver_ipv4` 若同时设置，除 `0.0.0.0` 外不能冲突
- `socket_rcvbuf_bytes` 必须大于 `0`

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
./build/src/receiver/rx_receiver_socket --help
```

## 当前已验证结果

最近一轮与 socket ingress 相关的 Linux 服务器复验分成两部分：

- 代码构建与测试：`/home/devuser/WorkSpace/rx_tech_demo_socket_validation_20260409`
  - 构建通过
  - unit tests 通过：20/20
  - integration tests：1/2 通过
  - 未通过项：`rxtech_integration_slow_consumer_tests`
  - 这条失败路径使用 fake backend，不经过 `LinuxSocketIngress`，因此它是当前仓库里的既有集成问题，不是本次 socket 接入直接引入的回归证据
- socket 运行态验证：`/home/devuser/WorkSpace/rx_tech_demo_socket_runtime_validation_20260409`
  - `rx_receiver_socket` 已在 Linux 服务器上完成真实进程级 loopback 收包
  - 接收端汇总：原始收包 246 包，解析有效包 246 包，控制表包 3 包，数据包 243 包，CPI 数 3，完整 PRT 数 9，通道数 3
  - 落盘结果：`capture_index.csv` 为 246 条记录加表头，`capture_packets.bin` 为 503808 字节

当前对 socket 路径已经确认的事实是：

- `--dry-run` 能打印 `backend=socket`
- `make_backend("socket")` 能创建 `LinuxSocketIngress`
- `ReceiveRunner -> OwnerLoop -> PacketPipeline` 主流程无需分叉即可跑通 socket backend
- socket 路径已在 Linux 服务器上完成真实进程级业务流落盘

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
./build/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --dry-run
```

重点确认：

- `interface=receiver0`
- `receiver_ipv4=172.20.11.100`
- `allowed_source_ipv4=172.20.11.222`
- `allowed_dest_port=9999`
- `protocol_channels_per_prt=3`
- `protocol_packets_per_channel=9`

socket dry-run 还应重点确认：

- `backend=socket`
- `socket_bind_ip=127.0.0.1`
- `socket_bind_port=9999`
- `socket_rcvbuf_bytes=8388608`

### 2. socket loopback 运行验证

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1
```

这条路径适合验证：

- socket backend 的配置校验和启动装配
- UDP payload 是否能继续进入统一的 PacketPipeline
- capture 输出和最终汇总是否正常生成

### 3. DPDK 固定时长接收

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf --duration 30
```

### 4. DPDK 持续接收，直到手工停止

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf --run-until-stopped --status-interval 1
```

### 5. 查看接收结果

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
- 当前 DPDK 仍是权威真实网卡主路径；socket 是已经接入统一主线的第二后端策略。
- 当前 socket 最新运行态验证是在 Linux 服务器根命名空间的 loopback 上完成的真实进程级收发与落盘，不等同于 `ns_sender -> ns_receiver` 光口链路联调。
- `ns_sender` / `ns_receiver` 路径当前需要 sudo 口令才能进入 namespace，本轮没有把这条光口链路作为已完成验证写死到文档里。
- 当前 integration 套件并非全绿：`rxtech_integration_slow_consumer_tests` 仍失败，因此不能把本轮结果表述成“全部集成验证通过”。
