# 发送端对接说明（AF_XDP 主线）

> 本文档面向发送端开发/联调人员。
> 它描述的是接收端当前已经落地并验证过的 AF_XDP 接收实现，便于发送端按相同假设完成对接。

## 1. 当前接收端主线

当前 `rx_tech_demo` 已不再以 `socket` 作为主接收路径，唯一主线为：

- `AF_XDP backend`
- 主入口程序：`rxbench_xdp`
- 运行模式：前台长时间运行，手动 `Ctrl+C` 停止
- 周期输出：默认每 `10s` 打印一次状态快照

当前在服务器上验证通过的真实链路是：

- 接收网口：`receiver0`
- 接收队列：`queue 22`
- 接收模式：`driver + copy`

## 2. 当前运行方式

接收端当前推荐启动方式：

运行位置：Linux server `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
sudo ./build/src/apps/rxbench_xdp --config ./configs/af_xdp_receiver0.conf --until-stopped
```

运行特征：

- 进程前台独占终端
- 每 `10s` 打印一次 `[status] ...`
- 手动 `Ctrl+C` 停止
- 停止时会输出最终一行：
  - `status=... backend=af_xdp mode=parse scenario=... queue_id=22 rx_packets=...`

## 3. 当前接收绑定

当前正式配置文件：[`configs/af_xdp_receiver0.conf`](D:\WorkSpace\Company\Tower\rx_tech_demo\configs\af_xdp_receiver0.conf)

关键配置如下：

- `backend: af_xdp`
- `mode: parse`
- `interface_name: receiver0`
- `queue_id: 22`
- `xdp_bind_mode: auto`
- `run_until_stopped: true`
- `status_interval_seconds: 10`
- `feedback_enabled: true`
- `feedback_enabled: true`
- `feedback_host: 172.20.11.11`
- `feedback_bind_host: 172.20.11.100`
- `feedback_port: 9999`
- `feedback_interval_seconds: 1`

说明：

- `queue_id=22` 不是拍脑袋值，而是对 sender0 当前真实流量做 RSS 落点排查后确定的队列
- 若发送端五元组发生变化，RSS 可能改变落队列，接收端也需要重新核查 queue

## 4. 当前 sender0 已验证链路

接收端在服务器上抓到的 sender0 实流量特征是：

- 源 IP：`172.20.11.11`
- 目标 IP：`172.20.11.100`
- 传输层：`UDP`
- 当前接收端对齐端口：`9999`

在 `receiver0 + queue 22 + AF_XDP parse` 路径上，已经拿到过以下真实结果：

- `rx_packets > 0`
- `parsed_packets > 0`
- `invalid_header_count = 0`
- `reassembled_blocks > 0`

因此对于 sender 来说，当前最重要的对齐项不是“是不是 UDP”，而是：

1. 目标 IP 必须是 `172.20.11.100`
2. 目标端口必须是 `9999`
3. 包头必须符合下面列出的真实 `TPDX` 格式
4. 若更改五元组，需重新核查 RSS queue 落点

## 5. 当前真实包头格式

接收端当前 parser 已按 sender 真实流量重写，不再使用早期假设的 `DEMO` 头。

当前识别的 magic：

- ASCII：`TPDX`
- 十六进制：`0x54504458`

当前 parser 解析规则：

- 若收到的是 AF_XDP 交上来的完整以太网帧：
  - 先跳过 `Ethernet + IPv4 + UDP`
  - 再从 UDP payload 起始位置解析业务头
- 若收到的是纯 payload（测试场景）：
  - 直接从起始位置解析

当前头部总长度：

- `32 bytes`

当前字段解释如下：

| 偏移 | 长度 | 解释 | 字节序 |
|---|---:|---|---|
| `0` | 4 | `magic = TPDX` | 大端可读 |
| `4` | 2 | `version` | 小端 |
| `6` | 2 | `flags` | 小端 |
| `8` | 4 | `stream_id` | 小端 |
| `12` | 8 | `block_id` | 小端 |
| `20` | 4 | `block_bytes` | 小端 |
| `24` | 2 | `frag_idx` | 小端 |
| `26` | 2 | `frag_count` | 小端 |
| `28` | 2 | `frag_payload_bytes` | 小端 |
| `30` | 2 | 保留 | 当前未使用 |

接收端当前硬编码的有效性规则：

- `magic == 0x54504458`
- `version == 1`
- `frag_count > 0`
- `frag_idx < frag_count`
- `frag_payload_bytes <= 实际包体剩余长度`
- `block_bytes > 0`

## 6. 重组逻辑

接收端当前重组逻辑：

- 重组单元：`(port_id, block_id)`
- 当前 sender0 联调时只会落在：
  - `port_id = 0`
- 使用字段：
  - `frag_idx`
  - `frag_count`
  - `frag_payload_bytes`
  - `block_bytes`
- `flags.first/last` 只作为辅助校验，不替代 `frag_idx/frag_count`

当前已验证的统计项包括：

- `rx_packets`
- `rx_bytes`
- `parsed_packets`
- `invalid_header_count`
- `reassembled_blocks`
- `missing_fragments`
- `duplicate_fragments`
- `reassembly_timeout_count`
- `throughput_gbps`

## 7. 终端状态输出

接收端前台运行时每 `10s` 输出一次状态快照，格式类似：

```text
[status] elapsed=10s aggregate rx_packets=... rx_bytes=... gbps=... drop_rate=... empty_poll_ratio=...
[status] port=0 rx_packets=... rx_bytes=... gbps=... reassembled_blocks=... invalid_header=... missing=... duplicate=... timeout=...
```

发送端联调时可以把这两类终端输出当作 receiver 实时观测窗口：

- aggregate 行：看总吞吐、总包数、整体 drop rate
- port 行：看单口解析、重组、缺片、超时

## 8. 当前反馈报文

接收端当前还会独立于状态输出，按 `1s` 周期向发送端发送一条 UDP JSON 反馈报文。

默认反馈目标：

- 目标：`172.20.11.11:9999`
- 源地址：`172.20.11.100`

报文格式：

```json
{
  "type": "receiver_feedback",
  "rx_packets": 2784828,
  "rx_bytes": 4008295992,
  "rx_mib": 3822.61,
  "dropped_packets": 0,
  "loss_rate": 0.0,
  "queue_id": 22,
  "gbps": 6.41327
}
```

字段含义：

- `type`
  - 固定为 `receiver_feedback`
- `rx_packets`
  - receiver 当前累计接收包数
- `rx_bytes`
  - receiver 当前累计接收字节数
- `rx_mib`
  - receiver 当前累计接收数据量（MiB）
- `dropped_packets`
  - receiver 当前累计丢弃包数
- `loss_rate`
  - 按 `dropped / (rx + dropped)` 计算
- `queue_id`
  - 当前 AF_XDP 绑定 queue
- `gbps`
  - 当前 aggregate 吞吐估算

发送端如果要读取 receiver 反馈，需要：

1. 在 sender 侧监听 `9999/udp`
2. 识别 JSON 文本
3. 解析出上述字段

注意：

- 当前反馈通道只是 receiver 单向上报
- 没有 ACK、重传、会话协商
- 属于“联调指标反馈”，不是可靠控制协议

## 9. 联调注意事项

### 9.1 queue 绑定是当前关键点

当前 sender0 的真实流量被 RSS 到 `receiver0 queue 22`。

这意味着：

- receiver 若绑 `queue 0`，即使链路通、包也会是 `0`
- sender 若改动五元组，queue 落点可能变化

因此每次 sender 改这些项后，都要重新注意 queue：

- 源 IP
- 目标 IP
- 源端口
- 目标端口
- 协议

### 9.2 当前平台模式不是 zerocopy

当前 receiver 实测结论是：

- `xdp_attach_mode = driver`
- `xsk_mode = copy`

不要把当前联调结果理解成 zerocopy 路径已经成立。

### 9.3 receiver 当前是 parse 主线

当前运行命令使用的是：

- `mode=parse`

也就是说 receiver 不只是收包，还会立即执行：

- 业务头解析
- fragment 重组
- per-port 指标统计

所以 sender 看到 receiver 反馈为 0 或大量 invalid，不一定是“网卡没收到”，也可能是“协议字段不匹配”。

### 9.4 当前 sender 对接必须遵守的最小前提

发送端联调时，至少要保证：

1. 目标口 `172.20.11.100:9999`
2. 业务头 magic 为 `TPDX`
3. `version = 1`
4. `block_bytes / frag_idx / frag_count / frag_payload_bytes` 与实际发送分片一致
5. 若 queue 落点变化，及时通知 receiver 侧更新绑定 queue

## 10. 推荐联调顺序

1. sender 先保证 `172.20.11.11 -> 172.20.11.100:9999` 持续发 UDP
2. receiver 前台运行 `rxbench_xdp --config ./configs/af_xdp_receiver0.conf --until-stopped`
3. 观察终端 `[status]` 输出
4. sender 同时监听 receiver 的 UDP JSON 反馈
5. 若 receiver `rx_packets > 0` 但 `invalid_header_count > 0`
   - 优先检查 `TPDX` 头字段布局
6. 若 receiver `rx_packets = 0`
   - 优先检查 sender 是否还保持当前五元组
   - 再检查 RSS queue 是否仍是 `22`

## 11. 当前接收端源码落点

便于 sender 对接时查阅的关键实现文件：

- 主入口：
  - [`src/apps/rxbench_xdp_main.cpp`](D:\WorkSpace\Company\Tower\rx_tech_demo\src\apps\rxbench_xdp_main.cpp)
- CLI 与运行入口：
  - [`src/apps/common/app_main_common.cpp`](D:\WorkSpace\Company\Tower\rx_tech_demo\src\apps\common\app_main_common.cpp)
- AF_XDP backend：
  - [`src/backends/af_xdp/src/xdp_backend.cpp`](D:\WorkSpace\Company\Tower\rx_tech_demo\src\backends\af_xdp\src\xdp_backend.cpp)
- 真实包头解析：
  - [`src/benchmark_core/src/parser.cpp`](D:\WorkSpace\Company\Tower\rx_tech_demo\src\benchmark_core\src\parser.cpp)
- 重组：
  - [`src/benchmark_core/src/reassembly.cpp`](D:\WorkSpace\Company\Tower\rx_tech_demo\src\benchmark_core\src\reassembly.cpp)
- parse 模式：
  - [`src/modes/parse/src/parse_mode.cpp`](D:\WorkSpace\Company\Tower\rx_tech_demo\src\modes\parse\src\parse_mode.cpp)
- 指标结构：
  - [`src/benchmark_core/include/rxtech/metrics.h`](D:\WorkSpace\Company\Tower\rx_tech_demo\src\benchmark_core\include\rxtech\metrics.h)

## 12. 当前结论

接收端当前已经具备以下可对接能力：

- AF_XDP 实流量接收
- 基于真实 queue 的稳定绑定
- `TPDX` 业务头解析
- fragment 重组
- 实时终端状态输出
- UDP JSON 反馈发送

发送端当前要做的，不是再猜 receiver 设计，而是按本文档对齐：

- 端口
- 包头
- queue 变更影响
- UDP 反馈接收逻辑
