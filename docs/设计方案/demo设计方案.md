## 我建议的 demo 结构

不要做成一个巨大的单二进制。
更实用的方式是：

* 一个 **common 核心库**
* 三个 **frontend 可执行程序**
* 一套 **统一压测脚本**
* 一套 **统一结果格式**

原因很简单：DPDK 的 EAL 初始化和运行模型比较特殊，硬塞进和 socket/AF_XDP 完全同一个 main 往往会把工程搞乱。DPDK 官方也把 pipeline 和 ring/lcore 分工当作标准模型来描述。([DPDK][2])

### 目录建议

```text
rx-tech-demo/
  CMakeLists.txt

  common/
    include/
      rx_backend.h        # 统一后端接口
      packet_desc.h       # 统一包描述符
      rx_config.h
      metrics.h
      parser.h
      spsc_ring.h
      sink.h
    src/
      parser.cc           # 公共轻量解析
      metrics.cc
      sink_null.cc        # 只释放，不做业务
      sink_spsc.cc        # 推入SPSC
      report_json.cc

  backends/
    socket/
      socket_backend.cc   # recv/recvmmsg 基线
    af_xdp/
      xsk_backend.cc      # XSK + UMEM + XDP program
      xdp_prog_kern.bpf.c
    dpdk/
      dpdk_backend.cc     # rte_eth_rx_burst + mbuf

  apps/
    rxbench_socket_main.cc
    rxbench_xdp_main.cc
    rxbench_dpdk_main.cc
    traffic_gen_main.cc   # 可选，本地小型发包器
    trace_replay_main.cc  # 可选，回放真实FPGA抓包

  scenarios/
    steady_single_face.yaml
    steady_three_faces.yaml
    burst_single_face.yaml
    burst_three_faces.yaml
    real_pktmix.yaml

  scripts/
    run_matrix.py
    pin_cpu.sh
    setup_xdp.sh
    setup_dpdk.sh
    collect_ethtool.sh
    report_compare.py

  results/
    2026-03-16/
      socket/
      af_xdp/
      dpdk/

  docs/
    DESIGN.md
    TESTPLAN.md
    REPORT_TEMPLATE.md
```

---

## 后端接口怎么定

后端接口不要暴露 socket、XSK、`rte_mbuf` 这些细节。
统一成“取一批包、处理、归还”的模型。

### 最小接口

```cpp
struct PacketDesc {
  uint8_t* data;
  uint32_t len;
  uint64_t ts_ns;
  uint32_t port_id;
  uint32_t queue_id;
  uint32_t face_id;
  uintptr_t cookie;   // 后端私有句柄：pool slot / UMEM frame / rte_mbuf*
};

struct RxBurst {
  PacketDesc* pkts;
  uint32_t count;
};

class IRxBackend {
public:
  virtual bool init(const RxConfig&) = 0;
  virtual int recv_burst(RxBurst* burst, uint32_t max_burst) = 0;
  virtual void release_burst(const RxBurst&) = 0;
  virtual BackendStats stats() const = 0;
  virtual void shutdown() = 0;
  virtual ~IRxBackend() = default;
};
```

这样做的好处是：

* `socket` 后端可以继续用你当前的 `PacketPool`
* AF_XDP 后端的 `cookie` 指向 UMEM frame
* DPDK 后端的 `cookie` 指向 `rte_mbuf*`
* 解析层和后处理层完全不需要知道底层是谁

这才是真正“只比较接收技术”。

---

## demo 里必须拆成 4 个模式

不要一上来就测“全链路”。
要分层，否则你最后只知道“慢了”，不知道慢在哪。

### 模式 A：RX-only

收到包后只计数、立刻释放。

目标：

* 看纯接收上限
* 看每核 CPU
* 看批量大小分布
* 看用户态可见丢包

这个模式最适合回答：
**AF_XDP/DPDK 相比当前 `recvmmsg`，纯 ingress 提升有多大。**

### 模式 B：RX + 轻量解析

收到后只做：

* 读固定头字段
* 校验 magic / version / source_id
* 分类 packet_type
* 释放

目标：

* 看“真实热路径”上限
* 排除业务逻辑影响

### 模式 C：RX + SPSC handoff

收到后：

* 轻量解析
* 推一个 SPSC ring
* 下游 sink 线程只消费并释放

这一步最接近你当前工程形态。
DPDK 官方把这种“一个核收包，另一个核通过 ring 继续处理”的 pipeline 当作标准模式之一。([DPDK][2])

### 模式 D：RX + 当前完整后处理

把你现在的 `RxStage -> Dispatcher/Reassembler/...` 那套真正接上。

目标：

* 看“能不能落地到现有系统”
* 不是只看实验室极限

---

## 三个 backend 的实现边界

### 1. 当前基线：socket + recvmmsg

这个 backend 的职责很简单：

* `socket() / bind() / rcvbuf`
* `recvmmsg()` 批量收包
* 填 `PacketDesc`
* release 时归还 pool slot

它必须保留，因为 `recvmmsg()` 本来就是 Linux UDP 高性能接收的合理基线。([Man7.org][1])

### 2. AF_XDP backend

建议做成：

* 每个 port/queue 一个 XSK
* 每个 face 一个独立 UMEM
* 一个线程只轮询一个 queue
* XDP program 只做最小 redirect，不在 BPF 里做复杂逻辑

关键约束是：AF_XDP socket 绑定到某个 `netdev + queue_id` 后，只会接收来自那个队列的帧；如果 XDP 程序把别的 queue 的包 redirect 过来，是收不到的。zero-copy 不是必然的，可以强制 `XDP_ZEROCOPY`，不支持就失败；也可以接受 copy mode 作为对照。([Linux内核文档][3])

所以 AF_XDP demo 一定要把这几项单独记录进报告：

* driver 名称
* 是否 zero-copy
* queue_id
* XDP program attach 模式
* UMEM 大小
* fill/completion ring 深度

### 3. DPDK backend

建议做成：

* 一个 lcore 只轮询一个 RX queue
* 一条 face 一条 queue
* 解析和后处理仍复用 common 层
* mbuf 生命周期由 backend 封装掉

DPDK 官方明确建议避免多核共享 RX/TX queue，并强调 per-core private resource、NUMA 本地内存池和通过 ring 进行异步 pipeline。([DPDK][2])

---

## demo 的“公平对比”规则

这部分比代码更重要。

### 固定这些变量

* 同一台服务器
* 同一张网卡
* 同一端口
* 同一 NUMA 节点
* 同样的 CPU 绑核
* 同样的包型、包长、速率、突发
* 同样的后处理逻辑
* 同样的运行时长
* 日志、抓包、调试输出全部关闭

### 单独记录这些差异

* AF_XDP 是 copy 还是 zero-copy
* DPDK 是否独占 NIC / VFIO
* socket 路径是否开 busy-poll
* 每种模式用了几个物理核
* hugepage 配置
* IRQ / RSS 配置

Linux 文档说明，RSS 会把不同流分散到不同 RX queue，再由不同 CPU 处理；每个队列有独立 IRQ，IRQ affinity 和 indirection table 会显著影响结果。这个变量必须被固定并记录，不然三种模式根本没法公平比较。([Linux内核文档][4])

---

## 压测矩阵怎么设计

最少做下面 4 组。

### 组 1：单阵面、稳态

目标：看单 face 极限。

维度：

* 包长：真实包长 + 1500 / 1024 / 512 / 256
* 速率：从低到高阶梯压
* 时长：每点 30~60 秒

输出：

* sustained Gbps / Mpps
* drops
* CPU per core
* batch histogram
* user-space p99 latency

### 组 2：三阵面、稳态

目标：看并发隔离性。

维度：

* 3 个端口同时发
* 每 face 相同包型
* 每 face 独立绑核/队列

输出：

* per-face Gbps / Mpps
* 是否互相拖累
* 每 face 队列水位
* 总 CPU 和单核峰值

### 组 3：突发

目标：看抗 burst 能力。

维度：

* steady + burst
* 20 ms / 50 ms / 100 ms 突发窗口
* 突发倍率 1.2x / 1.5x / 2x

输出：

* 突发期 drop
* 恢复时间
* ring high watermark
* pool starvation

### 组 4：真实报文分布

目标：看工程真实值。

来源：

* FPGA 真实抓包回放
* 或由发包器按真实长度分布和类型分布合成

这组结果往往比固定 512B/1024B 更重要。

---

## 输出指标必须统一

建议每次运行产出一份 JSON，一份汇总 CSV。

### 每次运行至少记这些

```json
{
  "backend": "socket|af_xdp|dpdk",
  "mode": "rx_only|parse|spsc|full",
  "ports": 1,
  "queues": 1,
  "cores": [16],
  "packet_size_profile": "512B_fixed",
  "target_rate_gbps": 4.8,
  "actual_rx_gbps": 4.79,
  "actual_rx_mpps": 1.17,
  "drops_nic": 0,
  "drops_backend": 0,
  "drops_ring": 0,
  "cpu_user_pct": 81.2,
  "cpu_sys_pct": 7.4,
  "latency_p50_us": 8.1,
  "latency_p99_us": 24.3,
  "batch_avg": 47.6,
  "batch_p99": 64,
  "rss_queue": 0,
  "numa_node": 0,
  "af_xdp_mode": "zerocopy",
  "dpdk_driver": "",
  "notes": ""
}
```

### 推荐看的结论，不是单一峰值

* 达标时是否 0 丢包
* 达标时每核 CPU 还有多少余量
* 小包时谁先崩
* 突发时谁恢复更快
* 三阵面并行时是否有 face 间干扰

---

## 最重要的设计决策

### 1. 先做一个“共用后处理”的 benchmark core

这是整个 demo 成败的关键。
如果你给 socket、AF_XDP、DPDK 各写一套不同 parse/queue/stats，最后对比没有意义。

### 2. DPDK 用独立 executable

不要为了“统一”而把 DPDK EAL 搅进 socket/AF_XDP 的 main。
统一的是接口、配置、输出格式，不是一定一个二进制。

### 3. 先做 AF_XDP，再做 DPDK

因为你当前系统已经是 Linux 风格的 face-local pipeline，AF_XDP 改造面更小；DPDK 更适合在 AF_XDP 仍然不够时再上。AF_XDP 的 queue 绑定、UMEM、XSKMAP 模式和你现在“一 face 一线程一队列”的目标更接近。([Linux内核文档][3])

---

## 我会怎么排实现顺序

### 第 1 步

做 `common/` 和 `socket_backend/`，把你当前实现抽成 benchmark baseline。

### 第 2 步

接上 `mode A/B/C`，先把对比框架跑通。

### 第 3 步

加 AF_XDP backend，只做单端口单队列 PoC。

### 第 4 步

扩到三端口三队列，确认和 RSS/NUMA/绑核配合正常。Linux 的多队列/RSS 和 IRQ affinity 本身就是你要控制的重要变量。([Linux内核文档][4])

### 第 5 步

最后再接 DPDK backend。

---

## 直接给你的简版结论

demo 最好组织成：

* **一个 common benchmark core**
* **三个可替换接收 backend：socket / AF_XDP / DPDK**
* **四个测试模式：RX-only / RX+parse / RX+SPSC / full**
* **一套统一场景、一套统一 JSON 报告**
* **先单阵面，再三阵面，再 burst，再真实报文回放**

这样做的好处是：

* 你能明确知道收益来自哪里
* 你能避免“不公平对比”
* 你能把 PoC 结果直接映射回当前工程

如果你要，我下一条可以直接给你一版 **`DESIGN.md` 骨架 + CMake 目录 + `IRxBackend` 头文件草稿**，你基本可以照着开工。

[1]: https://www.man7.org/linux/man-pages/man2/recvmmsg.2.html "recvmmsg(2) - Linux manual page"
[2]: https://doc.dpdk.org/guides-24.03/prog_guide/poll_mode_drv.html "15. Poll Mode Driver — Data Plane Development Kit 24.03.0 documentation"
[3]: https://docs.kernel.org/6.5/networking/af_xdp.html "AF_XDP — The Linux Kernel  documentation"
[4]: https://docs.kernel.org/networking/scaling.html "Scaling in the Linux Networking Stack — The Linux Kernel  documentation"
