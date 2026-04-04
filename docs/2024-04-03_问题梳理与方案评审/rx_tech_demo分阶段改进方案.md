# rx_tech_demo 项目改进方案

> 基于 2024-04-03 问题梳理与方案评审文档的综合分析。
> 本方案按依赖顺序编排，每个任务包含精确的文件路径、接口变更和验收标准，可直接交给 AI Agent 执行。

---

## 现状基线摘要

| 区域 | 当前状态 | 关键缺陷 |
|------|---------|---------|
| OwnerLoop 热路径 | `raw_frame_recorder->submit()` / `status_reporter.emit_periodic()` 每迭代调用 | 无编译期裁剪开关 |
| CpiStateCoordinator | 单 active_ctx 模型，`n_prt = spec_.expected_n_prt` 硬编码 | 不支持动态 PRT |
| MetricsCollector | `on_reject(reason)` 存在但 reason 信息丢弃；`latencies_ns_` 每包 push_back | 无按原因分类；无 `on_output_backpressure` |
| ProtocolSpec | 仅 `expected_n_prt`，无 `dynamic_prt_enabled` / `max_n_prt` | 控制包 n_prt 未解析 |
| RxConfig | 无 `metrics_detail_enabled`、无 debug/profile 档位 | 配置项不足 |
| CMake options | 仅 `BUILD_TESTS` / `BUILD_*_BACKEND` / `WARNINGS_AS_ERRORS` | 无 `RXTECH_DEBUG_MODE` 等编译期开关 |
| output ring 满 | 记为 `on_pool_exhaustion()`，立即 release ctx | 背压与池耗尽混口径 |

---

## Phase 0：规则冻结（不做大改代码，只增补定义与最小接口）

### P0-1 生产路径 / 调试路径三层分界

**目标**：冻结热路径边界，后续所有实现遵守此分层。

**三层定义**：

**A. Core Path（生产最小路径，始终编译、始终执行）**
- `backend->recv_burst()` / `release_burst()`
- `UdpPayloadAssembler` / `SamplePacketParser` / `SamplePacketValidator` / `ProtocolSequenceInterpreter`
- `matches_packet_filter()`
- `CpiAdmission` / `SlotWriter` / `ProgressTracker` / `CpiFinalizer`
- `output_ring->push()` / `recycle_ring drain`
- `check_timeout()`
- 最小指标更新：总收包/字节、reject/drop/error 计数、pool_exhaustion、output_backpressure、ring high watermark、control/data 有效包数

**B. Soft Sidecar（运行期开关，生产默认关闭或低频）**
- `RuntimeStatusReporter::emit_periodic()` — 受 `status_interval_seconds` 控制，`= 0` 时完全禁用
- feedback 网络发送 — 受 `feedback_enabled` 控制
- RejectReason 周期性摘要日志 — 受 `status_interval_seconds` 控制
- raw_frame_recorder 状态汇总 — 仅在 snapshot 时输出

**C. Debug Heavy（编译期开关，生产构建完全裁剪）**
- invalid packet hex preview / decoded dump（当前 `invalid_dumped < 5U`）
- per-packet tracing
- `MetricsCollector` 的 `latencies_ns_` / `bursts_` 全量采样
- 高频详细状态面板（每秒刷新面板）
- raw frame 全量录制的诊断日志

**落地动作**：

1. options.cmake 新增：
```cmake
option(RXTECH_ENABLE_DEBUG_DIAGNOSTICS "Enable heavy debug diagnostics (invalid packet dump, per-packet tracing, detail metrics sampling)" OFF)
```

2. CMakeLists.txt 传递编译定义：
```cmake
if(RXTECH_ENABLE_DEBUG_DIAGNOSTICS)
    target_compile_definitions(rx_receiver PRIVATE RXTECH_DEBUG_DIAGNOSTICS=1)
endif()
```

3. 受影响代码用条件编译包裹：

- packet_pipeline.cpp — `emit_invalid_packet_diagnostic()` 调用处：
```cpp
#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
    if (diagnostic_output != nullptr && invalid_dumped < 5U) {
        emit_invalid_packet_diagnostic(*diagnostic_output, ...);
        ++invalid_dumped;
    }
#endif
```

- sidecar 中 `MetricsCollector` 的 `on_packet_latency_ns()` 和 `on_burst()` 的全量采样逻辑：
```cpp
void MetricsCollector::on_packet_latency_ns(uint64_t ns) {
#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
    latencies_ns_.push_back(ns);
#endif
    // core counter always updated (if needed)
}
```

4. rx_config.h 新增最小字段：
```cpp
bool metrics_detail_enabled = false;  // 运行期控制详细采样（与编译期开关独立）
```

**验收标准**：
- `RXTECH_ENABLE_DEBUG_DIAGNOSTICS=OFF` 构建后，热路径中无 `emit_invalid_packet_diagnostic`、无 vector push_back 采样
- `status_interval_seconds = 0` 时 `emit_periodic()` 不做任何格式化工作
- `raw_record_enabled = false` 时 `submit()` 不被调用（在 `owner_loop.cpp` 中加空指针检查已有，确认逻辑正确即可）

---

### P0-2 动态 PRT / CPI 状态机规则冻结

**目标**：在代码实现前，把动态 PRT 行为定义为明确的状态机规则。

**最小状态模型（兼容当前单 active_ctx）**：

| 状态 | 含义 | 进入条件 |
|------|------|---------|
| `UNBOUND` | 无 active CPI，无可用控制快照 | 初始 / CPI finalize 后 |
| `PROVISIONAL` | 数据先到，已 open active ctx，但 n_prt 未经控制包正式绑定 | 数据包先到时 `open_active()` |
| `BOUND` | 控制包已到，n_prt/channel_count/packets_per_channel 正式绑定 | 收到合法控制包且无冲突 |
| `CONFLICT` | 控制包与已观测状态矛盾 | 控制包 n_prt < 已观测最大 prt |

**状态转移规则**：

```
UNBOUND ──数据包到达──→ PROVISIONAL (n_prt = max_n_prt 保守上限)
UNBOUND ──控制包到达──→ BOUND (n_prt = 控制包值)
PROVISIONAL ──控制包到达且 n_prt >= 已观测最大 prt──→ BOUND (收敛)
PROVISIONAL ──控制包到达且 n_prt < 已观测最大 prt──→ CONFLICT (记异常，不回滚)
BOUND ──同 CPI 控制包字段一致──→ BOUND (记 duplicate)
BOUND ──同 CPI 控制包字段冲突──→ CONFLICT (记 conflict，不覆盖首绑定)
任意状态 ──新 CPI 数据到达──→ finalize 旧 CPI → PROVISIONAL/BOUND
任意状态 ──timeout──→ finalize 当前 CPI → UNBOUND
```

**规则清单**：

1. **控制包优先于配置默认值**：当 `dynamic_prt_enabled == true` 且控制包 `n_prt` ∈ `[1, max_n_prt]`，以控制包为准
2. **数据先到允许 provisional**：使用 `max_n_prt` 作为上限，ctx 标记 `bind_source = provisional`
3. **控制包后到只允许收敛不允许回滚**：`n_prt >= observed_max_prt` 时收敛，否则进入 conflict
4. **多份控制包以首个合法绑定为准**：后续一致 = duplicate，后续冲突 = conflict，均不覆盖
5. **timeout 锚点 = 首个有效数据包接收时间**（`first_rx_tsc`）
6. **Phase 0 不引入双缓冲语义**

**落地动作**：

1. `include/rxtech/protocol_spec.h` 新增：
```cpp
bool   dynamic_prt_enabled = true;
std::uint32_t max_n_prt = 100U;
// expected_n_prt 语义改为：dynamic_prt 关闭时使用的固定值
```

2. cpi_context.h — `CpiHotHeader` 或 `BoundWaveSnapshotLite` 新增：
```cpp
enum class BindSource : std::uint8_t { fixed, provisional, control };
BindSource bind_source = BindSource::fixed;
bool       conflict    = false;
```

3. rx_config.h 新增：
```cpp
bool          protocol_dynamic_prt_enabled = true;
std::uint32_t protocol_max_n_prt           = 100U;
```

4. runtime 中 `protocol_spec_from_config()` 映射新配置字段到 `ProtocolSpec`

5. `src/receiver/core/internal/cpi_state_coordinator.cpp` — `process_control_packet()` 修改：
```cpp
// 替换:
// current_snapshot_.n_prt = spec_.expected_n_prt;
// 改为:
if (spec_.dynamic_prt_enabled) {
    const auto parsed_n_prt = parsed.wave_cpi_info.n_prt;  // 从控制包解析
    if (parsed_n_prt >= 1 && parsed_n_prt <= spec_.max_n_prt) {
        current_snapshot_.n_prt = parsed_n_prt;
        // ... bind_source / conflict 逻辑按状态机规则处理
    } else {
        // 异常 n_prt，记告警，使用 expected_n_prt 回退
    }
} else {
    current_snapshot_.n_prt = spec_.expected_n_prt;
}
```

6. protocol — 协议解析层需确认控制包中 `n_prt` 字段已被正确提取到 `ParsedPacket` / `InterpretedPacket` 结构中；若未提取，需补充解析逻辑

**验收标准**：
- 单元测试覆盖：控制包先到 → BOUND、数据先到+控制包后到 → PROVISIONAL → BOUND、冲突场景 → CONFLICT
- `dynamic_prt_enabled = false` 时行为与当前完全一致（向后兼容）
- 异常 n_prt（0、超过 max_n_prt）不会静默污染状态机

---

### P0-3 零拷贝输出链路生命周期契约冻结

**目标**：明确所有权边界和 ring 满策略，后续不再反复改定义。

**所有权模型**：

| 对象 | 所有者 | 有效期 | 规则 |
|------|--------|--------|------|
| `PacketDesc` / mbuf 数据 | DPDK backend | `recv_burst()` → `release_burst()` | 下游不得跨 burst 持有指针 |
| `UdpPayloadFrame` / `ParsedPacketView` | 栈帧 | `process_packet()` 执行期间 | view 类型，不得跨线程 |
| `RawFrameRecorder` 接收数据 | recorder 内部队列 | submit 返回后独立 | submit 时完成数据保留 |
| `CpiContext` | `CpiContextPool` | `acquire()` → `release()` | 槽位写入都落在 ctx 内存上 |
| `CpiOutput` | output/recycle 协议 | push 到 output_ring → 收到 ReleaseToken | consumer 只读消费，不修改 ctx |

**output ring 满时策略**：

| 规则 | 内容 |
|------|------|
| 不阻塞 | owner 线程永不因 ring 满阻塞 |
| 丢输出结果 | 立即 release ctx，单独计数为 `output_backpressure_drop` |
| 不复用 pool_exhaustion | 与池耗尽严格分开 |
| 不做同步降级 | 不引入"ring 满改同步写"的 fallback |

**shutdown 时序（固定契约）**：

```
1. 停止 recv loop（should_stop = true）
2. finalize_active() 完成 active CPI 收尾
3. 通知 consumer stop
4. join consumer thread
5. drain_recycle() 到空
6. 停止 raw_frame_recorder
7. 生成 final RunSummary
```

**落地动作**：

1. metrics.h — `IMetricsCollector` 新增接口：
```cpp
virtual void on_output_backpressure() = 0;
```

2. `MetricsCollector` 实现此接口，内部新增 `output_backpressure_count` 计数器

3. receive_context.h — `RunSummary` 新增：
```cpp
std::uint64_t output_backpressure_count = 0;
```

4. `src/receiver/core/internal/cpi_state_coordinator.cpp` — `finalize_active()` 中 ring 满时：
```cpp
// 替换:
// metrics_.on_pool_exhaustion();
// 改为:
metrics_.on_output_backpressure();
```

5. `on_pool_exhaustion()` 仅保留给真正的 `ctx_pool_.acquire()` 失败场景

**验收标准**：
- `RunSummary` 中 `pool_exhaustion_count` 和 `output_backpressure_count` 严格分开
- shutdown 时序与上述契约一致，无悬挂引用
- 单元测试验证：ring 满时计 backpressure 而非 pool_exhaustion

---

## Phase 1：第一批结构实现

### P1-1 热路径裁剪与开关体系落地

**前置依赖**：P0-1 编译期开关已添加。

**具体修改清单**：

| 文件 | 修改内容 |
|------|---------|
| owner_loop.cpp | `raw_frame_recorder->submit()` 调用前增加 `if (raw_record_enabled && recorder != nullptr)` 确认不会因空指针或关闭状态调用 |
| owner_loop.cpp | `status_reporter.emit_periodic()` 内部确认 `status_interval_seconds == 0` 时直接 return，不做时间戳比较 |
| packet_pipeline.cpp | `emit_invalid_packet_diagnostic()` 用 `#if RXTECH_DEBUG_DIAGNOSTICS` 包裹 |
| sidecar MetricsCollector | `latencies_ns_.push_back()` 和 `bursts_.push_back()` 用 `#if RXTECH_DEBUG_DIAGNOSTICS` 或 `metrics_detail_enabled` 运行期开关包裹 |
| sidecar RuntimeStatusReporter | 确认 `emit_periodic()` 在 interval 未到时的开销 < 1 个时间戳比较+分支 |

**验收标准**：
- 生产构建（`RXTECH_ENABLE_DEBUG_DIAGNOSTICS=OFF`）：热路径无诊断输出、无全量采样
- 生产配置（`status_interval_seconds=0, raw_record_enabled=false`）：热路径无状态格式化、无 recorder 调用
- 调试构建仍保留完整定位能力

---

### P1-2 实现动态 PRT 检测

**前置依赖**：P0-2 状态机规则冻结完成、ProtocolSpec / RxConfig 字段已新增。

**修改清单**：

| 文件 | 修改内容 |
|------|---------|
| `include/rxtech/protocol_spec.h` | 新增 `dynamic_prt_enabled`、`max_n_prt`（P0-2 已完成） |
| sample_packet_parser.h / 实现 | 确认控制包解析结果中包含 `n_prt` 字段；若没有，在 `ParsedControlPacket` 中补充 |
| `src/receiver/core/internal/cpi_state_coordinator.cpp` | `process_control_packet()` 按 P0-2 状态机规则实现动态绑定 |
| `src/receiver/core/internal/cpi_state_coordinator.cpp` | `open_active()` 时根据 `dynamic_prt_enabled` 决定初始 `n_prt`（provisional 用 max_n_prt，fixed 用 expected_n_prt） |
| cpi_context.h | `BoundWaveSnapshotLite` 增加 `BindSource bind_source` 和 `bool conflict` |

**新增单元测试**：
- `tests/unit/test_dynamic_prt.cpp`：
  - 控制包先到 → n_prt 来自控制包
  - 数据先到 → provisional → 控制包收敛为 bound
  - 控制包 n_prt < 已观测最大 prt → conflict 记录
  - dynamic_prt_enabled=false → 行为与原逻辑一致
  - n_prt = 0 或超 max_n_prt → 告警 + 使用 expected_n_prt 回退

**验收标准**：
- 拼波位场景下 `n_prt` 来自控制包实际解析值
- 全部新增测试通过
- 异常控制包不会静默覆盖已有绑定

---

### P1-3 RejectReason 分类统计

**前置依赖**：P0-1 指标分层规则。

**修改清单**：

| 文件 | 修改内容 |
|------|---------|
| metrics.h | `MetricsCollector` 内部新增 `std::array<std::uint64_t, 8> reject_counts_{}`（对应 8 种 RejectReason） |
| `MetricsCollector::on_reject()` | 按 reason 索引递增对应计数器：`reject_counts_[static_cast<size_t>(reason)]++` |
| receive_context.h | `RunSummary` 新增 `std::array<std::uint64_t, 8> reject_by_reason{}` |
| `MetricsCollector::finalize()` | 将 `reject_counts_` 拷贝到 `RunSummary::reject_by_reason` |
| `RuntimeStatusReporter` | 周期性摘要中输出 top reject reason（受 `status_interval_seconds` 控制，不在每包路径） |

**验收标准**：
- `RunSummary` 可看出"丢的是哪一类"
- 热路径开销仅为一次数组索引递增（O(1)）
- 周期性摘要输出受 interval 控制，不影响热路径

---

### P1-4 DPDK 显式网卡绑定

**前置依赖**：无。

**修改清单**：

| 文件 | 修改内容 |
|------|---------|
| rx_config.h | 新增 `std::string dpdk_pci_addr`（默认空，空则保持现有遍历逻辑） |
| 配置文件解析逻辑 | 支持 `dpdk_pci_addr = "0001:05:00.0"` |
| ingress dpdk_backend | EAL 初始化时若 `dpdk_pci_addr` 非空，添加 `-a <pci_addr>` 参数（注意：DPDK 20.11+ 用 `-a` 替代 `-w`） |
| ingress dpdk_backend | 初始化完成后仅绑定指定 PCI 设备的端口，不再遍历全部端口 |

**验收标准**：
- 配置 `dpdk_pci_addr` 后 DPDK 只扫描指定设备
- 未配置时行为与当前一致（向后兼容）
- 服务器实测初始化路径可预测

---

## Phase 2：核心状态与数据路径改造

### P2-1 CPI 双窗口策略

**前置依赖**：P1-2 动态 PRT 已稳定。

**设计要点**：

```
CpiStateCoordinator {
    CpiContext* current_;    // 当前 active CPI
    CpiContext* previous_;   // 前一个 CPI（允许迟到包写入）
}
```

**状态转移**：
- 新 CPI 数据到达时：`previous_ = current_`（不立即 finalize），`current_ = open_active(new_cpi)`
- 数据命中 `previous_->cpi_id`：允许写入（迟到包宽容窗口）
- 数据既不命中 current 也不命中 previous：丢弃，记 reject
- CPI 跳变（`new_cpi - current_cpi > 1`）：强制 finalize previous，将 current 移为 previous
- previous 的 finalize 触发条件：
  - current 收到足够数据（说明 previous 已无更多包）
  - timeout
  - 再次发生 CPI 切换（previous 被驱逐）

**修改清单**：

| 文件 | 修改内容 |
|------|---------|
| cpi_state_coordinator.h | 新增 `CpiContext* previous_ctx_` 成员 |
| `cpi_state_coordinator.cpp` | `process_data_packet()` 增加 previous 匹配分支 |
| `cpi_state_coordinator.cpp` | CPI 切换逻辑：current → previous → finalize |
| `cpi_state_coordinator.cpp` | `check_timeout()` 同时检查 previous 的超时 |
| metrics.h | 新增 `on_late_packet_accepted()` / `on_late_packet_rejected()` |
| 测试 | 场景：顺序切换、跳变（1→3）、迟到包接受、迟到包拒绝（previous 已 finalize） |

**验收标准**：
- 顺序切换：无数据丢失
- 跳变：previous 被强制 finalize，计异常跳变告警
- 迟到包：在窗口内允许写入，超窗口丢弃并计数

---

### P2-2 零拷贝输出链路收口

**前置依赖**：P0-3 生命周期契约已冻结。

**修改清单**：

| 文件 | 修改内容 |
|------|---------|
| `src/receiver/core/internal/cpi_state_coordinator.cpp` | 确认 `finalize_active()` 中 ring 满时调用 `on_output_backpressure()` 而非 `on_pool_exhaustion()` |
| cpi_consumer.cpp | 确认 consumer handler 对 CpiOutput 只读消费，不写 ctx 内存 |
| owner_loop.cpp | 确认 shutdown 时序严格遵守 P0-3 契约 |
| 新增压测场景 | ring 满 → backpressure 计数正确；consumer 慢 → 无 use-after-free；快速 CPI 切换 → recycle 正确 |

**验收标准**：
- `pool_exhaustion` 和 `output_backpressure` 在 RunSummary 中严格分开
- 压测下无悬挂引用、无重复释放
- shutdown 无死锁

---

### P2-3 raw frame recorder 职责切边

**前置依赖**：P1-1 热路径裁剪。

**原则**：
- `submit()` 只做快速入队（复制必要元信息+payload 到 recorder 自有缓冲）
- 不在 submit 内做磁盘写、路径切换、日志拼装
- recorder 错误只通过 `snapshot()` 周期性汇总，不反向侵入热路径

**修改清单**：

| 文件 | 修改内容 |
|------|---------|
| raw_frame_recorder.cpp | 审计 `submit()` 实现，确认无阻塞操作 |
| owner_loop.cpp | 确认 `raw_record_enabled=false` 时不创建 recorder 实例（而非创建后不调用） |
| raw_frame_recorder.h | 在接口注释中明确 submit 的性能契约 |

**验收标准**：
- `raw_record_enabled=false` 时 recorder 不占用任何资源
- submit() 执行时间 < 1μs（仅入队）

---

## Phase 3：参数收敛与性能验证

### P3-1 巨型帧资源参数实验

**前置依赖**：Phase 2 结构稳定。

**实验矩阵**：

| 参数 | 扫描值 | 观察指标 |
|------|--------|---------|
| `udp_packet_size` | 2048 / 8000 | 基线对比 |
| `mempool_size` | 4096 / 8192 / 16384 | mempool 高水位、rx_nombuf |
| `rx_desc` | 256 / 1024 / 2048 | desc 压力、backend drops |
| `socket_mem_mb` | 256 / 512 / 1024 | OOM 发生率 |

**内存需求估算（8000B 巨型帧）**：
```
单 mbuf = 128(mbuf) + 128(headroom) + 8192(data) = 8,448 字节
mempool_size=8192 → 总 ≈ 69 MB
mempool_size=16384 → 总 ≈ 138 MB
```

**执行方式**：在 Linux 服务器上通过 `ssh kds` 执行，配合发包工具实测。

**验收标准**：
- 给出推荐默认值（有实验数据支撑）
- 高水位 < 80%
- 无 OOM

---

### P3-2 CPU 周期预算实测

**前置依赖**：P1-1 热路径裁剪完成（排除调试代码干扰）。

**基线**：
```
CPU: Phytium S5000C, 2.3 GHz
单包预算: < 12.2μs ≈ 28,060 cycles
```

**方法**：
1. 在 `process_packet()` 入口/出口插入 `rdtsc` 或 `steady_clock`（仅测量构建启用）
2. 采集 p50 / p99 / max cycles/packet
3. 用 `perf stat` / `perf record` 分析 cache miss / branch miss
4. 确定真正热点

**验收标准**：
- 生产模式下单包处理 < 28K cycles
- 热点定位基于实测数据

---

## Phase 4：条件性微优化

### P4-1 解释层内联与局部优化

**前置依赖**：P3-2 profiling 结果确认收益。

**仅在以下条件成立时执行**：
- profiling 证明 parser / validator / interpreter 构成显著热点（>10% cycles）
- 编译器未自动内联关键函数

**可选手段**：
- `__attribute__((always_inline))` 或 `[[gnu::always_inline]]` 标注关键小函数
- constexpr / table-driven 替代分支链
- 检查编译器输出（`-S` 或 `objdump`）确认内联效果

**禁止**：
- 不为了微优化破坏可维护性
- 不靠"感觉上应该更快"立项

---

## 依赖关系总图

```
P0-1 ─────────→ P1-1 ─────→ P3-2 ─────→ P4-1
P0-2 ─────────→ P1-2 ─────→ P2-1
P0-3 ─────────→ P1-3       → P2-2 ─────→ P3-1
              → P1-4       → P2-3
```

**严格禁止倒序**：
- 不先做内联再做热路径裁剪
- 不先做双缓冲再定动态 PRT 规则
- 不先调 mempool 参数再收口零拷贝生命周期
- 不先跑 profiling 再把调试输出从热路径剥离

---

## 各 Phase 交付物检查清单

| Phase | 交付物 | 服务器验证 |
|-------|--------|-----------|
| P0 | 编译开关就位、接口新增、配置字段新增、状态机规则文档 | 编译通过 + 现有测试全过 |
| P1 | 热路径裁剪代码、动态 PRT 实现、RejectReason 分类、DPDK 显式绑定 | 编译通过 + 新增测试全过 + 现有测试全过 |
| P2 | 双窗口 CPI、零拷贝收口、recorder 切边 | 编译通过 + 全量测试通过 + 压测场景通过 |
| P3 | 参数实验报告、profiling 报告 | 服务器实测数据 |
| P4 | 条件性内联优化（仅在 P3 证明需要时） | 优化前后 profiling 对比 |
