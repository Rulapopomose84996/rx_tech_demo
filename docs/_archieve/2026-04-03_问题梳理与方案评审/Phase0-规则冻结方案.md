# Phase 0：规则冻结方案（基于当前实现）

## 0. 现状基线

当前代码已经完成了主循环模块化：`OwnerLoop` 在运行时直接创建 `PacketPipeline / CpiStateCoordinator / DataOrderTracker / CaptureSink / RuntimeStatusReporter`，并且在收包循环内同步执行 `raw_frame_recorder->submit(packet)`、`packet_pipeline.process_packet(...)`、`check_timeout(...)`、`drain_recycle(...)`、`emit_periodic(...)` 等动作，这说明“模块已经拆开”，但“热路径边界”还没有完全冻结 。
协议侧目前已经是 assembler → parser → validator → interpreter 的顺序，invalid 诊断最多打印 5 次，但 `accepted_bytes / accepted_packets` 是在通过 packet filter 后、协议校验前就累计的，这意味着当前统计口径还偏“链路层通过数”，不是“业务有效数” 。
CPI 状态侧当前仍是**单 active_ctx** 模型，而且 `process_control_packet()` 还把 `n_prt` 固定绑定为 `spec_.expected_n_prt`，没有从控制包动态解析；当 output ring 满时，目前是记作 `pool_exhaustion` 并立即释放 ctx，这也说明“背压”和“池耗尽”还没分开定义 。
配置与协议规格层当前只有 `expected_n_prt / cpi_timeout_ns / raw_record_enabled / status_interval_seconds` 这些基础项，没有 `dynamic_prt_enabled`、`max_n_prt` 或统一的 debug/profile 档位配置  。
指标接口目前只有 `on_reject / on_drop / on_error / on_pool_exhaustion` 等通用钩子，`MetricsCollector` 内部仍用 `std::vector` 全量存储 burst 和 latency 样本并在 `finalize()` 时排序，`on_reject(RejectReason)` 也还没有做按 reason 分类聚合  。

基于这些现状，Phase 0 不应该直接开始做双缓冲、巨帧调参或解释层微优化；更合适的做法是先冻结三件事：

1. **生产最小路径 vs 调试扩展路径**
2. **动态 PRT / CPI 状态机规则**
3. **零拷贝输出链路生命周期契约**

---

# 1. P0-1：生产最小路径 / 调试扩展路径冻结

## 1.1 目标

把当前已经模块化的实现进一步分层，冻结哪些逻辑属于：

* **Core Path（生产最小路径）**
* **Soft Sidecar（可开关的轻量旁路）**
* **Debug Heavy（重调试路径）**

这样做的目的不是为了“重构得更漂亮”，而是为了让后续所有实现都不再反复争论“这个逻辑该不该留在热路径”。

---

## 1.2 冻结后的三层定义

### A. Core Path（生产最小路径，始终存在）

允许留在 owner 线程内、并且要求低开销的逻辑只有：

* `backend->recv_burst()` / `release_burst()`
* `UdpPayloadAssembler / parser / validator / interpreter`
* packet filter
* `CpiAdmission / SlotWriter / ProgressTracker / CpiFinalizer`
* output ring push
* recycle ring drain
* timeout check
* 最小指标更新：

  * 总收包/总字节
  * reject/drop/error
  * pool exhaustion
  * ring depth high watermark
  * 业务有效包数（control/data）

### B. Soft Sidecar（生产可开关，默认关闭或低频）

保留运行期开关，但不允许每包做重操作：

* `RuntimeStatusReporter.emit_periodic(...)`
* RejectReason 周期性摘要
* raw_frame recorder 状态汇总
* 低频运行摘要组装

### C. Debug Heavy（调试专用，优先编译裁剪）

默认不进入生产热路径：

* invalid packet hex preview
* invalid packet decoded dump
* 高频详细状态面板
* per-packet tracing
* 原始帧全量录制的高频诊断日志

---

## 1.3 对当前代码的具体冻结规则

### 规则 A：`OwnerLoop` 允许“调度”，不允许“重格式化”

当前 `OwnerLoop` 中 `emit_periodic(...)` 仍在主循环内同步调用，这本身可以保留，但冻结规则应明确为：

* owner 线程**只负责触发**周期性状态更新
* 真正的人类可读字符串拼装、复杂状态面板渲染、长文本日志不应在每轮 loop 中无条件执行
* 若 `status_interval_seconds == 0`，则周期性状态完全禁用

### 规则 B：`PacketPipeline` 内允许有限 reject 计数，不允许持续重诊断

当前 invalid dump 已限制为前 5 次，这是对的 。
Phase 0 直接冻结为：

* 默认生产模式：仅计数，不打印 invalid preview
* 调试模式：最多 N 次（建议保持 5）详细 dump
* N 次之后只保留 reason 聚合，不再做 preview/decoded 输出

### 规则 C：`RawFrameRecorder::submit()` 允许在每包调用，但只允许“快速入队”

当前 recorder 已经有独立 `start/stop/submit/snapshot` 接口，这说明它适合做旁路队列化 。
冻结规则应明确：

* `submit(packet)` 只能做“快速复制必要元信息/原始帧内容到 recorder 自己的队列”
* 不允许在 submit 内做磁盘写、路径切换、日志拼装
* recorder 错误只能通过 `snapshot()/error_message()` 周期性汇总上报，不反向侵入 packet hot path

### 规则 D：当前 metrics 分两档，不再继续扩散

结合当前 `IMetricsCollector` 接口与 `MetricsCollector` 的实现，冻结为：

* **core metrics**：计数器类，允许热路径实时更新
* **detail metrics**：样本向量/排序类，只允许在 perf/debug 模式开启
  当前 `latencies_ns_` 和 `bursts_` 全量采样做法可以保留，但只在性能测量模式启用；生产模式默认关闭采样，仅保留必要计数  。

---

## 1.4 建议的最小配置调整（保持简单）

为了平衡简单和实现效果，Phase 0 不建议引入大量新配置项，只建议：

### 保留现有项并赋予明确语义

* `status_interval_seconds = 0` 表示禁用周期状态输出
* `raw_record_enabled = false` 作为生产默认

### 新增最小字段

* `bool protocol_dynamic_prt_enabled = true`
* `std::uint32_t protocol_max_n_prt = 100`
* `bool metrics_detail_enabled = false`

### 不建议在 Phase 0 新增

* 大量细碎 debug flag
* 单独的“几十个功能开关”配置项
* 多级 profile preset 系统

保持简单比“开关做满”更重要。

---

## 1.5 P0-1 验收标准

* 生产模式下：主循环只保留核心路径和低开销计数
* 调试重输出全部可关闭
* raw record / periodic status 都是明确的旁路功能
* 后续改 dynamic PRT、双缓冲、零拷贝时，不需要再回头重切热路径边界

---

# 2. P0-2：动态 PRT / CPI 状态机规则冻结

## 2.1 目标

把动态 PRT 从“一个待实现想法”变成**明确的状态机规则**。
当前 `ProtocolSpec` 只有 `expected_n_prt`，`CpiStateCoordinator::process_control_packet()` 也直接把 `current_snapshot_.n_prt` 绑定成这个固定值，这说明现在的代码还处在“固定 PRT 模型”  。

Phase 0 要做的不是立刻把代码全改掉，而是先决定：

* **何时信任控制包**
* **何时允许 provisional（临时绑定）**
* **遇到冲突如何处理**
* **timeout 以谁为锚点**

---

## 2.2 推荐的最小状态模型（兼容当前单 active_ctx）

为了不在 Phase 0 就引入多活/双缓冲复杂度，建议先冻结成下面 4 个状态：

### `UNBOUND`

还没有任何可用控制快照，也没有 active CPI。

### `PROVISIONAL`

数据包先到，已经为某个 CPI 打开 active ctx，但 `n_prt` 还没被控制包正式绑定。
此时只允许使用“保守上限配置”运行，不做最终语义承诺。

### `BOUND`

已收到合法控制包，`n_prt`/`channel_count`/`packets_per_channel` 已被正式绑定。

### `CONFLICT`

控制包信息与已观测状态矛盾；进入冲突态后，不再静默覆盖已有绑定，只记异常并保留现场。

---

## 2.3 明确规则

### 规则 1：控制包优先于配置默认值

当收到合法控制包时：

* 若 `protocol_dynamic_prt_enabled == true`
* 且控制包给出的 `n_prt` 在 `1..protocol_max_n_prt` 范围内

则以控制包为准，替代 `expected_n_prt`。

### 规则 2：数据先到时允许 provisional，但只允许“保守接纳”

如果数据包先到、控制包未到：

* 允许为该 CPI 打开 active ctx
* `n_prt` 使用 `protocol_max_n_prt` 作为 provisional 上限
* ctx 标记 `bind_source = provisional`
* 允许继续写 slot，但不宣称当前 PRT 语义已正式绑定

### 规则 3：控制包后到时只允许“收敛”，不允许“回滚”

当 active ctx 处于 provisional，随后收到同一 CPI 的控制包：

* 若控制包 `n_prt >= 已观测最大 prt`，则允许把 ctx 从 provisional 收敛为 bound
* 若控制包 `n_prt < 已观测最大 prt`，则进入 conflict，记录异常，不回滚已写数据

### 规则 4：同一 CPI 的多份控制包冲突时，以首个合法绑定为准

建议采用最稳妥的规则：

* 第一份合法控制包建立正式绑定
* 后续同 CPI 控制包若字段一致：记 duplicate，不改变状态
* 后续同 CPI 控制包若字段冲突：记 conflict，不覆盖首个绑定

这是最简单且最稳的规则；不建议做“以后来的为准”的覆盖式策略。

### 规则 5：timeout 锚点统一为“首个有效数据包到达时间”

当前代码 `check_timeout()` 使用 `header.first_rx_tsc` 作为 anchor，这个方向是对的 。
Phase 0 直接冻结：

* **timeout 锚点 = 当前 CPI 首个有效数据包的接收时间**
* 若还没有有效数据包，则不触发 timeout finalize
* 控制包 bind 时间仅用于诊断，不用于主 timeout 判定

### 规则 6：Phase 0 不引入“双缓冲语义”

虽然以后要做双窗口/双缓冲，但 Phase 0 不要把多活规则提前混进来。
Phase 0 只冻结**单 active ctx 下的语义规则**，这样实现代价最低，也最不容易把规则写乱。

---

## 2.4 对接口的最小修改建议

### `ProtocolSpec`

建议最小增补：

* `bool dynamic_prt_enabled = true;`
* `std::uint32_t max_n_prt = 100U;`

保留现有：

* `expected_n_prt`
* `cpi_timeout_ns`

语义改为：

* `expected_n_prt`：当 dynamic_prt 关闭时的固定模式值
* `max_n_prt`：dynamic_prt 打开时的安全上限，不用于直接宣称真实值

### `BoundWaveSnapshotLite` / ctx bind 元信息

建议增加但保持简单：

* `bind_source = fixed | provisional | control`
* `conflict = false`

不建议 Phase 0 就引入太多诊断字段。

---

## 2.5 P0-2 验收标准

* 动态 PRT 的行为在文档里可描述清楚，不依赖“实现细节猜测”
* 同一 CPI 的控制包冲突规则明确
* provisional 与正式绑定的边界明确
* timeout 锚点统一，不会后面再改口径
* 为后续 P2 双缓冲保留扩展空间，但当前实现仍可保持单 active 模型

---

# 3. P0-3：零拷贝输出链路生命周期契约冻结

## 3.1 目标

当前代码已经有 `owner -> output_ring -> consumer -> recycle_ring -> owner` 的基本输出管线，这是很好的基础 。
Phase 0 要做的是明确：**什么东西归谁所有，什么时候释放，ring 满时怎么办。**

重点是“冻结契约”，不是立即追求“全链路绝对零拷贝”。

---

## 3.2 建议冻结的所有权模型

### 对象 1：`PacketDesc` / mbuf 数据

* 所有者：DPDK backend
* 生命周期：仅在 `recv_burst()` 返回到 `release_burst()` 之间有效
* 规则：任何下游模块都**不得**在 burst 释放后继续持有 `packet.data` 指针

### 对象 2：`UdpPayloadFrame / ParsedPacketView / InterpretedPacketView`

* 所有者：当前 packet processing 栈帧
* 生命周期：仅在 `process_packet()` / callback 执行期间有效
* 规则：它们是“view/临时对象”，不得跨线程持有

### 对象 3：`RawFrameRecorder` 接收到的数据

* 所有者：recorder 自己的内部队列/缓冲
* 规则：`submit(packet)` 调用返回后，recorder 不能再依赖原始 `PacketDesc` 指针
* 含义：raw recorder 必须在 submit 时完成自己所需的数据保留动作

### 对象 4：`CpiContext`

* 所有者：`CpiContextPool`
* 生命周期：从 `ctx_pool_.acquire()` 到 `ctx_pool_.release()`
* 规则：所有用于输出线程消费的数据，必须最终落在 ctx 拥有的内存上，而不是落在 burst/mempool 视图上

### 对象 5：`CpiOutput`

* 所有者：逻辑上归 output/recycle 协议管理
* 规则：

  * push 到 output ring 成功后，owner 不得提前 release 对应 ctx
  * 只有收到 `ReleaseToken` 后，owner 才能 release ctx
  * consumer 不得修改 ctx 内容，只能只读消费

---

## 3.3 明确“零拷贝”的目标边界

为了平衡简单和实现效果，Phase 0 建议冻结成：

> 当前阶段的“零拷贝目标”是：
> **CPI context 到 consumer/output 链路不做额外深拷贝**，
> 而不是追求“从 NIC mbuf 到最终输出全程零拷贝”。

这样做有两个好处：

1. 兼容你现在已经有的 `ctx_pool_ + output_ring + recycle_ring`
2. 不会把 raw frame recorder、assembler、parser 的生命周期问题一并放大

---

## 3.4 output ring 满时的统一策略

当前 `finalize_active()` 里 output ring 满会记 `on_pool_exhaustion()` 并立即 release ctx，这在实现上简单，但语义不够准确，因为这其实是 **output backpressure**，不是 pool 真耗尽 。

Phase 0 建议直接冻结为：

### 规则 1：owner 不阻塞

owner 线程不因为 output ring 满而阻塞。

### 规则 2：ring 满时直接丢“输出结果”，但必须单独计数

* 立即 release ctx
* 单独记 `output_backpressure_drop`
* 不再复用 `pool_exhaustion_count`

### 规则 3：Phase 0 不做自动降级成同步消费

不引入“ring 满了改同步写”的复杂 fallback，保持策略单一。

这是最简单、最不容易引入死锁/隐性抖动的策略。

---

## 3.5 shutdown 时序冻结

建议冻结为固定顺序：

1. 停止主 recv loop
2. `capture_sink.flush()`
3. `finalize_active()` / `release_active()` 完成 active CPI 收尾
4. 通知 consumer stop
5. `join consumer thread`
6. `drain_recycle()` 到空
7. 停止 raw frame recorder
8. 生成 final summary

当前 `OwnerLoop` 已经有 stop consumer / join / drain recycle 的基本结构，这个方向正确 。
Phase 0 只需要把顺序写成固定契约，避免后续边改边变。

---

## 3.6 对 metrics 的配套冻结

由于当前 `IMetricsCollector` 没有 `on_output_backpressure()`，Phase 0 建议最小新增一个接口：

* `virtual void on_output_backpressure() = 0;`

并在 `RunSummary` 中新增：

* `std::uint64_t output_backpressure_count = 0;`

这样就把：

* `pool_exhaustion`
* `output_backpressure`
* `reject`
* `drop`

四类问题彻底分开，不再混口径。

---

## 3.7 P0-3 验收标准

* burst 生命周期、ctx 生命周期、consumer 生命周期边界明确
* raw recorder 不再依赖 burst 后指针
* output ring 满时策略统一，不再混记为 pool exhaustion
* shutdown 时序固定
* 后续 P2 做零拷贝收口时，不需要再重定义 ownership

---
