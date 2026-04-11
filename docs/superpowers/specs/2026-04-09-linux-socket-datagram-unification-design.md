# Linux Socket / DPDK UDP Datagram 统一化设计

## 1. 背景

`rx_tech_demo` 当前已经形成一条 Linux-only 的接收端主线：

- DPDK 后端负责真实网卡主路径接入
- Linux Socket 后端作为第二 ingress 策略接入统一主线
- 上层核心业务仍集中在协议解析、校验、CPI 组装、Finalize 与输出链路

但是当前 `LinuxSocketIngress` 仍是最小实现，存在两个结构性问题：

1. Socket 路径为了复用现有主线，在 ingress 内部拼装 synthetic Ethernet/IPv4/UDP frame。
2. 系统统一边界仍停留在“原始帧”层，而项目真正长期关心的是 UDP 载荷上的雷达协议语义。

这会导致：

- Socket 路径承担不必要的拼帧与额外拷贝成本
- DPDK/Socket 两后端虽然共用上层主线，但输入语义并没有真正统一
- 后续如果要扩展新的 Linux ingress 形态，仍然容易被迫回到“伪造完整帧”的兼容路线

本设计的目标不是引入更重的框架，而是把统一边界改正到更适合当前项目长期演进的位置。

## 2. 设计目标

本次设计的主目标是：

- 将 DPDK 与 Socket 的统一边界下沉到 `UDP datagram + 网络元数据` 层
- 让 DPDK/Socket 共用更多协议主线与 CPI 主线
- 移除 Socket 主路径中的 synthetic frame 依赖
- 保持当前 Linux-only、server-first 验证原则不变
- 为未来新增 ingress 方案保留稳定接口

本次设计不追求的内容：

- 不把当前接收端描述成已经完成最终形态
- 不在本次设计中引入多线程收包、多队列并发或事件总线
- 不把 Socket 路径自动提升为真实网卡权威主路径
- 不要求本次设计同时解决全部 CPI/Finalize 业务问题

## 3. 当前实现现状

当前主线可简化为：

`IRxBackend -> PacketDesc(frame) -> PacketPipeline -> 协议层 -> CPI/Finalize`

其中：

- DPDK 后端天然交付完整原始帧
- Socket 后端通过 `recvfrom()` 接收 UDP payload，再在 ingress 内部构造 synthetic Ethernet/IPv4/UDP frame
- `PacketPipeline` 先从 frame 中提取 UDP payload，再进入协议解析和业务层

当前 Socket 最小实现的主要问题：

- 每包一次 `recvfrom()`，系统调用成本高
- 每包拼装 synthetic frame，产生额外填头与拷贝
- burst 存储依赖 `std::vector<std::vector<std::uint8_t>>`，不是稳定的固定槽位池
- 统一统计仅覆盖收包数、字节数、错误数、空轮询等粗粒度指标，难以判断 burst 行为和内核丢包

## 4. 核心设计决策

本设计采用以下核心决策：

1. 统一边界从“原始帧”改为“UDP datagram + 网络元数据”。
2. DPDK ingress 在自身内部完成 `raw frame -> UDP datagram` 适配。
3. Socket ingress 直接产出 `UDP datagram`，不再构造 synthetic frame。
4. 上层协议主线改为直接消费 `UDP datagram`，而不是依赖 frame 先做一次 UDP 提取。
5. 原始帧相关能力只保留在确实需要它的 ingress 内部或 sidecar 路径，不再作为统一业务输入语义。

这意味着项目的长期统一主线将变为：

`IRxIngress -> UdpDatagramBurst -> UdpPayloadPipeline -> 协议层 -> CPI/Finalize`

## 5. 目标架构

### 5.1 新的统一输入对象

新增统一 datagram 描述对象，建议命名为 `UdpDatagramDesc`，字段应覆盖：

- `payload_data`
- `payload_len`
- `src_ipv4`
- `dst_ipv4`
- `src_port`
- `dst_port`
- `ts_ns`
- `queue_id`
- `cookie`
- `backend_kind`
- 可选异常/截断标志

同时新增：

- `UdpDatagramBurst`
- 与其配套的 release 语义

设计要求：

- `payload_data` 在 `release_burst()` 前必须保持有效
- 上层不感知底层存储来自 DPDK mbuf、Socket arena 或其他后端
- 生命周期规则必须和当前 `RxBurst` 一样清晰且低成本

### 5.2 ingress 层

统一 ingress 只负责：

- I/O 初始化
- 批量收包
- 基础网络级过滤
- datagram 生命周期管理
- 后端级统计

ingress 不负责：

- 协议 magic 判断
- control/data 包语义识别
- CPI 状态推进
- finalize 业务决策

### 5.3 Datagram Normalize 层

在 ingress 与 protocol 之间新增一层轻量统一层，职责包括：

- 统一地址、端口、时间戳语义
- 统一截断、异常标志表达
- 统一后端统计入口
- 为 sidecar/raw record 提供可选接入点

这层的作用是把“后端实现差异”挡在协议主线之外。

### 5.4 UdpPayloadPipeline 层

现有 `PacketPipeline` 中真正依赖 UDP payload 的逻辑保留，但入口语义改为 datagram。

建议拆分为：

- `FrameIngressAdapter` 或同类组件
  - 仅服务于 DPDK 这类原始帧 ingress
  - 在 ingress 内部或紧邻 ingress 的边界层完成 UDP 提取
- `UdpPayloadPipeline`
  - 所有后端共用
  - 从 datagram 开始进入 parser / validator / interpreter / coordinator 主线

## 6. DPDK 后端改造方案

DPDK 仍然是当前真实网卡主路径，本设计不改变这一事实。

DPDK ingress 的调整重点是：

- 保留现有 EAL、端口、queue、mbuf、ARP 处理能力
- 将 ARP、非 IPv4/非 UDP 等链路层与网络层特殊路径封装在 ingress 内部
- 只向上层交付合法 UDP datagram
- 将 mbuf 生命周期通过 `cookie` 挂接到 datagram 描述对象

这样做的结果是：

- DPDK 仍保留其原始帧能力与高性能路径
- 但业务主线不再被迫统一在“完整帧”层

## 7. Socket 后端改造方案

Socket 后端将从“最小兼容 ingress”升级为“正式 datagram ingress”。

### 7.1 首版正式形态

建议 Socket 后端首版正式支持：

- `recvmmsg()` 批量收包
- 固定槽位的 burst arena
- `SO_RCVBUF`
- `SO_REUSEADDR`
- `IP_PKTINFO`
- `SO_RXQ_OVFL`
- 可选 `SO_RCVTIMEO` 或 `recvmmsg` 超时参数
- 可选 `SO_TIMESTAMPNS`

### 7.2 burst 存储策略

当前 `std::vector<std::vector<std::uint8_t>>` 不适合作为长期热路径结构。

建议改为固定槽位池，例如 `SocketBurstArena`，每个槽位固定持有：

- payload buffer
- source/destination metadata
- ancillary metadata
- datagram length
- 截断/异常标志

设计要求：

- `recv_burst()` 只填充已接收 datagram 的槽位
- `release_burst()` 只重置活跃数量，不进行逐包动态释放
- 避免每包 `malloc/free`

### 7.3 Socket 路径职责边界

Socket ingress 内可以做：

- 地址族过滤
- 零长度 / 超限 datagram 过滤
- 源 IP 白名单过滤
- 目的端口一致性检查
- 截断包计数

Socket ingress 内不应做：

- 控制包/数据包业务判断
- 协议字段解析
- CPI/PRT 业务逻辑

### 7.4 synthetic frame 下线

本次设计的关键收益之一，是 Socket 路径停止构造 synthetic Ethernet/IPv4/UDP frame。

收益包括：

- 减少每包填头与拷贝
- 降低 Socket 路径的认知负担
- 使 Socket 与 DPDK 真正在 datagram 层汇合

## 8. 统计与可观测性

当前 `BackendStats` 需要扩展为可支持 datagram 统一比较的形式。

建议至少增加以下统计：

- `receive_batches`
- `received_datagrams`
- `received_payload_bytes`
- `avg_burst_size`
- `max_burst_size`
- `would_block_polls`
- `timeout_polls`
- `kernel_drop_count`
- `truncated_datagrams`
- `source_filtered_datagrams`
- `datagram_format_errors`

设计要求：

- DPDK 与 Socket 都输出统一口径的 burst 和 drop 统计
- 状态面板与最终汇总能判断瓶颈更偏 ingress、protocol 还是后续业务链路
- 对于 Socket 路径，要能区分“空轮询”“后端错误”“内核丢包”“源过滤”

## 9. 测试设计

本设计要求按 TDD 推进实现，不允许先写生产代码再补测试。

### 9.1 datagram 抽象层测试

需要新增或补齐：

- `UdpDatagramDesc` 生命周期测试
- `UdpDatagramBurst` release 语义测试
- metadata 填充正确性测试

### 9.2 pipeline 迁移测试

需要验证：

- 同一 UDP payload 经新 `UdpPayloadPipeline` 处理后，解析/校验/解释结果与旧主线一致
- control/data 包的协议语义不回退
- CPI 状态推进结果不回退

### 9.3 Socket ingress 测试

需要覆盖：

- loopback 下多 datagram burst 接收
- `recvmmsg` 批接收行为
- source / destination metadata 正确性
- source filter / truncation / timeout / would-block 统计正确性

### 9.4 DPDK adapter 测试

需要覆盖：

- raw frame 到 datagram 的提取正确性
- ARP、非 IPv4、非 UDP 路径不污染上层 datagram 主线
- mbuf 生命周期释放正确

## 10. 验证与环境约束

本仓库是 Linux-only 项目，权威验证必须在 Linux 服务器进行。

因此：

- 本地 Windows 仅用于代码阅读、编辑与文档变更
- 构建、测试、运行态验证必须走 `ssh kds`
- 如果服务器不可达，再根据项目文档考虑 WSL fallback

需要明确：

- 单元测试、集成测试通过，只能证明 datagram 主线逻辑正确
- 只有在 Linux 服务器上完成真实构建与运行态验证后，才能声称该方案已完成权威验证
- 即使架构统一完成，DPDK 仍是当前真实网卡主路径；Socket 不能被表述为已经替代 DPDK

## 11. 分阶段落地建议

本次设计追求“架构边界一步到位，交付分阶段完成”。

### 阶段 1：建立 datagram 统一语义

目标：

- 引入 `UdpDatagramDesc / UdpDatagramBurst`
- 增加 `UdpPayloadPipeline`
- 让 DPDK 与 Socket 都能接入 datagram 主线
- Socket 停止依赖 synthetic frame

验收：

- 两后端同走 datagram 主线
- 协议解析、校验、CPI 主线行为不回退

### 阶段 2：将 Socket 做成正式高性能 ingress

目标：

- `recvfrom()` 升级为 `recvmmsg()`
- 引入固定槽位 arena
- 接入 `IP_PKTINFO` 与 `SO_RXQ_OVFL`
- 扩展统一 stats

验收：

- `release_burst()` 为低成本常数级操作
- Socket 热路径无明显每包动态分配
- 可观测性足以和 DPDK 做基线比较

### 阶段 3：收敛长期边界

目标：

- 清理旧 frame-first 主入口
- 将原始帧能力限制在必要范围内
- 更新 README 与主文档，固定 datagram-first 叙述

验收：

- 新增 ingress 只需解决“如何产出 UDP datagram”
- 协议与 CPI 主线不再区分 DPDK/Socket 来源

## 12. 主要风险

本设计的主要风险包括：

- 入口边界重构可能短期影响现有 `PacketPipeline` 与测试组织
- DPDK 适配层如果拆分不干净，可能把链路层复杂性重新泄漏到上层
- Socket 可观测性增强后，可能暴露此前未显性记录的内核丢包或 burst 退化问题

这些风险都属于可控工程风险，但需要通过：

- 明确的接口边界
- TDD 驱动的回归测试
- Linux 服务器上的阶段性验证

来逐步压实。

## 13. 结论

对于 `rx_tech_demo` 当前阶段，最合适的长期统一路线不是继续强化“原始帧统一”，而是将统一边界改到 `UDP datagram + 网络元数据` 层。

该路线的关键价值是：

- 更符合当前项目真正关心的协议语义层级
- 让 Socket 摆脱 synthetic frame 的兼容负担
- 让 DPDK/Socket 共用更多主线，同时保留各自 ingress 的技术差异
- 为后续新增 Linux ingress 方案提供稳定扩展点

这是一次面向长期演进的接口边界修正，而不是对现有最小实现做局部补丁。
