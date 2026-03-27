# AF_XDP Throughput Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在单核优先的约束下，先优化 AF_XDP 接收主路径以提升吞吐、降低丢包，并为后续“纯接收 vs 接收+解析”对照测试提供可调参数。

**Architecture:** 本阶段只收敛 `af_xdp` backend 和 `RxConfig`。先把 ring 深度、fill/completion 回填和 poll 策略从写死常量改为可调，并去掉明显的每包固定开销；不在本阶段引入线程拆分。

**Tech Stack:** C++17, AF_XDP/libbpf, CMake, existing unit tests

---

## File Map

- Modify: `src/benchmark_core/include/rxtech/rx_config.h`
- Modify: `src/benchmark_core/src/rx_config.cpp`
- Modify: `src/backends/af_xdp/src/xdp_backend.cpp`
- Modify: `tests/unit/test_rx_config.cpp`
- Modify: `tests/unit/test_merge_config.cpp`

### Task 1: 为 AF_XDP 调优参数补配置入口

**Files:**
- Modify: `src/benchmark_core/include/rxtech/rx_config.h`
- Modify: `src/benchmark_core/src/rx_config.cpp`
- Modify: `tests/unit/test_rx_config.cpp`
- Modify: `tests/unit/test_merge_config.cpp`

- [ ] **Step 1: 先写失败测试，覆盖新增 XDP ring/frame/poll 配置字段解析与 merge**
- [ ] **Step 2: 运行单元测试确认它先失败**
- [ ] **Step 3: 在 `RxConfig` 中新增 AF_XDP 调优参数并接入配置解析**
- [ ] **Step 4: 重新运行单元测试确认通过**

### Task 2: 优化 AF_XDP 接收主路径

**Files:**
- Modify: `src/backends/af_xdp/src/xdp_backend.cpp`

- [ ] **Step 1: 将 ring/frame 常量改为从配置驱动，并做最小边界保护**
- [ ] **Step 2: 把 fill/completion 回填改为“尽力回填”，避免整批 reserve 自旋**
- [ ] **Step 3: 把 `recv_burst()` 改为先 peek 再决定是否 poll**
- [ ] **Step 4: 把时间戳从 per-packet 改为 per-burst**

### Task 3: 验证与记录

**Files:**
- Modify: `progress.md`
- Modify: `findings.md`

- [ ] **Step 1: 运行受影响的单元测试**
- [ ] **Step 2: 如果本地构建可用，做一次快速构建验证**
- [ ] **Step 3: 更新 planning 文件，记录本阶段完成情况与后续对照测试建议**
