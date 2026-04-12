# 接收端业务逻辑层评审修复清单

## 评审范围

数据包经 ingress 接收后 → 协议解析 → CPI 状态管理 → SlotWrite → SPSC push 之前的全部业务处理逻辑。
评审基线：强实时、零阻塞接收端，**性能优先于鲁棒性**。

---

## P0 — 必须立即修复（直接违反零阻塞承诺 / 热路径性能瓶颈）

### B-01 · `open_active()` 包含 `sleep_for` 阻塞
- **维度**: 无锁 / 零阻塞保证
- **位置**: `src/receiver/core/cpi_state_coordinator.cpp` L252-L267
- **问题**: 当 16 深度的 CPI context pool 耗尽时，进入 backoff 循环依次 `std::this_thread::sleep_for(1µs, 10µs, 100µs)`。在实时接收端主线程执行 `sleep_for` 会导致不确定的内核调度延迟（可达毫秒级），期间丢失整个 burst 窗口的数据。consumer 稍慢即可触发此路径
- **修复**: 移除 `sleep_for`，改为纯非阻塞 try-drain recycle → 重试 acquire → 若仍失败则立即降级丢弃当前包并记录 metrics，不阻塞主循环

### B-02 · `format_wall_clock_now()` 在每个有效业务包路径上调用
- **维度**: 热路径性能
- **位置**: `src/receiver/core/owner_loop.cpp` L201
- **问题**: 每收到一个 valid business packet 都执行 `format_wall_clock_now()` → `std::chrono::system_clock::now()` + `localtime_r` + `strftime` + `std::string` 构造。这是一次系统调用 + 堆分配，在百万 pps 下为 ~200ns/packet 的额外开销。实际上 `TrafficStateTracker` 状态转换是极稀有事件（首次/恢复/中断），绝大多数调用的 wall_time 构造完全浪费
- **修复**: 将 `format_wall_clock_now()` 移到 `TrafficStateTracker::observe_valid_business_packet()` 内部，仅在检测到状态转换后才构造 wall_time 字符串；per-packet 路径只传入 `monotonic_ns`

### B-03 · `std::function` 类型擦除在每包热路径上
- **维度**: 热路径性能
- **位置**: `src/receiver/protocol/udp_datagram_pipeline.cpp` L168；`src/receiver/protocol/udp_datagram_pipeline.h` L27
- **问题**: `process_datagram` 的 `on_packet` 参数为 `const std::function<void(const ProcessedPacket &)> &`，每次调用产生一次虚调度 + 间接跳转。在百万 pps 级别，类型擦除开销约 5-10ns/packet，累积可观
- **修复**: 将 `process_datagram` 改为函数模板 `template<typename Callback> process_datagram(..., Callback&& on_packet)` 以消除类型擦除；或改为函数指针 + `void* context` 的 C-style 回调

---

## P1 — 高优先级（影响 CPU 效率 / 热路径额外开销）

### B-04 · `IMetricsCollector` 虚函数调用在每包路径上
- **维度**: 热路径性能
- **位置**: `include/rxtech/metrics.h` L250；每包调用 2-4 次（`on_valid_packet`, `on_reject`, `on_drop`, `on_packet_latency_ns` 等）
- **问题**: `IMetricsCollector` 接口有 12+ 个纯虚方法，当前只有唯一实现 `MetricsCollector`。每次虚调用约 2-5ns，每包 2-4 次，在百万 pps 下累积为 ~4-20ns/packet
- **修复**: 当前只有一个实现，可用 CRTP 静态分发消除虚调用；或直接使用 `MetricsCollector&` 具体类型而非接口引用；保留接口仅用于测试 mock 场景

### B-05 · `CpiConsumer` 空轮询无 CPU pause hint
- **维度**: 无锁 / 零阻塞保证 / CPU 效率
- **位置**: `src/receiver/output/cpi_consumer.cpp` L13-L15
- **问题**: `output_ring_.pop()` 返回 false 时直接 `continue`，无任何 CPU hint。长时间空闲时独占一个核的 100% CPU，对同物理核上的超线程兄弟线程（可能正是 owner 线程）产生竞争干扰，降低 owner 线程的 IPC
- **修复**: 在空轮询分支加 `_mm_pause()`（x86）或 `std::this_thread::yield()`；注释中已提到可改用 futex/eventfd，可作为后续优化

### B-06 · `DebugCaptureRecord` 在热路径上构造 `std::string` 成员
- **维度**: 热路径性能
- **位置**: `src/receiver/runtime/internal/debug_capture_writer.h` L18-L20；`src/receiver/core/owner_loop.cpp` L257-L267
- **问题**: `DebugCaptureRecord::payload` 和 `packet_kind` 均为 `std::string`，每包执行一次堆分配 + memcpy。即使 `capture_writer` 为 nullptr 可跳过，当 capture 启用时是严重瓶颈
- **修复**: `payload` 改为 `const uint8_t* + size_t` 视图（生存期绑定到 burst）；`packet_kind` 改为 `PacketKind` 枚举，仅在写出时格式化为字符串

### B-07 · recycle ring push 在 consumer 侧为阻塞式自旋
- **维度**: 无锁 / 零阻塞保证
- **位置**: `src/receiver/output/cpi_consumer.cpp` L30-L34
- **问题**: `while (!recycle_ring_.push(token))` 无限自旋等待 owner drain。若 owner 线程进入 `sleep_for` backoff（B-01），consumer 和 owner 可能形成活锁：consumer 等 owner drain，owner 在 sleep
- **修复**: 修复 B-01 后此风险大幅降低；额外可在 spin 中加 `_mm_pause()` 并设置最大自旋次数，超限后降级丢弃

---

## P2 — 中优先级（结构优化 / 可维护性）

### B-08 · `OwnerLoop::run()` 承载过多职责
- **维度**: 架构分层与职责边界
- **位置**: `src/receiver/core/owner_loop.cpp` L97-L370，函数体约 270 行
- **问题**: 同时管理 9 种职责：协议管道创建、SPSC ring 装配、consumer 线程启动、raw frame 录制、debug capture 写入、traffic 状态跟踪与 structured logging、status reporting、超时检查、shutdown 序列。sidecar 职责混入核心循环闭包增加 icache 压力
- **修复**: 将初始化序列提取为 `OwnerLoopContext` 装配类；将 per-packet 闭包中的 sidecar 逻辑（traffic tracking、structured logging、debug capture）提取为独立 observer，通过编译期组合注入主循环

### B-09 · `structured_log` 在 per-packet 回调闭包内有条件调用
- **维度**: 架构分层与职责边界 / 热路径性能
- **位置**: `src/receiver/core/owner_loop.cpp` L208-L220
- **问题**: 虽然只在状态转换时执行，但 `nlohmann::json` 对象的 initializer_list 构造代码位于 per-packet lambda 内部。编译器在未实际触发时仍需准备栈帧，增加了函数体积和 icache 开销
- **修复**: 将 structured_log 调用移到 `TrafficStateTracker` 内部或独立的 observer 中，从 per-packet 闭包完全移除

### B-10 · `CpiContext::reset()` 对数十 KB 结构做全量零初始化
- **维度**: 数据结构与内存布局
- **位置**: `include/rxtech/cpi_context.h` `reset()` 函数
- **问题**: 每次 `pool.acquire()` → `reset()` 将 `prt_summary`（64 × PrtSummary）和 `slot_valid_bytes`（2304 × uint16）全部 fill(0)。CPI 切换频繁时为数十 KB 的 memset 开销
- **修复**: 用 generation counter 做 lazy-reset：`reset()` 只递增 generation 并清零 header，读写时比较 generation 判断有效性；或仅清零 `header` 和 `control`，`slot_valid_bytes` 仍 fill(0) 但 `prt_summary` 按 `observed_n_prt` 范围做部分清零

### B-11 · `finalize_active` 与 `finalize_previous` 逻辑重复
- **维度**: 状态机正确性 / 可维护性
- **位置**: `src/receiver/core/cpi_state_coordinator.cpp` L284-L354
- **问题**: 两个方法逻辑几乎完全一致（唯一区别是操作 `active_ctx_` vs `previous_ctx_`），代码拷贝约 40 行。若一个分支修改忘记同步另一个，会引入微妙的状态机 bug
- **修复**: 提取私有方法 `finalize_ctx(CpiContext*& ctx, uint32_t& ctx_index, uint32_t trigger, IMetricsCollector& metrics)`，两个公开方法委托到此实现

### B-12 · `UdpPayloadBuffer` 隐式拷贝触发堆分配
- **维度**: 数据结构与内存布局
- **位置**: `include/rxtech/udp_payload_assembler.h` L23-L48
- **问题**: 热路径中 `set_view()` 避免了拷贝，但 `UdpPayloadBuffer` 的拷贝构造函数会触发 `vector::assign`。`ProcessedPacket` 通过 `std::move(udp_frame)` 传递，但若回调外部任何代码不慎拷贝则触发堆分配。类型设计存在隐式拷贝陷阱
- **修复**: 将 `UdpPayloadBuffer` 标记为 move-only（delete copy ctor/assignment）；或在拷贝构造中 assert 确保只在 owned 模式下拷贝，view 模式禁止拷贝

---

## P3 — 低优先级（容错增强 / 可观测性增强）

### B-13 · Pool 耗尽直接导致接收端硬停
- **维度**: 错误处理与降级策略
- **位置**: `src/receiver/core/cpi_state_coordinator.cpp` `open_active()` 返回 false → `run_error` 设定 → 主循环 break
- **问题**: 当 16 个 context pool 全部被 consumer 占用，`open_active` 返回 false 导致设置 `run_error`，主循环退出。在实时场景下，pool 短暂耗尽可能是瞬态现象（consumer 暂时卡顿），硬停不合理
- **修复**: pool 耗尽后不设 `run_error`，改为递增 `discard_due_to_pool_exhaustion` 计数器，丢弃当前包并继续接收；达到连续 N 次耗尽阈值后再降级为错误退出

### B-14 · `output_degraded_` relaxed 写可致延迟感知
- **维度**: 错误处理与降级策略
- **位置**: `src/receiver/core/cpi_state_coordinator.cpp` L304
- **问题**: `output_degraded_.store(true, memory_order_relaxed)` 在 `output_drop_policy == error` 时，主循环可能在数个 burst 后才感知降级，期间处理了额外数据包。这违反 "error 时立即停止" 的语义
- **修复**: 当 policy 为 error 时，改用 `memory_order_release` 写入；或在 finalize 后立即检查 degraded 状态而非等到下一个 burst 尾部

### B-15 · 缺少 `CpiStateCoordinator` 专项单元测试
- **维度**: 可测试性与测试覆盖
- **位置**: `tests/unit/`
- **问题**: CPI 双窗口切换、pool 耗尽回退、finalize 竞态、late packet 容忍等核心业务逻辑场景，未见独立的 `test_cpi_state_coordinator.cpp`。当前覆盖依赖 `test_cpi_output_pipeline` 间接触达，不够精准
- **修复**: 新增 `test_cpi_state_coordinator.cpp`，覆盖场景：正常 CPI 切换、active→previous 双窗口迟到包写入、pool exhaustion fallback、output ring 满时的 degraded 路径、control snapshot 合并与冲突检测
