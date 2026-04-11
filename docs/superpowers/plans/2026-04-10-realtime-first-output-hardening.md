# Realtime-First Output Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden the CPI finalize/output path so slow consumers never block the receive hot path; finalized output may be dropped, but the drop must be immediate, observable, and reflected in the final run result.

**Architecture:** Keep the current single-owner receive loop and asynchronous consumer model, but make the finalize path explicitly non-blocking. Output enqueue becomes a one-shot `push`; on failure the coordinator immediately records a realtime degradation event, releases the pool slot, and upgrades the run conclusion according to config. Metrics, summary, and status panel are extended so output-side degradation is visible without changing the zero-backpressure behavior.

**Tech Stack:** C++17, CMake 3.16.x, Ninja, existing `OwnerLoop`/`CpiStateCoordinator`/`CpiConsumer` pipeline, Linux server validation via `ssh kds`

---

## File Map

- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\include\rxtech\rx_config.h`
  - Add output drop policy and configurable ring capacities.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\runtime\rx_config.cpp`
  - Parse and merge the new output-related config keys.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\include\rxtech\metrics.h`
  - Add explicit output degradation counters and final run-state fields.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\core\internal\cpi_state_coordinator.h`
  - Add a small runtime policy/result surface for zero-blocking output decisions.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\core\cpi_state_coordinator.cpp`
  - Implement immediate-drop finalize behavior and slot release on output congestion.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\core\owner_loop.cpp`
  - Replace the fixed ring capacity with config-driven values and thread the output policy into the coordinator.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\core\owner_loop_summary.cpp`
  - Merge output degradation counters and upgraded run status into `RunSummary`.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\sidecar\status_panel.cpp`
  - Render output degradation counters and final run-state classification.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\tests\unit\test_cpi_output_pipeline.cpp`
  - Lock in zero-blocking drop behavior and immediate slot reuse.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\tests\integration\test_slow_consumer_pressure.cpp`
  - Prove a slow consumer degrades output without stalling the receive loop.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\tests\unit\test_owner_loop_summary.cpp`
  - Verify summary/status-panel output for `degraded` and `error`.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\README.md`
  - Document the realtime-first output boundary.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\docs\当前接收端代码实现与执行逻辑详解.md`
  - Describe zero-blocking finalize/output behavior and degradation semantics.

## Task 1: Add Output Policy Configuration

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\include\rxtech\rx_config.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\runtime\rx_config.cpp`
- Test: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\tests\unit\test_rx_config.cpp`

- [ ] **Step 1: Write the failing config test**

Add a new assertion block in `test_rx_config.cpp` that locks in the intended keys and defaults:

```cpp
rxtech::RxConfig cfg = rxtech::load_default_config();
assert(cfg.output_drop_policy == "degrade");
assert(cfg.output_ring_capacity == 32U);
assert(cfg.recycle_ring_capacity == 32U);

const rxtech::RxConfig loaded = rxtech::load_config_file("tests/data/output_policy.conf");
assert(loaded.output_drop_policy == "error");
assert(loaded.output_ring_capacity == 64U);
assert(loaded.recycle_ring_capacity == 128U);
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R "test_rx_config|test_merge_config"
```

Expected: compile failure or assertion failure because the output policy keys do not exist yet.

- [ ] **Step 3: Add the minimal config fields and parsing**

Implement the smallest possible config surface:

```cpp
struct RxConfig {
    std::string output_drop_policy = "degrade";
    std::uint32_t output_ring_capacity = 32U;
    std::uint32_t recycle_ring_capacity = 32U;
};
```

Parse and merge:

```cpp
{"output_drop_policy", "runtime.output_drop_policy"}
{"output_ring_capacity", "runtime.output_ring_capacity"}
{"recycle_ring_capacity", "runtime.recycle_ring_capacity"}
```

Reject unsupported `output_drop_policy` values by normalizing them back to `degrade` or `error`.

- [ ] **Step 4: Run the targeted test to verify it passes**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R "test_rx_config|test_merge_config"
```

Expected: config tests pass and the new keys are stable.

- [ ] **Step 5: Commit the config contract**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening"
git add include/rxtech/rx_config.h src/receiver/runtime/rx_config.cpp tests/unit/test_rx_config.cpp
git commit -m "feat: add realtime-first output policy config"
```

## Task 2: Lock In Non-Blocking Drop Behavior In The Output Pipeline

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\core\internal\cpi_state_coordinator.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\core\cpi_state_coordinator.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\tests\unit\test_cpi_output_pipeline.cpp`

- [ ] **Step 1: Write the failing zero-blocking drop test**

Extend `test_cpi_output_pipeline.cpp` with a case where `output_ring` is intentionally full before finalize:

```cpp
rxtech::SpscRing<rxtech::CpiOutput> output_ring(2U);
rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(2U);
// Fill all usable output slots first.
assert(output_ring.push(dummy1));

coordinator.attach_rings(&output_ring, &recycle_ring);
coordinator.configure_output_policy("degrade");

const auto before = metrics.finalize("socket", "", "", 0).output_backpressure_count;
coordinator.finalize_active_for_shutdown(metrics);

const auto after = metrics.finalize("socket", "", "", 0);
assert(after.output_backpressure_count == before + 1U);
assert(pool.acquire(99U) != rxtech::kInvalidPoolIndex);
```

This test should prove:

- finalize does not wait for ring space
- the slot is released immediately on drop
- the drop is recorded

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_cpi_output_pipeline
```

Expected: failure because the current coordinator behavior is not exposed as an explicit output policy contract.

- [ ] **Step 3: Implement the minimal non-blocking finalize policy**

Add a tiny policy surface to the coordinator:

```cpp
void configure_output_policy(std::string policy);
bool output_drop_is_error() const;
```

Keep the finalize path one-shot:

```cpp
if (!output_ring_->push(out)) {
    metrics.on_output_backpressure();
    ctx_pool_.release(active_ctx_index_);
    mark_output_degraded();
}
```

Do not add:

- retry loops
- timed waits
- `std::this_thread::sleep_for`

- [ ] **Step 4: Run the targeted test to verify it passes**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R "test_cpi_output_pipeline|test_spsc_ring"
```

Expected: output pipeline tests pass and the ring semantics remain intact.

- [ ] **Step 5: Commit the coordinator hardening**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening"
git add src/receiver/core/internal/cpi_state_coordinator.h src/receiver/core/cpi_state_coordinator.cpp tests/unit/test_cpi_output_pipeline.cpp
git commit -m "fix: make finalized output drop non-blocking"
```

## Task 3: Thread Output Policy And Ring Capacity Through OwnerLoop

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\core\owner_loop.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\tests\integration\test_slow_consumer_pressure.cpp`

- [ ] **Step 1: Write the failing slow-consumer integration expectation**

Extend `test_slow_consumer_pressure.cpp` so it expects the loop to complete even if output drops occur:

```cpp
assert(std::chrono::steady_clock::now() < deadline);
assert(!decisions.empty());
assert(summary.run_status == "degraded" || summary.run_status == "error");
assert(summary.output_backpressure_count > 0U);
```

If `OwnerLoop::run()` currently does not expose enough summary information, fail the test first and then wire the result through.

- [ ] **Step 2: Run the targeted integration test to verify it fails**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/integration
ctest --output-on-failure -R test_slow_consumer_pressure
```

Expected: failure because the output policy and final run-state are not yet threaded through the owner loop and summary.

- [ ] **Step 3: Make OwnerLoop use config-driven ring capacities and policy**

Replace the fixed constant:

```cpp
const std::size_t output_capacity = std::max<std::size_t>(2U, context.config.output_ring_capacity);
const std::size_t recycle_capacity = std::max<std::size_t>(2U, context.config.recycle_ring_capacity);
SpscRing<CpiOutput> output_ring(output_capacity);
SpscRing<ReleaseToken> recycle_ring(recycle_capacity);
cpi_state_coordinator.configure_output_policy(context.config.output_drop_policy);
```

The loop must still remain zero-wait. The only change is that it now uses config values and carries the policy into runtime state.

- [ ] **Step 4: Run the targeted integration test to verify it passes**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/integration
ctest --output-on-failure -R test_slow_consumer_pressure
```

Expected: the loop completes under a slow consumer without any sign of owner-thread waiting.

- [ ] **Step 5: Commit the owner-loop wiring**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening"
git add src/receiver/core/owner_loop.cpp tests/integration/test_slow_consumer_pressure.cpp
git commit -m "feat: wire realtime-first output policy through owner loop"
```

## Task 4: Expose Degradation In Metrics, Summary, And Status Panel

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\include\rxtech\metrics.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\core\owner_loop_summary.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\src\receiver\sidecar\status_panel.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\tests\unit\test_owner_loop_summary.cpp`

- [ ] **Step 1: Write the failing summary/state tests**

Extend `test_owner_loop_summary.cpp` to lock in the new visibility:

```cpp
summary.run_status = "degraded";
summary.output_backpressure_count = 7U;

const std::string human = rxtech::build_run_human_summary(summary);
assert(human.find("运行结果： 退化") != std::string::npos);
assert(human.find("输出退化次数： 7") != std::string::npos);

const auto lines = rxtech::build_status_snapshot_lines_for_test(summary, std::chrono::seconds(2));
assert(any_line_contains(lines, "输出退化"));
assert(any_line_contains(lines, "7"));
```

Add a second assertion block for `run_status = "error"` with the same counters.

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R "test_owner_loop_summary|test_metrics"
```

Expected: failure because the summary does not yet classify degraded/error runs or render output degradation counters.

- [ ] **Step 3: Implement the minimal observability surface**

Extend the metrics and summary structures with explicit output degradation fields, then render them:

```cpp
std::uint64_t finalized_outputs = 0;
std::uint64_t queued_outputs = 0;
std::uint64_t dropped_outputs = 0;
std::uint64_t output_ring_full_events = 0;
bool run_degraded = false;
```

Keep the rendering clear:

- `运行结果： 成功 / 退化 / 失败`
- `输出退化次数`
- `输出环满次数`

- [ ] **Step 4: Run the targeted tests to verify they pass**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R "test_owner_loop_summary|test_metrics"
```

Expected: summary and metrics tests pass with the new degraded/error semantics.

- [ ] **Step 5: Commit the observability update**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening"
git add include/rxtech/metrics.h src/receiver/core/owner_loop_summary.cpp src/receiver/sidecar/status_panel.cpp tests/unit/test_owner_loop_summary.cpp
git commit -m "feat: expose realtime output degradation in summary"
```

## Task 5: Update Runtime Documentation And Run Linux Validation

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\README.md`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening\docs\当前接收端代码实现与执行逻辑详解.md`

- [ ] **Step 1: Write the failing documentation checklist**

Before editing docs, reject the following stale statements in task notes:

- "slow consumer should backpressure the owner thread"
- "output reliability takes priority over receive realtime behavior"
- any wording that implies finalize will wait for ring space

- [ ] **Step 2: Update the docs to the realtime-first policy**

Document these facts:

- the receive hot path is zero-blocking with respect to output
- finalized CPI output may be dropped when the async path is congested
- drops are observable and reflected in run status
- `degrade` vs `error` only changes the final conclusion, not hot-path behavior

- [ ] **Step 3: Run the full unit suite on the Linux server**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure
```

Expected: all unit tests pass, or any unrelated pre-existing failure is recorded explicitly.

- [ ] **Step 4: Run the slow-consumer integration validation on the Linux server**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/integration
ctest --output-on-failure -R "test_slow_consumer_pressure|test_receive_runner_fake"
```

What it verifies: the output path can degrade under pressure without stalling the owner loop and the final summary semantics remain consistent.

- [ ] **Step 5: Commit the docs and validation notes**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-phase4-output-hardening"
git add README.md docs/当前接收端代码实现与执行逻辑详解.md
git commit -m "docs: describe realtime-first output degradation policy"
```

## Notes For The Implementer

- Do not introduce any timed wait, bounded wait, or retry loop into the finalize path. That would violate the phase goal.
- The presence of `degraded` or `error` status must never change the zero-blocking hot-path behavior.
- Releasing the pool slot immediately on output drop is mandatory; otherwise the system becomes “fake realtime” and still dies under pressure.
- Linux server validation is authoritative. Windows edits and static inspection are not enough to claim completion.
