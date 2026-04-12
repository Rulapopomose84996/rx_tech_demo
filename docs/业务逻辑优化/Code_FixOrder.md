## 核心规划原则

**前提约束：**
1. **性能大于鲁棒性**——P0 首先消除热路径上的阻塞和系统调用，而非追求完备的错误处理
2. **零阻塞先行**——违反零阻塞承诺的 `sleep_for` 必须第一时间消除
3. **侵入性由小到大**——先改独立组件的局部问题，再改跨组件的接口和架构
4. **每波必须可独立验证**——每 wave 结束后全量测试通过才进入下一 wave

---

## 执行阶段

### Wave 0 — 消除零阻塞违规（P0，风险最高的单点问题）

> 这是唯一直接违反实时约束的问题，必须第一时间修复。改动局限在一个函数内，无外部依赖。

| 编号 | 任务 | 涉及文件 |
|------|------|---------|
| B-01 | 移除 `open_active()` 中的 `sleep_for` backoff，改为非阻塞 try-drain + 立即降级 | `src/receiver/core/cpi_state_coordinator.cpp` |

**检查点**：全量 unit + integration 测试通过；用 `test_cpi_output_pipeline` 和 `test_slow_consumer` 验证 pool 耗尽场景无阻塞。

---

### Wave 1 — 热路径系统调用 / 堆分配消除（P0/P1，per-packet 级收益）

> 这波改动集中在主循环和协议管道层，每项改动独立，无相互依赖。

| 编号 | 任务 | 涉及文件 | 说明 |
|------|------|---------|------|
| B-02 | `format_wall_clock_now()` 移出 per-packet 路径 | `src/receiver/core/owner_loop.cpp`、`src/receiver/sidecar/traffic_state_tracker.cpp`、`src/receiver/sidecar/internal/traffic_state_tracker.h` | 改 `observe_valid_business_packet` 签名为只接受 `monotonic_ns`，状态转换时内部构造 wall_time |
| B-03 | `process_datagram` 的 `std::function` 改模板回调 | `src/receiver/protocol/udp_datagram_pipeline.h`、`src/receiver/protocol/udp_datagram_pipeline.cpp` | 改为 `template<typename F> process_datagram(..., F&& on_packet)`；实现移入头文件或显式实例化 |
| B-06 | `DebugCaptureRecord` 消除 `std::string` 成员 | `src/receiver/runtime/internal/debug_capture_writer.h`、`src/receiver/runtime/debug_capture_writer.cpp`、`src/receiver/core/owner_loop.cpp` | payload 改为 `const uint8_t* + size_t`；packet_kind 改为枚举 |

**检查点**：全量测试通过；`test_udp_datagram_pipeline` 通过。

---

### Wave 2 — 虚调用去虚拟化与 CPU hint（P1）

> 依赖 Wave 1 完成后接口稳定。

| 编号 | 任务 | 涉及文件 | 依赖 |
|------|------|---------|------|
| B-04 | `IMetricsCollector` 去虚拟化 | `include/rxtech/metrics.h`、`src/receiver/sidecar/metrics.cpp`、所有使用 `IMetricsCollector&` 的调用点 | B-03 之后（pipeline 接口稳定后再改 metrics 传递方式） |
| B-05 | `CpiConsumer` 空轮询加 `_mm_pause()` | `src/receiver/output/cpi_consumer.cpp` | 无 |
| B-07 | recycle ring push 加 pause hint + 最大自旋限制 | `src/receiver/output/cpi_consumer.cpp` | B-01 之后（sleep_for 消除后风险已降低） |

**检查点**：全量测试通过；`test_spsc_ring`、`test_slow_consumer` 通过。

---

### Wave 3 — 状态机与数据结构优化（P2）

> 这波改动涉及 CpiStateCoordinator 内部和 CpiContext 布局，相互有弱依赖，按以下顺序避免冲突：

```
B-11 (finalize 合并) → B-10 (reset lazy 化) → B-12 (UdpPayloadBuffer move-only)
  ↑ 重构内部方法            ↑ 依赖 finalize 逻辑稳定    ↑ 独立，最后做
```

| 编号 | 任务 | 涉及文件 | 依赖 |
|------|------|---------|------|
| B-11 | 合并 `finalize_active/previous` 为统一私有方法 | `src/receiver/core/cpi_state_coordinator.cpp`、`src/receiver/core/internal/cpi_state_coordinator.h` | 无 |
| B-10 | `CpiContext::reset()` 惰性化 | `include/rxtech/cpi_context.h`、`src/receiver/storage/slot_writer.cpp` | B-11 之后 |
| B-12 | `UdpPayloadBuffer` 标记 move-only | `include/rxtech/udp_payload_assembler.h`、编译验证所有调用点 | 无 |

**检查点**：全量测试 + ASan/UBSan 通过；重点验证 `test_slot_writer`、`test_cpi_output_pipeline`、`test_udp_payload_assembler`。

---

### Wave 4 — 架构分层优化（P2）

> 依赖 Wave 1-3 的接口和实现稳定后再做。这是侵入性最大的一波，建议在独立分支上。

| 编号 | 任务 | 涉及文件 | 说明 |
|------|------|---------|------|
| B-08 | 拆分 `OwnerLoop::run()` 职责 | `src/receiver/core/owner_loop.cpp`、可能新增 `owner_loop_context.h` | 提取初始化装配为独立类；sidecar observer 从 per-packet 闭包移出 |
| B-09 | `structured_log` 从 per-packet 闭包移出 | `src/receiver/core/owner_loop.cpp`、`src/receiver/sidecar/traffic_state_tracker.cpp` | 可与 B-08 合并完成 |

**检查点**：全量测试通过；`test_receive_runner_fake`、`test_receive_runner_until_stopped`、`test_receive_runner_filtered` 通过。

---

### Wave 5 — 容错增强与测试补强（P3）

> 最后一波，在所有核心改动稳定后补充。

| 编号 | 任务 | 涉及文件 | 说明 |
|------|------|---------|------|
| B-13 | Pool 耗尽改为降级丢弃而非硬停 | `src/receiver/core/cpi_state_coordinator.cpp` | 配合 B-01 的改动，完善降级策略 |
| B-14 | `output_degraded_` 在 error policy 下使用 release 语义 | `src/receiver/core/cpi_state_coordinator.cpp` | 局部改动 |
| B-15 | 新增 `test_cpi_state_coordinator.cpp` | `tests/unit/test_cpi_state_coordinator.cpp` | 覆盖双窗口切换、pool exhaustion、output ring full 等场景 |

**检查点**：全量测试通过 + ASan/UBSan 通过。

---

## 完整执行顺序总览

```
Wave 0: 零阻塞修复   → B-01
   ↓
Wave 1: 热路径优化   → B-02, B-03, B-06
   ↓
Wave 2: 去虚拟化     → B-04, B-05, B-07
   ↓
Wave 3: 数据结构优化 → B-11, B-10, B-12
   ↓
Wave 4: 架构分层     → B-08, B-09
   ↓
Wave 5: 容错与测试   → B-13, B-14, B-15
```

---

## 并行执行建议

若有多人协作，以下组合可并行进行，无文件冲突：

| 并行组 A | 并行组 B |
|---------|---------|
| Wave 1 的 B-02（owner_loop + sidecar） | Wave 1 的 B-03（protocol 层） |
| Wave 2 的 B-04（metrics 层） | Wave 2 的 B-05 + B-07（consumer 层） |
| Wave 3 的 B-11（coordinator） | Wave 3 的 B-12（assembler） |

---

## 高风险改动的额外建议

| 改动 | 风险 | 缓解策略 |
|------|------|---------|
| **B-01** 移除 sleep_for | pool exhaustion 时丢包率上升 | 添加 `pool_exhaustion_drop_count` 指标并在 summary 中报告；配合 B-13 设置降级阈值 |
| **B-03** std::function → 模板 | 改变 `process_datagram` 的 ABI，强制调用者改为模板实例化 | 实现移入头文件（或 `.ipp`）；确保所有 test 和 benchmark 的调用点同步更新 |
| **B-04** IMetricsCollector 去虚拟化 | 影响所有使用 `IMetricsCollector&` 的代码约 20+ 处 | 可先保留接口但在 `OwnerLoop` 内部改用 `MetricsCollector&` 具体类型；测试 mock 通过模板参数注入 |
| **B-10** CpiContext::reset 惰性化 | 如 slot_writer 任何路径假设 payload 全零会静默产生错误数据 | 先审计 `slot_valid_bytes` 的所有读写路径；确认 write 前不依赖旧 payload 内容 |

