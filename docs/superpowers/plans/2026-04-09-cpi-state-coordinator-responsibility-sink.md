# CpiStateCoordinator Responsibility Sink Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move CPI lifecycle business decisions deeper into `CpiStateCoordinator` so `OwnerLoop` stays a thin flow driver while control/data hot paths remain direct function calls.

**Architecture:** Keep `OwnerLoop` on two direct entrypoints: `process_control_packet(...)` and `process_data_packet(...)`. Refactor `CpiStateCoordinator` internals so snapshot staging, active/previous window switching, late-packet handling, and finalize decisions are centralized behind private helpers without introducing an event bus, queue, thread, or heap allocation.

**Tech Stack:** C++17, CMake 3.16.x, Ninja, existing `rx_receiver_core` unit test harness, Linux server validation via `ssh kds`

---

## File Map

- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\internal\cpi_state_coordinator.h`
  - Narrow the public API to coordinator-owned dependencies only.
  - Rename staged control state to reflect its actual role.
  - Declare private helpers for routing, switching, previous-window late handling, and finalize decisions.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\cpi_state_coordinator.cpp`
  - Move CPI business decisions out of the monolithic data path into focused helpers.
  - Deduplicate active/previous finalize logic.
  - Keep control/data entrypoints flat.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\owner_loop.cpp`
  - Update the coordinator call site after API cleanup.
  - Keep `OwnerLoop` limited to packet classification, flow driving, and side effects outside CPI business state.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_dynamic_prt.cpp`
  - Add regression coverage for previous-window control merge, window switch ordering, and finalize staying coordinator-internal.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\docs\当前接收端代码实现与执行逻辑详解.md`
  - Refresh the coordinator description so it explicitly names `CpiStateCoordinator` as the CPI business hub.

### Task 1: Lock In Coordinator Semantics With Failing Tests

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_dynamic_prt.cpp`

- [ ] **Step 1: Write the failing test for previous-window control merge**

Add a test in `test_dynamic_prt.cpp` that:

```cpp
// CPI 1 opens from data, CPI 2 causes switch, late control for CPI 1 arrives while CPI 1 is previous.
coord.process_data_packet(make_data(1U, 1U, 0U, 1U), make_interpreted_data(1U, 1U, 0U, 1U), metrics, status, error);
coord.process_data_packet(make_data(2U, 1U, 0U, 1U), make_interpreted_data(2U, 1U, 0U, 1U), metrics, status, error);
coord.process_control_packet(make_control(1U, 30U));
const auto output = finalize_via_switch(coord, output_ring, metrics, status, error, 3U, 4U);
assert(output.cpi_id == 1U);
assert(output.control.bind_source == rxtech::BindSource::control);
assert(output.control.n_prt == 30U);
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_dynamic_prt
```

Expected: `test_dynamic_prt` fails on the new previous-window control assertion, proving the test exercises the intended coordinator path.

- [ ] **Step 3: Write the failing test for switch ordering and late packet routing**

Add a second test that:

```cpp
// CPI 1 -> CPI 2 -> CPI 3; old previous must be finalized before CPI 2 becomes previous.
coord.process_data_packet(make_data(1U, 1U, 0U, 1U), make_interpreted_data(1U, 1U, 0U, 1U), metrics, status, error);
coord.process_data_packet(make_data(2U, 1U, 0U, 1U), make_interpreted_data(2U, 1U, 0U, 1U), metrics, status, error);
coord.process_data_packet(make_data(3U, 1U, 0U, 1U), make_interpreted_data(3U, 1U, 0U, 1U), metrics, status, error);
auto late_for_cpi2 = coord.process_data_packet(make_data(2U, 1U, 1U, 1U), make_interpreted_data(2U, 1U, 1U, 1U), metrics, status, error);
assert(late_for_cpi2.accepted);
auto too_late_for_cpi1 = coord.process_data_packet(make_data(1U, 1U, 1U, 1U), make_interpreted_data(1U, 1U, 1U, 1U), metrics, status, error);
assert(!too_late_for_cpi1.accepted);
```

- [ ] **Step 4: Re-run the targeted test to verify both new checks fail for the right reason**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_dynamic_prt
```

Expected: failure is still inside `test_dynamic_prt`, now covering both previous-window control merge and window-switch routing expectations.

- [ ] **Step 5: Commit the red tests**

Execution location: local Windows workspace `D:\WorkSpace\Company\Tower\rx_tech_demo`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo"
git add tests/unit/test_dynamic_prt.cpp
git commit -m "test: lock coordinator cpi window semantics"
```

### Task 2: Sink CPI Business Decisions Into Coordinator Helpers

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\internal\cpi_state_coordinator.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\cpi_state_coordinator.cpp`

- [ ] **Step 1: Add private helper declarations before changing behavior**

Update `cpi_state_coordinator.h` to declare focused helpers such as:

```cpp
enum class DataRoute
{
    write_active,
    write_previous,
    reject,
};

void stage_or_merge_control_snapshot(const ControlSnapshot &candidate);
void merge_snapshot_into_context(CpiContext &ctx, const ControlSnapshot &candidate);
bool ensure_active_for_packet(std::uint16_t cpi_id,
                              IMetricsCollector &metrics,
                              std::string &run_status,
                              std::string &run_error);
bool switch_active_window(std::uint16_t next_cpi_id,
                          IMetricsCollector &metrics,
                          std::string &run_status,
                          std::string &run_error);
DataRoute resolve_data_route(const ParsedPacketView &parsed,
                             const InterpretedPacketView &packet,
                             IMetricsCollector &metrics,
                             std::string &run_status,
                             std::string &run_error);
bool write_active_packet(const ParsedPacketView &parsed,
                         const InterpretedPacketView &packet,
                         IMetricsCollector &metrics);
bool write_previous_packet(const ParsedPacketView &parsed,
                           const InterpretedPacketView &packet,
                           IMetricsCollector &metrics);
void finalize_if_needed(CpiContext &ctx,
                        bool active_window,
                        IMetricsCollector &metrics);
void finalize_window(bool active_window,
                     std::uint32_t trigger,
                     IMetricsCollector &metrics);
```

Also rename `current_control_` to `staged_control_` everywhere in the class.

- [ ] **Step 2: Run the targeted test to confirm declaration-only edits do not make red tests pass accidentally**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_dynamic_prt
```

Expected: build succeeds and the new tests still fail, proving behavior is not yet changed.

- [ ] **Step 3: Implement minimal coordinator routing and finalize helpers**

Refactor `cpi_state_coordinator.cpp` so:

```cpp
CpiProcessResult CpiStateCoordinator::process_data_packet(... )
{
    CpiProcessResult result;
    const DataRoute route = resolve_data_route(parsed, packet, metrics, run_status, run_error);
    switch (route)
    {
    case DataRoute::write_active:
        result.accepted = write_active_packet(parsed, packet, metrics);
        break;
    case DataRoute::write_previous:
        result.accepted = write_previous_packet(parsed, packet, metrics);
        break;
    case DataRoute::reject:
        break;
    }
    return result;
}
```

Implementation rules:
- `resolve_data_route(...)` owns admission, CPI switch sequencing, and rejection metrics.
- `write_active_packet(...)` owns slot write, progress advance, duplicate/reject handling, and `finalize_if_needed(...)`.
- `write_previous_packet(...)` owns late packet acceptance/rejection metrics.
- `finalize_window(...)` deduplicates active/previous finalize mechanics without introducing a new abstraction layer.

- [ ] **Step 4: Run the targeted test to verify the new helper structure turns the red tests green**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_dynamic_prt
```

Expected: `test_dynamic_prt` passes, proving coordinator-internal routing still preserves previous-window, switch, and finalize behavior.

- [ ] **Step 5: Commit the coordinator refactor**

Execution location: local Windows workspace `D:\WorkSpace\Company\Tower\rx_tech_demo`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo"
git add src/receiver/core/internal/cpi_state_coordinator.h src/receiver/core/cpi_state_coordinator.cpp
git commit -m "refactor: centralize cpi business decisions in coordinator"
```

### Task 3: Thin The OwnerLoop-Facing API Without Adding A New Layer

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\owner_loop.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\internal\cpi_state_coordinator.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_dynamic_prt.cpp`

- [ ] **Step 1: Remove redundant `ProtocolSpec` plumbing from `process_data_packet(...)`**

Change the public signature to:

```cpp
CpiProcessResult process_data_packet(const ParsedPacketView &parsed,
                                     const InterpretedPacketView &packet,
                                     IMetricsCollector &metrics,
                                     std::string &run_status,
                                     std::string &run_error);
```

Update all call sites and tests to rely on coordinator-owned `spec_`.

- [ ] **Step 2: Run the targeted test to verify the API cleanup compiles and keeps behavior green**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_dynamic_prt
```

Expected: `test_dynamic_prt` remains green after the API cleanup.

- [ ] **Step 3: Keep `OwnerLoop` limited to direct dispatch**

Ensure `owner_loop.cpp` still does only:

```cpp
if (processed.interpreted.kind == PacketKind::control_table)
{
    cpi_state_coordinator.process_control_packet(processed.parsed);
}

if (processed.interpreted.kind == PacketKind::data_packet)
{
    data_order_tracker.observe(processed.interpreted);
    const CpiProcessResult cpi_result = cpi_state_coordinator.process_data_packet(
        processed.parsed,
        processed.interpreted,
        *context.metrics,
        runtime_state.run_status,
        runtime_state.run_error);
    if (!cpi_result.accepted)
    {
        return;
    }
    runtime_state.record_data_packet(processed.parsed, processed.interpreted, spec);
}
```

No event object, bus, queue, or extra dynamic dispatch is allowed.

- [ ] **Step 4: Commit the API cleanup**

Execution location: local Windows workspace `D:\WorkSpace\Company\Tower\rx_tech_demo`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo"
git add src/receiver/core/owner_loop.cpp src/receiver/core/internal/cpi_state_coordinator.h tests/unit/test_dynamic_prt.cpp
git commit -m "refactor: keep owner loop thin on direct cpi paths"
```

### Task 4: Refresh Docs And Run Final Verification

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\docs\当前接收端代码实现与执行逻辑详解.md`

- [ ] **Step 1: Update the coordinator doc section**

Refresh the wording so the document says:

```md
- `OwnerLoop` only classifies packets and drives the flow.
- `CpiStateCoordinator` is the sole CPI business hub for:
  - control snapshot staging/merge
  - active/previous window switching
  - late packet routing
  - finalize decisions (`switch`, `full_ready`, `wave_end`, `timeout`, `shutdown`)
```

- [ ] **Step 2: Run the authoritative unit suite**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure
```

Expected: unit tests pass. This is the authoritative validation for this refactor; do not use Windows build or test output as completion evidence.

- [ ] **Step 3: Optional runtime smoke check if the user asks for runtime confirmation**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1
```

What it verifies: the socket ingress still runs through the unchanged `OwnerLoop -> PacketPipeline -> CpiStateCoordinator` hot path after the coordinator refactor. Skip unless the user explicitly asks for runtime validation.

- [ ] **Step 4: Commit the docs sync**

Execution location: local Windows workspace `D:\WorkSpace\Company\Tower\rx_tech_demo`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo"
git add docs/当前接收端代码实现与执行逻辑详解.md
git commit -m "docs: clarify coordinator as cpi business hub"
```
