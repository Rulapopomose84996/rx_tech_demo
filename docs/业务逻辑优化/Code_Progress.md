# Code Progress

## 2026-04-12

- 当前分支：`codex/business-logic-opt-v2`（基于 `codex/observability-logging-stage1`）

### Wave 0 — 已完成

| ID | 修改项 | 文件 | 状态 |
|----|--------|------|------|
| B-01 | 去除 `sleep_for` 阻塞 | `src/receiver/core/cpi_state_coordinator.cpp` | ✅ 完成 |

### Wave 1 — 已完成

| ID | 修改项 | 文件 | 状态 |
|----|--------|------|------|
| B-02 | `format_wall_clock_now` 下沉 | `sidecar/traffic_state_tracker.cpp`, `owner_loop.cpp` | ✅ 完成 |
| B-03 | `std::function` 类型擦除消除 | `protocol/udp_datagram_pipeline.h/.cpp`, `packet_pipeline.h/.cpp` | ✅ 完成 |
| B-06 | DebugCaptureRecord 去堆分配 | `runtime/debug_capture_writer.h/.cpp`, `owner_loop.cpp` | ✅ 完成 |

### Wave 2 — 已完成

| ID | 修改项 | 文件 | 状态 |
|----|--------|------|------|
| B-04 | `IMetricsCollector` 去虚拟化 | `receive_context.h`, `udp_datagram_pipeline.h/.cpp`, `packet_pipeline.h/.cpp`, `cpi_state_coordinator.h/.cpp` | ✅ 完成 |
| B-05 | `_mm_pause` spin hint | `src/receiver/output/cpi_consumer.cpp` | ✅ 完成 |
| B-07 | recycle ring spin 上限 | `src/receiver/output/cpi_consumer.cpp` | ✅ 完成 |

### Wave 3 — 已完成

| ID | 修改项 | 文件 | 状态 |
|----|--------|------|------|
| B-10 | 去除 release 时冗余 reset | `include/rxtech/cpi_context_pool.h` | ✅ 完成 |
| B-11 | finalize_active/previous 合并 | `cpi_state_coordinator.h/.cpp` | ✅ 完成 |
| B-12 | UdpPayloadBuffer move-only | `include/rxtech/udp_payload_assembler.h` | ✅ 完成 |

### Wave 4 — 已完成

| ID | 修改项 | 文件 | 状态 |
|----|--------|------|------|
| B-08 | 提取 OwnerLoop sidecar 逻辑 | `src/receiver/core/owner_loop.cpp` | ✅ 完成 |
| B-09 | OwnerLoop lambda 精简 | `src/receiver/core/owner_loop.cpp` | ✅ 完成 |

### Wave 5 — 已完成

| ID | 修改项 | 文件 | 状态 |
|----|--------|------|------|
| B-13 | 池耗尽降级（不终止 run） | `src/receiver/core/cpi_state_coordinator.cpp` | ✅ 完成 |
| B-14 | `output_degraded_` 去 atomic | `cpi_state_coordinator.h/.cpp` | ✅ 完成 |
| B-15 | 池耗尽降级回归测试 | `tests/unit/test_pool_exhaustion_degradation.cpp` | ✅ 完成 |

### 验证状态

- **Windows 本地代码编辑**：全部 B-01 至 B-15 代码修改已完成
- **Linux 服务器构建验证**：未执行
- **Linux 服务器测试验证**：未执行
- **真实数据面验证**：未执行
