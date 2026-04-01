# Receiver Pipeline Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize the codebase into a pure receiver pipeline structure with DPDK as the active mainline and AF_XDP moved into legacy code.

**Architecture:** Build a new `src/receiver` tree around the receive flow, migrate the existing runtime, protocol, storage, output, and DPDK ingress pieces into it, and downgrade AF_XDP into `src/legacy/af_xdp`. Keep behavior stable while making the source tree, targets, docs, and tests reflect a receiver product instead of a benchmark framework.

**Tech Stack:** CMake, C++17, DPDK, retained AF_XDP legacy code, local Windows build/test, Linux server validation via `ssh kds`

---

### Task 1: Create the New Source Skeleton

**Files:**
- Create: `src/receiver/`
- Create: `src/legacy/`
- Modify: `CMakeLists.txt`
- Modify: `src/apps/CMakeLists.txt`
- Modify: `src/backends/CMakeLists.txt`

- [ ] **Step 1: Create the new top-level source directories**

Create:
- `src/receiver/app/cli`
- `src/receiver/core`
- `src/receiver/ingress/dpdk`
- `src/receiver/protocol`
- `src/receiver/admit`
- `src/receiver/storage`
- `src/receiver/finalize`
- `src/receiver/output`
- `src/receiver/sidecar`
- `src/receiver/runtime`
- `src/legacy/af_xdp`

- [ ] **Step 2: Add the new CMake skeleton**

Run: local CMake reconfigure
Expected: configure succeeds with new subtree placeholders

- [ ] **Step 3: Commit the structural skeleton**

```bash
git add CMakeLists.txt src
git commit -m "refactor(receiver): 建立纯接收流程目录骨架"
```

### Task 2: Move the Runtime and App Entry Path

**Files:**
- Create: `src/receiver/app/main_dpdk.cpp`
- Create: `src/receiver/app/cli/cli_args.h`
- Create: `src/receiver/app/cli/cli_args.cpp`
- Create: `src/receiver/runtime/receive_runner.h`
- Create: `src/receiver/runtime/receive_runner.cpp`
- Create: `src/receiver/runtime/receive_context.h`
- Create: `src/receiver/runtime/receiver_config.h`
- Create: `src/receiver/runtime/receiver_config.cpp`
- Modify: `src/apps/common/app_main_common.cpp`
- Modify: `src/apps/common/cli_args.*`
- Delete or stop building: `src/apps/*` old mainline files

- [ ] **Step 1: Write a failing build expectation**

Run: local build target for the new runtime/app
Expected: missing file / include / target failures

- [ ] **Step 2: Move the current runtime and CLI code into the new receiver tree**

Preserve behavior:
- config load/merge
- dry-run
- receive-runner execution
- DPDK main entry

- [ ] **Step 3: Run the focused build**

Run: local build for the app targets
Expected: receiver app builds from the new paths

- [ ] **Step 4: Commit the runtime/app migration**

```bash
git add CMakeLists.txt src/receiver src/apps
git commit -m "refactor(receiver): 迁移运行时与入口到 receiver 主线"
```

### Task 3: Move DPDK Ingress and Shared Runtime Headers

**Files:**
- Create: `src/receiver/ingress/dpdk/*`
- Create: `src/receiver/runtime/rx_backend.h`
- Create: `src/receiver/runtime/packet_desc.h`
- Create: `src/receiver/runtime/time_utils.h`
- Modify: `src/backends/dpdk/*`
- Modify: include references in runtime/protocol/tests

- [ ] **Step 1: Write a failing build expectation for DPDK ingress includes**

Run: local build
Expected: include path or missing target errors after path changes

- [ ] **Step 2: Move DPDK ingress code into the new ingress layer**

Keep:
- backend interface compatibility
- existing DPDK init/poll/release behavior

- [ ] **Step 3: Rebuild**

Run: local full build
Expected: DPDK-related targets compile from `src/receiver/ingress/dpdk`

- [ ] **Step 4: Commit the ingress migration**

```bash
git add src/receiver src/backends/dpdk CMakeLists.txt
git commit -m "refactor(receiver): 迁移 DPDK 接收入口到 ingress 分层"
```

### Task 4: Move Protocol, Metrics, and Storage-Oriented Code

**Files:**
- Create: `src/receiver/protocol/*`
- Create: `src/receiver/storage/*`
- Create: `src/receiver/sidecar/*`
- Modify: existing parser/reassembly/metrics files
- Modify: tests under `tests/unit`

- [ ] **Step 1: Write failing build or test expectations**

Run: focused unit test build
Expected: path/include failures while files are being moved

- [ ] **Step 2: Move the current protocol and sidecar/storage code**

Map:
- parser/demo_protocol -> `protocol`
- reassembly -> `storage`
- metrics -> `sidecar`

- [ ] **Step 3: Fix includes and target boundaries**

Ensure:
- no remaining `benchmark_core` include paths in active code
- tests reference the new receiver paths

- [ ] **Step 4: Run unit tests**

Run: local unit/integration tests
Expected: tests pass from the new layout

- [ ] **Step 5: Commit the protocol/storage migration**

```bash
git add src/receiver tests CMakeLists.txt
git commit -m "refactor(receiver): 迁移协议 存储与 sidecar 模块"
```

### Task 5: Downgrade AF_XDP into Legacy

**Files:**
- Create: `src/legacy/af_xdp/*`
- Modify: `src/backends/af_xdp/*`
- Modify: top-level and per-dir CMake
- Modify: app target wiring

- [ ] **Step 1: Move AF_XDP sources into `src/legacy/af_xdp`**

Preserve code, but remove it from the main receiver source semantics.

- [ ] **Step 2: Update CMake**

Ensure:
- AF_XDP is built only as a legacy-compatible target
- DPDK remains the primary active app path

- [ ] **Step 3: Rebuild**

Run: local full build
Expected: both mainline DPDK targets and legacy AF_XDP targets compile as configured

- [ ] **Step 4: Commit the AF_XDP downgrade**

```bash
git add src/legacy src/backends/af_xdp CMakeLists.txt
git commit -m "refactor(receiver): 降级 AF_XDP 到 legacy 目录"
```

### Task 6: Simplify Naming, Docs, and Config Entry Points

**Files:**
- Modify: `README.md`
- Modify: `configs/*.conf`
- Modify: relevant tests
- Modify: executable names and target names if safe in this round

- [ ] **Step 1: Update project wording**

Change README and config descriptions to:
- pure receiver software
- DPDK active mainline
- AF_XDP legacy/compatibility

- [ ] **Step 2: Remove stale path references**

Specifically remove:
- benchmark terminology
- deleted worktree instructions
- AF_XDP-as-mainline wording

- [ ] **Step 3: Run docs/config sanity checks**

Run: dry-run or config parsing checks
Expected: config tests still pass

- [ ] **Step 4: Commit the naming/docs cleanup**

```bash
git add README.md configs tests
git commit -m "docs(receiver): 对齐纯接收端主线口径"
```

### Task 7: Verify Locally and on the Server

**Files:**
- Verify: local build outputs
- Verify: server workspace `/home/devuser/WorkSpace/rx_tech_demo`
- Verify: server build/test scripts already in repo

- [ ] **Step 1: Run local verification**

Run locally:
- full build
- all tests

Expected:
- build passes
- tests pass

- [ ] **Step 2: Sync code to the server repository**

Use:
- git push to `gitea/main`
- `ssh kds`
- server fetch/reset to `gitea/main`

- [ ] **Step 3: Run server build**

Run on Linux server in:
- `/home/devuser/WorkSpace/rx_tech_demo`

Command source:
- existing active repo script `scripts/build_server_shared_cache.sh`

- [ ] **Step 4: Run server tests**

Run on Linux server in:
- `/home/devuser/WorkSpace/rx_tech_demo/build`

Command:
- `ctest --output-on-failure`

- [ ] **Step 5: Commit the final integrated refactor**

```bash
git add -A
git commit -m "refactor(receiver): 重组为纯接收流程分层结构"
```
