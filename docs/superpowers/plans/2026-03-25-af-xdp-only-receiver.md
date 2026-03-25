# AF_XDP-Only Receiver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `rx_tech_demo` 重构为 AF_XDP 唯一主线接收端，删除 socket 联调资产，并在服务器 worktree 中完成环境整备、主入口收敛与基础验证。

**Architecture:** 保留 `benchmark_core` 的协议/重组/指标/报告逻辑，删除 `socket` 主线及其资产，让 `rxbench_xdp + af_xdp backend` 成为唯一接收入口。验证工作优先在服务器 `/home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver` 中直接进行。

**Tech Stack:** C++17, AF_XDP, libbpf 0.8.1, libxdp 1.2.9 (prefix), Intel X710/i40e, CMake, server-side worktree

---

## File Map

- Modify: `src/apps/CMakeLists.txt`
- Delete: `src/apps/rxbench_socket_main.cpp`
- Modify: `src/backends/CMakeLists.txt`
- Delete: `src/backends/socket/CMakeLists.txt`
- Delete: `src/backends/socket/include/rxtech/socket_backend.h`
- Delete: `src/backends/socket/src/socket_backend.cpp`
- Modify: `src/apps/common/app_main_common.cpp`
- Modify: `src/apps/common/app_main_common.h`
- Modify: `src/apps/common/cli_args.cpp`
- Modify: `src/apps/common/cli_args.h`
- Modify: `src/benchmark_core/include/rxtech/rx_config.h`
- Modify: `src/benchmark_core/src/rx_config.cpp`
- Modify: `src/benchmark_core/src/bench_runner.cpp`
- Modify: `src/backends/af_xdp/src/xdp_backend.cpp`
- Modify: `src/backends/af_xdp/include/rxtech/xdp_backend.h`
- Modify: `tests/integration/CMakeLists.txt`
- Delete or Modify: `tests/integration/test_bench_smoke.cpp`
- Delete: `tests/integration/test_socket_three_port.cpp`
- Modify: `tests/integration/test_bench_fake.cpp`
- Modify: `tests/unit/CMakeLists.txt`
- Delete or Modify: any socket-specific unit tests if they exist
- Delete: `configs/rx_sender_link.conf`
- Create: `configs/af_xdp_receiver0.conf`
- Delete: `scripts/run_sender_link_smoke.sh`
- Modify: `scripts/check_af_xdp_env.sh`
- Modify: `scripts/run_af_xdp_benchmark.sh`
- Modify: `README.md`
- Modify: `docs/接收端适配说明.md`
- Modify: `docs/设计方案/AF_XDP_DPDK_准备与分工.md`

### Task 1: 收敛构建入口为 AF_XDP 主线

**Files:**
- Modify: `src/apps/CMakeLists.txt`
- Delete: `src/apps/rxbench_socket_main.cpp`
- Modify: `src/backends/CMakeLists.txt`
- Delete: `src/backends/socket/CMakeLists.txt`
- Delete: `src/backends/socket/include/rxtech/socket_backend.h`
- Delete: `src/backends/socket/src/socket_backend.cpp`
- Test: configure/build on server worktree

- [ ] **Step 1: 写失败检查，确认构建系统仍引用 socket 主线**
- [ ] **Step 2: 删除 socket app/backend 构建入口**
- [ ] **Step 3: 让 `rxtech_app_common` 只暴露 AF_XDP/DPDK 可用入口**
- [ ] **Step 4: 在服务器 worktree 重新构建**

Run:
```bash
cd /home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver
bash ./scripts/build_server_shared_cache.sh
```

Expected:
- 不再生成 `rxbench_socket`
- `rxbench_xdp` 仍可构建

### Task 2: 删除 socket 联调资产并替换为 AF_XDP 配置

**Files:**
- Delete: `configs/rx_sender_link.conf`
- Create: `configs/af_xdp_receiver0.conf`
- Delete: `scripts/run_sender_link_smoke.sh`
- Modify: `src/benchmark_core/include/rxtech/rx_config.h`
- Modify: `src/benchmark_core/src/rx_config.cpp`
- Modify: `src/apps/common/cli_args.cpp`
- Modify: `src/apps/common/app_main_common.cpp`
- Test: `--dry-run`

- [ ] **Step 1: 写失败测试或 dry-run 断言，证明当前配置仍偏 socket**
- [ ] **Step 2: 删除 socket 配置，新增 AF_XDP 专用配置**
- [ ] **Step 3: 让 CLI/dry-run 只展示 AF_XDP 相关运行信息**
- [ ] **Step 4: 在服务器上验证新配置**

Run:
```bash
cd /home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
./build/src/apps/rxbench_xdp --config ./configs/af_xdp_receiver0.conf --dry-run
```

### Task 3: 固化 AF_XDP 前台长时间运行模型

**Files:**
- Modify: `src/benchmark_core/src/bench_runner.cpp`
- Modify: `tests/integration/test_bench_fake.cpp`
- Modify: `src/apps/common/app_main_common.cpp`

- [ ] **Step 1: 写失败测试，覆盖 `--until-stopped` 的 10s 周期输出**
- [ ] **Step 2: 保留前台手动关闭模式，不再文档化 tmux/后台隔离**
- [ ] **Step 3: 验证 AF_XDP 主入口也有相同终端输出体验**

Run:
```bash
cd /home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver
cd build/tests/integration
ctest --output-on-failure -R rxtech_integration_fake_tests
```

### Task 4: 让 AF_XDP 后端贴近真实 sender0 流量

**Files:**
- Modify: `src/backends/af_xdp/src/xdp_backend.cpp`
- Modify: `src/backends/af_xdp/include/rxtech/xdp_backend.h`
- Modify: `scripts/check_af_xdp_env.sh`
- Modify: `scripts/run_af_xdp_benchmark.sh`
- Create or Modify: queue-selection notes/config
- Test: `rxbench_xdp`

- [ ] **Step 1: 写失败验证，证明当前 `receiver0 queue 0` 仍然 0 包**
- [ ] **Step 2: 增加 queue 选择/记录机制，避免 RSS 错绑**
- [ ] **Step 3: 若需要，修复 `libbpf: can't get next link` 相关兼容路径**
- [ ] **Step 4: 在 sender0 实流量存在时重新跑 `rxbench_xdp`**

Run:
```bash
cd /home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
sudo ./build/src/apps/rxbench_xdp rx_only "" results/af_xdp_receiver0_try receiver0 <queue> 2
```

Expected:
- `xdp_attach_mode=driver`
- `xsk_mode=copy`
- `rx_packets > 0`

### Task 5: 删除 socket 文档并重写仓库主线说明

**Files:**
- Modify: `README.md`
- Modify: `docs/接收端适配说明.md`
- Modify: `docs/设计方案/AF_XDP_DPDK_准备与分工.md`
- Delete or rewrite any socket-specific docs

- [ ] **Step 1: 清理 socket 主线表述**
- [ ] **Step 2: 把 AF_XDP 写成唯一推荐接收路径**
- [ ] **Step 3: 明确服务器 worktree、前台运行、queue 选择和验证命令**

## Definition Of Done

- 仓库不再构建 `socket` 接收主线
- AF_XDP 成为唯一推荐接收入口
- socket 相关配置、脚本、测试、文档主线已清理
- `rxbench_xdp` 能以前台长时间模式运行并每 10s 输出状态
- 在真实 sender0 流量存在时，`receiver0` 上的 AF_XDP 路径能收到包
