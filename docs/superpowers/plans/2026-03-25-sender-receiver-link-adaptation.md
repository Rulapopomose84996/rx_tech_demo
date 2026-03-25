# Sender-Receiver Link Adaptation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `rx_tech_demo` 基于当前 sender 三口 UDP + `DemoHeader` 基线，具备稳定的三口独立接收、协议解析、分片重组、每口统计和联调验证能力，为与另一发射主机建立稳定发送-接收链路提供可执行实现路径。

**Architecture:** 保留现有 backend/mode/benchmark core 分层，不把 sender 私有实现细节耦合进 backend。新增协议与重组能力优先落在 benchmark core 和独立 mode 中，由 backend 负责把每口原始 UDP 报文交上来，再由 mode 完成 `DemoHeader` 校验、按 `(port, block_id)` 重组和每口统计，最后通过现有报告链路输出结果。

**Tech Stack:** C++17, CMake, Linux socket/AF_XDP/DPDK backends, benchmark core, existing unit/integration tests, remote validation via `ssh kds`

---

## File Map

- Modify: `src/benchmark_core/include/rxtech/packet_desc.h`
  - 扩充 packet 来源信息，明确每口/每流区分能力是否足够支撑三口重组。
- Modify: `src/benchmark_core/include/rxtech/parser.h`
  - 将占位 `ParsedPacketMeta` 提升为真实 `DemoHeader` 解析结果承载体。
- Modify: `src/benchmark_core/src/parser.cpp`
  - 实现 `DemoHeader` 基础校验与字段提取。
- Create: `src/benchmark_core/include/rxtech/demo_protocol.h`
  - 本仓库内保存接收端所需的 sender 协议结构、magic/version/flags 常量与辅助函数。
- Create: `src/benchmark_core/include/rxtech/reassembly.h`
  - 定义 block 重组状态、超时规则、每口重组结果。
- Create: `src/benchmark_core/src/reassembly.cpp`
  - 实现按 `(port, block_id)` 的分片重组、缺片/重片/超时统计。
- Modify: `src/benchmark_core/include/rxtech/metrics.h`
  - 增加每口统计和重组统计字段，并保持现有汇总输出兼容。
- Modify: `src/benchmark_core/src/metrics.cpp`
  - 实现新增统计项累加、汇总与摘要产出。
- Modify: `src/benchmark_core/include/rxtech/rx_config.h`
  - 扩展三口接收配置表达能力，例如多 endpoint、重组超时、端口映射。
- Modify: `src/benchmark_core/src/rx_config.cpp`
  - 支持新配置字段加载与 merge。
- Modify: `src/backends/socket/include/rxtech/socket_backend.h`
  - 让 socket backend 能管理多 socket 或明确新增多口 backend 的接口要求。
- Modify: `src/backends/socket/src/socket_backend.cpp`
  - 支持 3 个接收端口并将 `port_id`/源信息正确写入 `PacketDesc`。
- Modify: `src/modes/parse/include/rxtech/parse_mode.h`
  - 为真实协议解析与重组 mode 准备状态。
- Modify: `src/modes/parse/src/parse_mode.cpp`
  - 从“逐包占位解析”升级为“逐包解析 + 每口重组 + 指标上报”。
- Modify: `src/apps/common/app_main_common.cpp`
  - 暴露三口配置与 dry-run 输出，确保 CLI 可见真实接收拓扑。
- Modify: `src/benchmark_core/src/bench_runner.cpp`
  - 确认 step 级执行与结果汇总能覆盖新增 per-port / reassembly 指标。
- Modify: `src/benchmark_core/include/rxtech/report_writer.h`
  - 如现有 writer 接口不足，增加 per-port 结果输出结构。
- Modify: `src/benchmark_core/src/report_writer_json.cpp`
  - 输出 per-port 和重组指标到 JSON。
- Modify: `src/benchmark_core/src/report_writer_csv.cpp`
  - 决定 CSV 是平铺 aggregate 字段还是新增 per-port 明细文件。
- Modify: `tests/unit/test_parser.cpp`
  - 覆盖 `DemoHeader` 合法/非法解析。
- Create: `tests/unit/test_reassembly.cpp`
  - 覆盖顺序乱序、尾片、重复片、缺片、超时。
- Modify: `tests/unit/test_metrics.cpp`
  - 覆盖新增 per-port 和重组统计。
- Modify: `tests/unit/test_rx_config.cpp`
  - 覆盖三口配置和 merge 语义。
- Create: `tests/integration/test_socket_three_port.cpp`
  - 覆盖三口 socket + parse/reassembly 的最小闭环。
- Create: `configs/rx_sender_link.conf`
  - 三口接收的正式配置样例。
- Create: `scenarios/sender_link_smoke.yaml`
  - 面向联调的最小场景文件。
- Create: `scripts/run_sender_link_smoke.sh`
  - 一键执行 socket 基线或服务器联调 smoke。
- Modify: `README.md`
  - 更新项目状态与 sender 联调入口。
- Modify: `docs/接收端适配说明.md`
  - 回填“本仓库落地后的接收侧实现映射”与联调步骤。

### Task 1: 固化协议基线与配置模型

**Files:**
- Create: `src/benchmark_core/include/rxtech/demo_protocol.h`
- Modify: `src/benchmark_core/include/rxtech/parser.h`
- Modify: `src/benchmark_core/src/parser.cpp`
- Modify: `src/benchmark_core/include/rxtech/rx_config.h`
- Modify: `src/benchmark_core/src/rx_config.cpp`
- Test: `tests/unit/test_parser.cpp`
- Test: `tests/unit/test_rx_config.cpp`

- [ ] **Step 1: 复制 sender 文档里稳定协议事实到本仓库类型系统**

定义接收端只依赖的 sender 协议事实：
- `DemoHeader` 的字段布局
- `magic/version/flags`
- `stream_id/block_id/block_bytes/frag_idx/frag_count/frag_payload_bytes`
- 三口 endpoint 映射默认值 `172.20.11/12/13.*` 与 `5010/5011/5012`

- [ ] **Step 2: 让 `ParsedPacketMeta` 承载真实解析结果**

至少包含：
- header 合法性
- `port_id`
- `stream_id`
- `block_id`
- `frag_idx`
- `frag_count`
- `frag_payload_bytes`
- `block_bytes`
- invalid reason

- [ ] **Step 3: 写失败测试覆盖合法头、坏 magic、坏 version、长度不足**

运行位置：项目根目录的 Linux/WSL 环境

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build build --target test_parser
ctest --test-dir build/tests/unit --output-on-failure -R test_parser
```

Expected:
- 当前占位 parser 无法通过新增测试

- [ ] **Step 4: 实现最小解析逻辑直到测试通过**

要求：
- 不在 parser 中做重组
- 只做包级字段提取和基础一致性校验
- 为后续重组保留错误原因

- [ ] **Step 5: 扩展配置以表达三口接收拓扑**

至少支持：
- 三个 receiver UDP 端口
- 每口 bind 地址
- 重组超时毫秒数
- 是否启用 sender 默认映射

- [ ] **Step 6: 运行 parser/config 单元测试并提交**

运行位置：项目根目录的 Linux/WSL 环境

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
ctest --test-dir build/tests/unit --output-on-failure -R "test_parser|test_rx_config"
git add src/benchmark_core/include/rxtech/demo_protocol.h src/benchmark_core/include/rxtech/parser.h src/benchmark_core/src/parser.cpp src/benchmark_core/include/rxtech/rx_config.h src/benchmark_core/src/rx_config.cpp tests/unit/test_parser.cpp tests/unit/test_rx_config.cpp
git commit -m "feat: add sender demo protocol parsing"
```

### Task 2: 实现每口独立 block 重组

**Files:**
- Create: `src/benchmark_core/include/rxtech/reassembly.h`
- Create: `src/benchmark_core/src/reassembly.cpp`
- Modify: `src/modes/parse/include/rxtech/parse_mode.h`
- Modify: `src/modes/parse/src/parse_mode.cpp`
- Test: `tests/unit/test_reassembly.cpp`

- [ ] **Step 1: 设计重组器状态模型**

要求：
- key 为 `(port_id, block_id)`
- fragment 位图或等价结构
- 记录 `frag_count`、`block_bytes`、已收字节数、最近更新时间
- 支持乱序和尾片长度

- [ ] **Step 2: 先写重组器失败测试**

测试覆盖：
- 顺序完整 block
- 乱序完整 block
- duplicate fragment
- missing fragment
- timeout eviction
- `flags.first/last` 与 `frag_idx/frag_count` 冲突时的辅助校验

- [ ] **Step 3: 实现最小重组器直到测试通过**

要求：
- 只在 block 完整时产出 reassembled result
- incomplete block 超时后计入 timeout/missing
- duplicate 不能污染完整 block 计数

- [ ] **Step 4: 把 parse mode 升级为“解析 + 重组”流水线**

要求：
- 单个 burst 中逐包 parse
- invalid header 直接计数
- valid packet 进入对应 port 的重组状态
- block 完整后上报 reassembled block

- [ ] **Step 5: 运行重组与 parse mode 相关单测并提交**

运行位置：项目根目录的 Linux/WSL 环境

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
ctest --test-dir build/tests/unit --output-on-failure -R "test_reassembly|test_parser"
git add src/benchmark_core/include/rxtech/reassembly.h src/benchmark_core/src/reassembly.cpp src/modes/parse/include/rxtech/parse_mode.h src/modes/parse/src/parse_mode.cpp tests/unit/test_reassembly.cpp
git commit -m "feat: add per-port block reassembly"
```

### Task 3: 打通三口 socket 接收与每口指标

**Files:**
- Modify: `src/backends/socket/include/rxtech/socket_backend.h`
- Modify: `src/backends/socket/src/socket_backend.cpp`
- Modify: `src/benchmark_core/include/rxtech/packet_desc.h`
- Modify: `src/benchmark_core/include/rxtech/metrics.h`
- Modify: `src/benchmark_core/src/metrics.cpp`
- Modify: `src/benchmark_core/src/bench_runner.cpp`
- Test: `tests/unit/test_metrics.cpp`
- Test: `tests/integration/test_socket_three_port.cpp`

- [ ] **Step 1: 决定多口接收落点**

优先方案：
- 保持 `socket` 作为 backend 名称
- backend 内部管理 3 个 `recv_fd`
- 每次 poll/recvmmsg 将包写回 `PacketDesc.port_id`

备选方案：
- 若改造成本过高，新建 `socket_multi` backend，但必须说明为什么不复用现有 `socket`

- [ ] **Step 2: 为 metrics 增加接收端最低必需统计**

至少包含：
- per-port received packets
- per-port received bytes
- per-port throughput
- reassembled blocks
- missing fragments
- duplicate fragments
- invalid header count
- reassembly timeout count

- [ ] **Step 3: 先写 metrics 和三口 integration 测试**

integration 测试要求：
- 本地 loopback 或本进程 UDP sender 向 `5010/5011/5012` 注入模拟 `DemoHeader` 报文
- 验证三口独立计数
- 验证某一口异常不会污染其它口统计

- [ ] **Step 4: 实现三口 socket backend 与指标聚合**

要求：
- 不把三口报文在 backend 层合流成无来源数据
- `PacketDesc` 要保留 port/queue/source 信息
- backend stats 和 mode stats 口径不要重复计数

- [ ] **Step 5: 更新 `bench_runner` / report writer 以落盘新结果**

至少保证：
- `summary.json` 有 aggregate 指标
- 另有 per-port 结构可供联调排查
- dry-run 能打印三口配置

- [ ] **Step 6: 运行单测/集成测试并提交**

运行位置：项目根目录的 Linux/WSL 环境

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
ctest --test-dir build/tests/unit --output-on-failure -R "test_metrics|test_parser|test_reassembly"
ctest --test-dir build/tests/integration --output-on-failure -R test_socket_three_port
git add src/backends/socket/include/rxtech/socket_backend.h src/backends/socket/src/socket_backend.cpp src/benchmark_core/include/rxtech/packet_desc.h src/benchmark_core/include/rxtech/metrics.h src/benchmark_core/src/metrics.cpp src/benchmark_core/src/bench_runner.cpp src/benchmark_core/src/report_writer_json.cpp src/benchmark_core/src/report_writer_csv.cpp tests/unit/test_metrics.cpp tests/integration/test_socket_three_port.cpp
git commit -m "feat: support three-port socket receive metrics"
```

### Task 4: 补齐配置、场景、CLI 和联调脚本

**Files:**
- Modify: `src/apps/common/app_main_common.cpp`
- Create: `configs/rx_sender_link.conf`
- Create: `scenarios/sender_link_smoke.yaml`
- Create: `scripts/run_sender_link_smoke.sh`
- Modify: `README.md`
- Modify: `docs/接收端适配说明.md`

- [ ] **Step 1: 让 dry-run 输出真实三口 endpoint 和重组参数**

要求：
- 能在不启 backend 的情况下确认三口绑定、模式、超时、输出目录

- [ ] **Step 2: 新增联调配置样例**

配置必须反映 sender 文档默认映射：
- `sender0 -> receiver:5010`
- `sender1 -> receiver:5011`
- `sender2 -> receiver:5012`

- [ ] **Step 3: 新增 smoke 场景和执行脚本**

脚本目标：
- 本地 socket smoke
- 服务器上真实 sender 联调 smoke 的统一入口
- 输出结果到独立目录，避免污染历史 benchmark

- [ ] **Step 4: 更新 README 和接收端说明**

文档应明确：
- 当前已支持到什么程度
- 本地验证和真实链路验证的区别
- 排查顺序按“每口独立链路”展开

- [ ] **Step 5: 执行 dry-run 验证并提交**

运行位置：项目根目录的 Linux/WSL 环境

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
python3 scripts/run_matrix.py --dry-run
./build/src/apps/rxbench_socket_main --config configs/rx_sender_link.conf --scenario scenarios/sender_link_smoke.yaml --dry-run
git add src/apps/common/app_main_common.cpp configs/rx_sender_link.conf scenarios/sender_link_smoke.yaml scripts/run_sender_link_smoke.sh README.md docs/接收端适配说明.md
git commit -m "docs: add sender receiver link configuration"
```

### Task 5: 服务器联调与稳定性验证

**Files:**
- Modify: `progress.md` or dedicated validation note if execution session requires logging
- Output: `results/sender_link_smoke/*`

- [ ] **Step 1: 本地改动完成后先同步代码到远端**

运行位置：Windows PowerShell

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo"
git status --short
git push
```

- [ ] **Step 2: 服务器拉取并构建**

运行位置：Linux server

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
git pull
./scripts/build_server_shared_cache.sh
```

- [ ] **Step 3: 服务器单测与集成测试**

运行位置：Linux server

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure
```

运行位置：Linux server

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/integration
ctest --output-on-failure
```

- [ ] **Step 4: 在真实链路上执行 sender-receiver smoke**

运行位置：Linux server

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/run_sender_link_smoke.sh
```

验证目标：
- 三口都能独立收到包
- `invalid header count` 为 0
- `reassembly timeout count` 可控
- 每口吞吐与 sender 侧统计在同一量级

- [ ] **Step 5: 若 `ssh kds` 不可用，按本地 WSL fallback 验证**

运行位置：WSL on Windows

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
./scripts/build_production.sh
ctest --test-dir build/tests/unit --output-on-failure
ctest --test-dir build/tests/integration --output-on-failure
```

说明：
- fallback 仅验证功能正确性和本地 socket 三口闭环
- 不产出真实链路性能结论

## Risks And Gates

- `DemoHeader` 的精确结构当前仅在 sender 文档里被字段级引用，真正编码布局必须以 sender 头文件为准；在拿到真实头文件前，不应冻结二进制兼容实现。
- 三口能力放在 backend 还是 mode 之间的边界需要尽早定死，否则会在 socket 与 AF_XDP/DPDK 迁移时重复返工。
- 报告格式若只保留 aggregate，会直接削弱联调排障能力；per-port 明细必须在首轮计划内完成。
- 真实链路验证依赖另一发射主机和服务器环境，预计超过 1 分钟；实施时应按 AGENTS 约束把命令明确给出，不默认长时间执行。

## Definition Of Done

- `parse` 模式能解析 sender `DemoHeader`
- 接收端能按三口独立 socket/port 接收 `5010/5011/5012`
- 能按 `(port, block_id)` 完成 block 重组
- 结果中可见每口吞吐、重组、缺片、重片、超时、非法头统计
- 本地 integration test 覆盖三口 socket 最小闭环
- 服务器环境完成至少一次 smoke 联调并产出结果目录
