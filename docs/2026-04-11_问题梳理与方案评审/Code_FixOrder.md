## 核心规划原则

**避免返工的关键点：**
1. **基础设施先行**——提取公共工具（`byte_order.h`、`path_utils.h`）后，其他文件才能安全引用
2. **死代码先清理**——删除遗留字段后，后续重构的搜索范围更小
3. **接口冻结后再优化实现**——先稳固 API 边界，再优化内部
4. **大型结构体重构最后做**——`RxConfig` 和 `RunSummary` 的拆分涉及全库，放到最后，否则中间所有 patch 都要二次修改

---

## 2026-04-10 状态更新

- 已按本顺序完成并经 kds 权威验证：Wave 0、Wave 1、Wave 2、Wave 3、Wave 4、Wave 5、Wave 6、Wave 7、Wave 8、Wave 9
- Wave 9 已完成并验证：F-31、F-32、F-33、F-29、F-30、F-38
- 最新服务器结果：`/home/devuser/WorkSpace/rx_tech_demo_wave89_validation_20260410_220000` 的 Debug Werror 构建通过，28/28 unit、3/3 integration 通过；同目录 GCC ASan/UBSan 构建与测试通过；clang benchmark-only 与 clang fuzz-only 可选构建通过

---

## 执行阶段

### Wave 0 — 工程基础配置（纯配置，零代码冲突）

> 这一波没有源码改动，先做完再开始任何代码修改。

| 编号 | 任务 | 说明 |
|------|------|------|
| F-34 | 添加 `.clang-format` | 后续所有提交自动格式化，避免 diff 噪声 |
| F-37 | CI 构建开启 `-Werror` | 在 cmake preset / CI 配置中设置，不改源码 |
| F-35 | 添加 `.clang-tidy` | 建立静态分析基线 |
| F-36 | 添加 CI 配置 | `.github/workflows/build.yml`，含 build + test |

(暂时不做CI流程相关工作，只关注代码质量)

---

### Wave 1 — 公共基础设施提取（前置依赖）

> **必须在 Wave 2 之前完成**，因为 Wave 2+ 的多个修复都要引用这些公共头。每个条目都是纯新增或纯内联替换，无逻辑改动。

| 编号 | 任务 | 涉及文件 | 说明 |
|------|------|---------|------|
| F-17 | 提取 `byte_order.h` | **新建** `include/rxtech/byte_order.h` → 替换 sample_packet_parser.cpp、udp_payload_assembler.cpp、dpdk_backend.cpp 中的局部函数 | 三处 anonymous namespace 合并为一处 inline 头 |
| F-16 | 统一 `path_utils.h` | `runtime/internal/path_utils.h`（已存在）← 合并 raw_frame_recorder.cpp 和 receive_runner.cpp 中的重复实现 | 先做，F-22 依赖此文件 |

**检查点**：运行全量测试，确认无回归。

---

### Wave 2 — 死代码清理（缩减后续改动范围）

> 先删除遗留字段，减少后续 Wave 中的搜索与修改范围。

| 编号 | 任务 | 涉及文件 |
|------|------|---------|
| F-20 | 删除 `BackendStats` 中 AF_XDP 遗留字段 | `rx_backend.h`、`metrics.h` (`RunSummary`)、后端实现 |
| F-19 | 处理 `truncated` 标志 | `udp_datagram.h`、`UdpDatagramPipeline`（检查并拒绝，或移除） |
| F-44 | 统一 `cpi_timeout_ns` 命名 | `rx_config.h`、`protocol_spec.h`、`rx_config.cpp` |
| F-45 | 为 `release_burst` 添加契约注释 | `rx_backend.h` 文档注释，无逻辑改动 |

**检查点**：运行全量测试 + 确认 clang-tidy 无新问题。

---

### Wave 3 — 内存与资源安全（P0/P1 安全问题）

> 这一波聚焦于资源管理正确性，每一项都是相对独立的局部改动。

**顺序依据**：F-10 和 F-22 先做（无依赖），F-11 和 F-40 次之（在 recorder 内部，保持在同一文件上下文中），F-23 / F-24 最后（receiver_runner 和 coordinator）。

| 编号 | 任务 | 涉及文件 |
|------|------|---------|
| F-10 | `RawFrameRecorder::Impl` 改 `unique_ptr` | raw_frame_recorder.cpp（头文件已有前向声明） |
| F-22 | POSIX 头加 `#ifdef __unix__` 保护 | raw_frame_recorder.cpp（与 F-10 在同一文件，一并提交） |
| F-40 | `ofstream::close()` 后检查 `fail()` | raw_frame_recorder.cpp、receive_runner.cpp |
| F-11 | 声明 `queue_mutex` < `state_mutex` 锁序 | raw_frame_recorder.cpp 注释（与 F-40 同文件，一并提交） |
| F-39 | `ParsedPacketView::payload_ptr` 生存期注释 | `sample_packet_parser.h` |
| F-24 | `output_degraded_` 改为 `std::atomic<bool>` 或添加注释 | `cpi_state_coordinator` 内部头 |
| F-23 | `g_stop_requested` 改为实例成员 | receive_runner.cpp、`receive_runner.h` |

**检查点**：运行 TSan + ASan（-DRXTECH_ENABLE_SANITIZERS）；全量测试通过。

---

### Wave 4 — 核心数据路径正确性（P0 正确性）

> 这一波改动的是热路径上的逻辑，互相有相邻关系，按以下顺序避免冲突：

```
F-12 (时钟) → F-02 (分片TTL) → F-03 (指标有界采样) → F-05 (超时输出) → F-04 (指针生存期)
   ↑ 独立                           ↑ 与 F-02 同属 P0 内存边界          ↑ 涉及 finalizer        ↑ 涉及 CpiOutput 结构
```

| 编号 | 任务 | 涉及文件 | 依赖 |
|------|------|---------|------|
| F-12 | 审计并统一时钟域，`rx_tsc` 全部来自 `steady_clock_now_ns()` | dpdk_backend.cpp、cpi_state_coordinator.cpp 注释 | 无 |
| F-02 | `FragmentAssembly` 加 TTL，`push()` 入口清理过期条目 | `udp_payload_assembler.h`、`udp_payload_assembler.cpp` | 无 |
| F-03 | `MetricsCollector` 改为固定容量有界采样缓冲 | `metrics.h`、metrics.cpp | F-02 之后（同一轮 P0 内存边界收口） |
| F-05 | 超时 CPI 产出 `ABNORMAL_CUTOFF_COMMIT` 输出 | cpi_finalizer.cpp、`cpi_state_coordinator.cpp` | 无 |
| F-04 | 为 `CpiReadOnlyView` 裸指针添加生存期文档+断言 | `cpi_finalizer.h` | F-05 之后（同文件） |

**检查点**：运行专项测试 `test_cpi_output_pipeline`、`test_udp_payload_assembler`；全量测试通过。

---

### Wave 5 — 性能热路径优化（P0/P1 性能）

> 这波改动最大，按依赖顺序：先改 `CpiContext::reset`（影响上游写入），再改收包路径（下游读取），最后改组装器。

| 编号 | 任务 | 涉及文件 | 依赖 |
|------|------|---------|------|
| F-01 | `CpiContext::reset()` 惰性清零 | `cpi_context.h`（`reset()` 内联函数）、slot_writer.cpp（需改为检查 `slot_valid_bytes` 而非假设全零） | **需先验证 slot_writer 逻辑** |
| F-07 | `DpdkIngress::recv_burst` 内部 mbuf 数组改栈分配 | dpdk_backend.cpp | 无 |
| F-06 | `UdpDatagramBurst` 改为固定大小数组 + size | `udp_datagram.h`、所有使用 `burst.datagrams` 的地方 | F-07 之后（burst 结构改了，dpdk 实现也要同步） |
| F-08 | `UdpPayloadAssembler::push()` 非分片路径返回视图 | `udp_payload_assembler.h`、udp_payload_assembler.cpp、`packet_pipeline.cpp` | F-06 之后（接口改变的向下传播） |

> ⚠️ F-06 是本阶段风险最高的改动（`vector` 改 `array` 影响所有 backend），建议单独提交并做全量回归。

**检查点**：运行全量测试 + 手工验证 FileReplay 集成场景；建议 Google Benchmark 对比（F-29 可提前添加作为评估工具）。

---

### Wave 6 — 接口与行为一致性（P1/P2）

> 这波改动相互独立，可按文件分组并行完成。

**Backend 一致性组**（同在 backend 层，集中修改）：
| 编号 | 任务 | 涉及文件 |
|------|------|---------|
| F-09 | `recv_burst` 截断上限常量化 + 调用者告警 | dpdk_backend.cpp、`rx_config.h` |
| F-13 | DPDK adapter 分片处理与 Assembler 路径对齐（或文档声明不支持） | dpdk_backend.cpp、文档 |
| F-41 | DPDK 后端定期检测 link status | dpdk_backend.cpp |

**状态机组**（同在 coordinator/config 层）：
| 编号 | 任务 | 涉及文件 |
|------|------|---------|
| F-18 | `output_drop_policy` 改枚举 | `cpi_state_coordinator` 内部头、owner_loop.cpp、`rx_config.h` |
| F-42 | Pool 耗尽指数退避 | cpi_state_coordinator.cpp |
| F-43 | 解析失败日志限流 | `udp_datagram_pipeline.cpp` |

**检查点**：全量测试通过；`test_receive_runner_fake` 和 `test_slow_consumer_pressure` 通过。

---

### Wave 7 — 大型结构体重构（P2，风险最高，单独做）

> **必须在 Wave 2–6 全部完成并合并后再做**，否则并发改动会产生大量冲突。

**建议在独立分支上完成，每步一次提交：**

```
Step 1: F-14 — RxConfig 拆分子结构体
        └→ 全量测试
Step 2: F-21 — config 校验函数（依赖 F-14 的新子结构体）
        └→ 添加 F-33 测试（一并提交）
Step 3: F-15 — RunSummary 分拆
        └→ 全量测试
```

| 编号 | 任务 | 涉及文件数 |
|------|------|-----------|
| F-14 | `RxConfig` 拆为 6 个子结构体 | ~25 处使用点 |
| F-21 | `validate_config()` 函数 | rx_config.cpp、`run_app.cpp` |
| F-15 | `RunSummary` 分拆 | ~15 处填充点 |

---

### Wave 8 — 可观测性增强（P3，已完成）

> 依赖 Wave 7 的 `RunSummary` 结构稳定后再做，避免重复修改。

| 编号 | 任务 | 依赖 |
|------|------|------|
| F-26 | `/proc/self/stat` CPU 指标采集 | F-15 稳定 |
| F-27 | 日志限流器（首次 + 每N秒聚合） | F-43 基础版已做 |
| F-25 | 引入结构化日志框架（spdlog） | 建议作为独立子任务评估引入成本 |
| F-28 | Prometheus / JSON-over-socket 导出 | F-26 完成后 |

---

### Wave 9 — 测试补强与 CI 完善（P3，已完成 F-31/F-32/F-33/F-29/F-30/F-38）

> 建议每个 Wave 结束时已同步补充对应测试；此波统一补充遗漏的专项测试。

| 编号 | 任务 | 对应 Wave |
|------|------|----------|
| F-32 | `test_cpi_timeout.cpp` | Wave 4 的验证 |
| F-33 | 非法配置值测试 | Wave 7 的验证 |
| F-31 | CMake Sanitizer preset | 独立，越早越好（建议 Wave 3 后立即添加） |
| F-29 | Google Benchmark 微基准 | Wave 5 后添加用于对比 |
| F-30 | libFuzzer harness | parser + assembler |
| F-38 | 全局包序列号 gap 检测 | 协议层功能扩展 |

---

## 完整执行顺序总览

```
Wave 0: 工程配置   → F-34, F-37, F-35, F-36
   ↓
Wave 1: 公共基础   → F-17, F-16
   ↓
Wave 2: 死代码清理 → F-20, F-19, F-44, F-45
   ↓
Wave 3: 内存安全   → F-10, F-22, F-40, F-11, F-39, F-24, F-23
   ↓ (+ F-31 Sanitizer preset)
Wave 4: 数据正确性 → F-12, F-02, F-03, F-05, F-04
   ↓ (+ F-32 超时测试)
Wave 5: 热路径性能 → F-01, F-07, F-06, F-08
   ↓ (+ F-29 Benchmark 基线)
Wave 6: 接口一致性 → F-09, F-13, F-41 | F-18, F-42, F-43
   ↓
Wave 7: 大型重构   → F-14 → F-21 → F-15
   ↓ (+ F-33 配置校验测试)
Wave 8: 可观测性   → F-26, F-27, F-25, F-28
   ↓
Wave 9: 测试补强   → F-30, F-38
```

---

## 并行执行建议

若有多人协作，以下组合可并行进行，无文件冲突：

| 并行组 A | 并行组 B |
|---------|---------|
| Wave 4（协议/finalizer 层） | Wave 3 剩余（receiver_runner.cpp） |
| Wave 6 Backend 一致性组 | Wave 6 状态机组 |
| Wave 8 可观测性 | Wave 9 Fuzzer |

---

## 高风险改动的额外建议

| 改动 | 风险 | 缓解策略 |
|------|------|---------|
| **F-01** CpiContext reset 惰性化 | 如果 `slot_writer` 任何地方假设 payload 全零会静默产生错误数据 | 修改前先用 ASan + 全量测试确认当前 `slot_valid_bytes` 使用的所有读写路径 |
| **F-06** `UdpDatagramBurst` 结构改 | 影响所有 backend 和消费者 | 用 `using` 类型别名做过渡层，先使所有调用点能编译，再逐步迁移 |
| **F-14** `RxConfig` 拆分 | 25+ 处使用点，极易遗漏 | 先将所有字段加 `[[deprecated]]` 注解指向新位置，编译器警告驱动迁移 |
