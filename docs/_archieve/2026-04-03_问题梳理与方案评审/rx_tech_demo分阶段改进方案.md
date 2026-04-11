# rx_tech_demo 项目改进方案（残留待办）

> 基于 2024-04-03 问题梳理与方案评审文档。
> **本文件只保留未完成条目**，已完成条目已清除。
> 最后整理时间：2026-04-04

---

## 已完成条目（已清除）

| 条目 | 完成状态 |
|------|---------|
| P0-1 生产/调试三层分界 | ✅ `RXTECH_ENABLE_DEBUG_DIAGNOSTICS` 编译开关就位；`packet_pipeline.cpp` / `metrics.cpp` 条件编译包裹；`status_interval_seconds=0` → `time_point::max()` 禁用；`metrics_detail_enabled` 已加入 `RxConfig` |
| P0-2 动态 PRT 状态机规则 | ✅ Phase 6 全部落地（ProtocolSpec 新增字段、BindSource 枚举、CpiStateCoordinator 实现） |
| P0-3 零拷贝输出链路生命周期契约 | ✅ `on_output_backpressure()` 已加入 IMetricsCollector；`pool_exhaustion` 与 `output_backpressure` 在 RunSummary 严格分开；shutdown 时序使用 `finalize_active_for_shutdown()` |
| P1-1 热路径裁剪与开关体系 | ✅ 诊断输出编译期裁剪；`status_interval_seconds=0` 零开销；`raw_frame_recorder` nullptr 保护就绪 |
| P1-2 动态 PRT 实现 | ✅ Phase 6 完成，16 unit + 1 integration 通过 |
| P1-3 RejectReason 基础设施 | ✅ `reject_counts_[8]` 数组、`on_reject()` 按 reason 分类、`RunSummary::reject_by_reason` 导出均已实现；**展示层（P1-3-display）保留** |
| P1-4 DPDK 显式网卡绑定 | ✅ `RxConfig::dpdk_pci_addr`、配置文件解析、`dpdk_backend` EAL `-a` 参数注入均已实现 |
| P2-1 CPI 双窗口策略 | ✅ Phase 6 完成：`previous_ctx_`、迟到包写入、`check_timeout()` 检查 previous、`on_late_packet_accepted/rejected` 计数 |
| P2-2 零拷贝输出链路收口 | ✅ ring 满时调 `on_output_backpressure()` 而非 pool_exhaustion；shutdown 无悬挂引用 |
| P2-3 raw_frame_recorder 职责切边 | ✅ `submit()` 通过 nullptr 保护；`start()` 才分配环形缓冲区；`enabled=false` 不消耗显著资源 |

---

## 残留待办

### P1-3-display：RejectReason 周期性面板展示

**背景**：数据基础设施（`reject_counts_[8]`、`RunSummary::reject_by_reason`、`on_reject()` 按 reason 计数）已全部完成。
唯一未完成的是**在状态面板中展示 top reject reason**。

**待做**：

| 文件 | 修改内容 |
|------|---------|
| `src/receiver/sidecar/status_panel.cpp` | `build_status_snapshot_lines()` 中增加 reject reason 分类输出：取 `reject_by_reason` 数组，找最大值及对应 `RejectReason` 字符串，添加为状态面板一行 |

**验收标准**：
- 状态面板中显示当前最多被丢弃的 reject reason（如 `invalid_len: 42`）
- 受 `status_interval_seconds` 控制，不影响热路径
- 0 丢弃时不显示该行

---

### P3-1：巨型帧资源参数实验

**前置依赖**：当前结构稳定。需在 Linux 服务器（`ssh kds`）配合发包工具实测。

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
mempool_size=8192  → 总 ≈ 69 MB
mempool_size=16384 → 总 ≈ 138 MB
```

**验收标准**：
- 给出推荐默认值（有实验数据支撑）
- mempool 高水位 < 80%
- 无 OOM

---

### P3-2：CPU 周期预算实测

**前置依赖**：生产构建（`RXTECH_ENABLE_DEBUG_DIAGNOSTICS=OFF`）。需在 Linux 服务器实测。

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

### P4-1：条件性内联与局部优化

**前置依赖**：P3-2 profiling 结果确认收益后方可执行。

**仅在以下条件成立时执行**：
- profiling 证明 parser / validator / interpreter 构成显著热点（>10% cycles）
- 编译器未自动内联关键函数

**可选手段**：
- `__attribute__((always_inline))` 或 `[[gnu::always_inline]]` 标注关键小函数
- `constexpr` / table-driven 替代分支链
- 检查编译器输出（`-S` 或 `objdump`）确认内联效果

**禁止**：
- 不为了微优化破坏可维护性
- 不靠"感觉上应该更快"立项

---

## 依赖关系

```
P1-3-display  →  独立，状态面板展示，可直接交付

P3-1          →  独立，需服务器实测

P3-2  ──────→  P4-1  （有 profiling 依据才做内联）
```
