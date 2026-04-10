# rx_tech_demo 评审修复清单

## 2026-04-10 Wave 8/9 对齐状态

- 已完成并经 kds 权威验证：F-25、F-26、F-27、F-28、F-29、F-31、F-32、F-33、F-38
- 已完成代码接入但受服务器运行库限制：F-30
- 最新权威结果：`/home/devuser/WorkSpace/rx_tech_demo_wave89_validation_20260410_220000` 的 Debug Werror 构建通过，28/28 unit、3/3 integration 通过；同目录 GCC ASan/UBSan 构建与测试通过；clang benchmark-only 可选构建通过；clang fuzz-only 构建在 `libclang_rt.fuzzer` / `libclang_rt.asan` 链接阶段被 kds 的 unresolved `__aarch64_*` 原子符号阻塞

> 因此，下面与 F-25/F-26/F-27/F-28/F-29/F-31/F-32/F-33/F-38 对应的条目应视为“已关闭”；F-30 应视为“实现完成，待服务器 clang runtime 修复后补齐权威通过”。

## P0 — 必须立即修复（影响正确性/安全/核心性能）

### F-01 · CpiContext::reset() 大面积清零
- **维度**: 数据通路性能
- **位置**: cpi_context.h
- **问题**: `payload.fill(0U)` 每次 CPI 切换清零约 4.7MB，在热路径上造成严重延迟
- **修复**: 改为惰性清零——仅清 `header`、`control`，`slot_valid_bytes` 填零用于标记无效，payload 按 `slot_valid_bytes` 判断有效性而非依赖全部清零；或基于 `received_slot_count` 仅清零已写入槽位

### F-02 · UdpPayloadAssembler 分片映射无界增长
- **维度**: 异常与鲁棒性 / 内存管理
- **位置**: udp_payload_assembler.h `fragments_` 成员
- **问题**: 不完整 IP 分片永驻 `unordered_map`，长时间运行导致 OOM
- **修复**: 为每个 `FragmentAssembly` 添加 `first_seen_ns` 时间戳；在 `push()` 入口扫描并淘汰超过 N 秒（建议 5s）的条目；或限制 `fragments_.size()` 上限

### F-03 · MetricsCollector 向量无界增长
- **维度**: 异常与鲁棒性 / 内存管理
- **位置**: metrics.h — `bursts_` 和 `latencies_ns_` 字段
- **问题**: `std::vector` 持续追加无上限，长时运行耗尽内存
- **修复**: 改用固定大小的环形采样缓冲或直方图（HDR Histogram）；百分位数改为在线估算（如 t-digest 或 P²算法）

### F-04 · CpiReadOnlyView 裸指针跨线程传递
- **维度**: 并发与线程安全 / 内存安全
- **位置**: cpi_finalizer.h
- **问题**: `payload_base`、`slot_valid_bytes`、`prt_summary` 指向池槽位内存，consumer 线程读取期间若 pool 槽位被回收则悬垂
- **修复**: 保证 `ReleaseToken` 回收严格在 consumer 处理完 `CpiOutput` 之后发生（当前设计已满足但缺乏形式化证明）；添加生存期文档注释或改为 `shared_ptr<const CpiContext>` 引用计数

### F-05 · CPI 超时不产出输出
- **维度**: 接收链路正确性
- **位置**: cpi_finalizer.cpp
- **问题**: `TriggerTimeout | TriggerStop` 返回 `nullopt`，超时 CPI 的数据和诊断信息全部丢失
- **修复**: 超时触发时参照 `TriggerCpiSwitch` 的逻辑产出 `ABNORMAL_CUTOFF_COMMIT` 或新增 `TIMEOUT_DISCARD` 决策；至少输出一个带统计的 `CpiOutput` 供下游诊断

---

## P1 — 高优先级（影响可靠性/线程安全/性能瓶颈）

### F-06 · 热路径堆分配——UdpDatagramBurst
- **维度**: 数据通路性能
- **位置**: udp_datagram.h `UdpDatagramBurst::datagrams`; owner_loop.cpp 主循环内
- **问题**: 每次 burst 构造 `std::vector<UdpDatagramDesc>`，触发堆分配
- **修复**: 将 `UdpDatagramBurst` 改为 `std::array<UdpDatagramDesc, kMaxBurst>` + `size`；或在 OwnerLoop 中预分配复用

### F-07 · 热路径堆分配——DpdkIngress::recv_burst 内部 vector
- **维度**: 数据通路性能
- **位置**: dpdk_backend.cpp
- **问题**: 每次调用创建 `std::vector<rte_mbuf*>(budget)`
- **修复**: 改为栈上 `std::array<rte_mbuf*, 64>` 或类成员预分配

### F-08 · 热路径堆分配——UdpPayloadAssembler::push() 返回 vector
- **维度**: 数据通路性能
- **位置**: udp_payload_assembler.cpp `push()` 函数
- **问题**: 非分片的常见路径仍创建 `vector<UdpPayloadFrame>` 含一个 `vector<uint8_t>` payload
- **修复**: 对非分片路径（无 MF、offset=0）返回零拷贝视图而非 vector 拷贝；或改为回调/output 参数模式

### F-09 · recv_burst 静默截断 burst 大小
- **维度**: Socket/Datagram 接口一致性
- **位置**: dpdk_backend.cpp `min(max_burst, 64U)`
- **问题**: 调用者传入 `>64` 的 max_burst 时被无感知截断
- **修复**: 将 64 提取为 `kDpdkMaxBurstSize` 常量；在 init 阶段校验 `config.max_burst <= kDpdkMaxBurstSize`，超出时日志告警；或改为 `std::min(max_burst, kDpdkMaxBurstSize)` 并记录到 stats

### F-10 · RawFrameRecorder 使用原始 new/delete
- **维度**: 内存与资源管理
- **位置**: raw_frame_recorder.cpp
- **问题**: `impl_ = new Impl(config)` + 析构中手动 `delete`，异常时有泄漏风险
- **修复**: 改为 `std::unique_ptr<Impl>` 成员（头文件已声明 Impl 前向声明，兼容 pImpl 惯用法）

### F-11 · raw_frame_recorder 双锁顺序无保证
- **维度**: 并发与线程安全
- **位置**: raw_frame_recorder.cpp `queue_mutex` / `state_mutex`
- **问题**: `writer_loop` 中 `write_buffer → ensure_active_segment → close_active_segment` 可嵌套获取两把锁，无文档化的获取顺序
- **修复**: 在 Impl 类顶部添加注释声明锁序（`queue_mutex` < `state_mutex`）；或重构为单锁 + 手动 unlock 区间

### F-12 · 时钟域不一致风险
- **维度**: 接收链路正确性
- **位置**: cpi_state_coordinator.cpp `check_timeout()`; DPDK 后端 `steady_clock_now_ns()`
- **问题**: `first_rx_tsc` 来源随后端不同（DPDK TSC vs steady_clock），`check_timeout` 统一用 `steady_clock_now_ns()` 比较
- **修复**: 统一所有 `rx_tsc` 为 `steady_clock_now_ns()`（当前 DPDK 后端已使用，但需审计确认无遗漏）；或在 `UdpDatagramDesc` 层规范化时间戳语义并添加文档

### F-13 · 分片重组路径不一致
- **维度**: Socket/Datagram 接口一致性
- **位置**: dpdk_backend.cpp `adapt_frame()` vs udp_payload_assembler.cpp `push()`
- **问题**: DPDK adapter 直接拒绝分片包（`flags_and_offset & 0x3FFF != 0`），但 `UdpPayloadAssembler` 具备分片重组能力，两条路径行为不一致
- **修复**: 在 DPDK adapter 中添加分片支持，或在文档中明确声明"DPDK 路径不支持 IP 分片"并在指标中统计被丢弃的分片包数量

---

## P2 — 中优先级（工程质量/可维护性）

### F-14 · RxConfig 单体结构体
- **维度**: 配置与可维护性
- **位置**: rx_config.h
- **问题**: 60+ 字段无分组，维护和理解成本高
- **修复**: 拆分为子结构体：`NetworkConfig`, `DpdkConfig`, `SocketConfig`, `ProtocolConfig`, `CaptureConfig`, `RawRecordConfig`, `OutputConfig`

### F-15 · RunSummary ~150 字段臃肿
- **维度**: 配置与可维护性
- **位置**: metrics.h `RunSummary`
- **问题**: 字段语义重叠（`rx_packets` / `raw_rx_packets` / `captured_packets` / `recorded_packets`），按值传递开销大
- **修复**: 区分 `BackendSummary` / `PipelineSummary` / `CaptureSummary` / `CpiSummary`；按 `const &` 或 `unique_ptr` 传递

### F-16 · 路径工具函数重复定义
- **维度**: 配置与可维护性
- **位置**: raw_frame_recorder.cpp 和 receive_runner.cpp 中各自定义 `is_path_separator`, `join_path`, `path_filename`
- **问题**: 重复代码，修改易遗漏
- **修复**: 提取到 path_utils.h（已有部分引用 `internal/path_utils.h`，统一至此）

### F-17 · 字节序读取函数重复定义
- **维度**: 可移植性与工程规范 / 数据通路性能
- **位置**: sample_packet_parser.cpp, udp_payload_assembler.cpp, dpdk_backend.cpp 各自定义 `read_u16_le/be`, `read_u32_le/be`
- **问题**: 3+ 处重复匿名命名空间函数
- **修复**: 提取到 `include/rxtech/byte_order.h`，提供 `inline` 函数

### F-18 · output_drop_policy 用字符串比较
- **维度**: 配置与可维护性
- **位置**: cpi_state_coordinator.cpp `configure_output_policy()`
- **问题**: `"error"` / `"degrade"` 字符串比较，拼写错误不可捕获
- **修复**: 定义 `enum class OutputDropPolicy { degrade, error };`，配置加载时转换

### F-19 · truncated 标志未被消费
- **维度**: Socket/Datagram 接口一致性
- **位置**: udp_datagram.h `UdpDatagramDesc::truncated`
- **问题**: 字段存在但整条管道从不检查
- **修复**: 在 `UdpDatagramPipeline::process_datagram` 入口检查 `truncated`，若为 true 则 `on_reject(truncated)` 并跳过；或移除该字段

### F-20 · BackendStats 遗留字段
- **维度**: Socket/Datagram 接口一致性
- **位置**: rx_backend.h `BackendStats`
- **问题**: `umem_size`, `fill_ring_size`, `completion_ring_size` 仅适用于已移除的 AF_XDP 路径
- **修复**: 移除这三个字段并清理 `RunSummary` 中对应输出

### F-21 · 无配置校验层
- **维度**: 配置与可维护性
- **位置**: rx_config.cpp
- **问题**: 配置加载后不验证端口范围（0-65535）、PRT 上限合理性、路径可写性
- **修复**: 添加 `validate_config(const RxConfig&) → std::vector<std::string>` 校验函数，在 `run_app.cpp` 初始化时调用

### F-22 · POSIX 头文件无条件引入
- **维度**: 可移植性与工程规范
- **位置**: raw_frame_recorder.cpp `<dirent.h>`, `<sys/stat.h>`, `<unistd.h>`
- **问题**: Windows IDE 分析报错，虽然目标为 Linux-only，但应明确隔离
- **修复**: 用 `#if defined(__linux__) || defined(__unix__)` 包围 POSIX 特有头和实现；或将整个 Impl 拆为 `raw_frame_recorder_posix.cpp`

### F-23 · 全局 g_stop_requested 阻止多实例
- **维度**: 并发与线程安全
- **位置**: receive_runner.cpp `g_stop_requested`
- **问题**: 全局 `atomic<bool>` 使同进程多 ReceiveRunner 实例互相干扰
- **修复**: 将 stop flag 改为 `ReceiveRunner` 成员；信号 handler 通过全局指针转发到当前活跃实例

### F-24 · output_degraded_ 无原子保护
- **维度**: 并发与线程安全
- **位置**: `CpiStateCoordinator` 成员
- **问题**: 单线程写、多处读，虽实际安全但缺乏形式化保证
- **修复**: 将 `output_degraded_` 改为 `std::atomic<bool>` 或添加注释声明单线程访问约束

---

## P3 — 低优先级（可观测性/测试/工程完善）

### F-25 · 无结构化日志框架
- **维度**: 日志与可观测性
- **位置**: 全局
- **问题**: 全部基于 `ostream`，无级别/分类/时间戳
- **修复**: 引入 spdlog 或自研轻量级 logger，支持 level + timestamp + source location

### F-26 · CPU 指标未实现
- **维度**: 日志与可观测性
- **位置**: metrics.h `cpu_metrics_status`
- **问题**: 永远为 `"unavailable"`，cpu_user_pct 等始终 0.0
- **修复**: 通过 `/proc/self/stat` 或 `getrusage()` 采集 CPU 使用率，在 `finalize()` 中填充

### F-27 · 无日志限流
- **维度**: 日志与可观测性
- **位置**: `UdpDatagramPipeline` 和 `OwnerLoop` 中的诊断输出
- **问题**: 高频异常可能产生日志风暴
- **修复**: 添加速率限制器（如首次 + 每 N 秒 + 总计），或仅在 `RXTECH_DEBUG_DIAGNOSTICS` 开启时输出详细日志

### F-28 · 无外部指标导出
- **维度**: 日志与可观测性
- **位置**: 全局
- **问题**: 运行时指标仅在终端面板显示，无法被外部监控系统采集
- **修复**: 添加 JSON-over-Unix-socket 或 Prometheus text format endpoint

### F-29 · 无性能基准测试
- **维度**: 测试覆盖
- **位置**: tests/
- **问题**: 缺少 throughput/latency benchmark
- **修复**: 引入 Google Benchmark；为 `SpscRing`, `SlotWriter`, `PacketParser`, `CpiContext::reset` 添加微基准

### F-30 · 无 Fuzz 测试
- **维度**: 测试覆盖
- **位置**: tests/
- **问题**: parser 和 assembler 未接 fuzzer
- **修复**: 为 `PacketParser::parse`, `UdpPayloadAssembler::push` 添加 libFuzzer harness

### F-31 · 无 Sanitizer 集成
- **维度**: 测试覆盖
- **位置**: CMake 构建
- **问题**: 未配置 ASan/TSan/UBSan 构建 preset
- **修复**: 在 options.cmake 添加 `RXTECH_ENABLE_SANITIZERS` 选项，启用时添加 `-fsanitize=address,undefined` 或 `-fsanitize=thread`

### F-32 · CPI 超时路径无隔离测试
- **维度**: 测试覆盖
- **位置**: tests/unit/
- **问题**: `check_timeout` 的各分支（active 超时、previous 超时、无超时）缺独立单测
- **修复**: 添加 `test_cpi_timeout.cpp`，覆盖超时前/后/边界/嵌套超时场景

### F-33 · 无非法配置值测试
- **维度**: 测试覆盖
- **位置**: test_rx_config.cpp
- **问题**: 未覆盖非法端口、负数、超大值、空字符串等边界
- **修复**: 在 `test_rx_config.cpp` 添加非法值用例，验证 `validate_config()` 返回预期错误

### F-34 · 无 clang-format 配置
- **维度**: 可移植性与工程规范
- **位置**: 项目根目录
- **问题**: 代码格式无自动化强制
- **修复**: 添加 `.clang-format` 配置文件

### F-35 · 无静态分析集成
- **维度**: 可移植性与工程规范
- **位置**: CMake 构建
- **问题**: 缺少 clang-tidy / cppcheck
- **修复**: 添加 `.clang-tidy` 配置；CMake 中设置 `CMAKE_CXX_CLANG_TIDY`

### F-36 · 无 CI 配置
- **维度**: 可移植性与工程规范
- **位置**: 项目根目录
- **问题**: 缺少自动化构建/测试配置
- **修复**: 添加 `.github/workflows/build.yml` 或 Jenkinsfile，覆盖 build + test + sanitizer

### F-37 · -Werror 默认关闭
- **维度**: 可移植性与工程规范
- **位置**: options.cmake
- **问题**: 编译警告不阻断构建
- **修复**: CI 构建中开启 `-DRXTECH_WARNINGS_AS_ERRORS=ON`

### F-38 · 无包级全局序列号检测
- **维度**: 接收链路正确性
- **位置**: 协议层
- **问题**: 仅依赖 PRT/channel/packet_index 三元组，无法检测跨 CPI 的 gap
- **修复**: 如协议支持全局递增序列号，在 `InterpretedPacketView` 中添加 `global_seq` 字段并在 `DataOrderTracker` 中检测 gap

### F-39 · ParsedPacketView::payload_ptr 裸指针生存期无标注
- **维度**: 内存与资源管理
- **位置**: sample_packet_parser.h `ParsedPacketView`
- **问题**: 指针指向 burst 内存，burst release 后失效，无文档约束
- **修复**: 添加 `[[lifetimebound]]` 注解（C++26）或在成员上方添加 `/// @warning Lifetime tied to source burst` 注释

### F-40 · ofstream close 未检查错误
- **维度**: 内存与资源管理
- **位置**: receive_runner.cpp capture 流关闭; raw_frame_recorder.cpp `close_active_segment()`
- **问题**: `close()` 后未检查 `fail()` 或 `bad()`，磁盘满等写入错误被静默忽略
- **修复**: 在 `close()` 后检查 `stream.fail()`，失败时记录到 stats 或 failure_message

### F-41 · NIC 热插拔/link-down 无处理
- **维度**: 异常与鲁棒性
- **位置**: dpdk_backend.cpp
- **问题**: DPDK 后端无 link status 事件回调，NIC 异常时 `recv_burst` 可能持续返回空
- **修复**: 定期调用 `rte_eth_link_get_nowait()` 检测 link status，link-down 时设置 `run_error`

### F-42 · Pool 耗尽仅重试 3 次
- **维度**: 异常与鲁棒性
- **位置**: cpi_state_coordinator.cpp `open_active()`
- **问题**: 高速场景下 3 次 yield 可能不够
- **修复**: 添加指数退避（1us → 10us → 100us）；在超出阈值后记录 warning 而非直接 error

### F-43 · 无错误日志限流（重复解析失败）
- **维度**: 异常与鲁棒性
- **位置**: `UdpDatagramPipeline` 诊断输出
- **问题**: 同一 RejectReason 的高频事件可能产生海量输出
- **修复**: 首次出现时输出 + 此后每 N 秒聚合输出一次 + 总计数

### F-44 · 命名不一致
- **维度**: 配置与可维护性
- **位置**: `ProtocolSpec::cpi_timeout_ns` vs `RxConfig::protocol_cpi_timeout_ns`
- **问题**: 同一概念在不同层命名不一致，增加认知负担
- **修复**: 统一命名约定——RxConfig 中带 `protocol_` 前缀，ProtocolSpec 中去前缀；或全部统一

### F-45 · release_burst 语义不对称
- **维度**: Socket/Datagram 接口一致性
- **位置**: 三个后端的 `release_burst()` 实现
- **问题**: DPDK 释放 mbuf，其他后端为空操作
- **修复**: 在 `IRxBackend` 文档中明确 release_burst 的语义契约；或改为 RAII guard 模式（已部分实现 `BurstReleaseGuard`）

---

## 汇总统计

| 优先级 | 数量 | 涉及维度 |
|--------|------|---------|
| **P0** | 5 | 性能, 内存, 并发, 正确性 |
| **P1** | 8 | 性能, 一致性, 内存, 并发, 正确性 |
| **P2** | 11 | 可维护性, 可移植性, 工程规范 |
| **P3** | 21 | 可观测性, 测试, 工程完善 |
| **合计** | **45** | |
