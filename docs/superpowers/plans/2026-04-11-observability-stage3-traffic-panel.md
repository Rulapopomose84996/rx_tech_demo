# Observability Stage 3 Traffic State And Status Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the Stage 3 traffic interruption state machine and convert the status panel from cumulative "曾经检测到流量" semantics to current-state traffic semantics with interruption and recovery timestamps.

**Architecture:** Keep Stage 3 bounded to traffic-state and panel semantics. Introduce a dedicated `TrafficStateTracker` inside `src/receiver/sidecar`, feed it from the owner loop when valid protocol traffic arrives, emit `traffic.first_seen` / `traffic.interrupted` / `traffic.resumed` through the existing Stage 2 event path, and expose a compact `TrafficFlowSummary` on `RunSummary` so the status panel can render current state without inferring from cumulative counters. Do not change debug artifact policy or release summary renderer scope beyond the fields needed to surface the traffic state.

**Tech Stack:** C++17, CMake 3.16, Ninja, existing `rx_receiver_core` unit/integration harness, WSL fallback validation, later Linux server validation via `ssh kds`

---

## File Map

### New Files

- `src/receiver/sidecar/internal/traffic_state_tracker.h`
  - Defines `TrafficState`, `TrafficStateSnapshot`, and the tracker interface.
- `src/receiver/sidecar/traffic_state_tracker.cpp`
  - Implements timeout-based state transitions and event payload helpers.
- `tests/unit/test_traffic_state_tracker.cpp`
  - Verifies `idle -> active -> interrupted -> active` transitions and timestamp retention.

### Modified Files

- `include/rxtech/metrics.h`
  - Adds `TrafficFlowSummary` to `RunSummary`.
- `src/receiver/CMakeLists.txt`
  - Adds `traffic_state_tracker.cpp` to `rx_receiver_core`.
- `tests/unit/CMakeLists.txt`
  - Adds `test_traffic_state_tracker`.
- `src/receiver/core/owner_loop.cpp`
  - Feeds valid protocol traffic timestamps into the tracker and passes tracker state into the reporter.
- `src/receiver/sidecar/internal/runtime_status_reporter.h`
  - Accepts the tracker in periodic/final summary paths.
- `src/receiver/sidecar/runtime_status_reporter.cpp`
  - Emits traffic events and copies `TrafficFlowSummary` into `RunSummary`.
- `src/receiver/sidecar/internal/status_panel.h`
  - Keeps the same render interface, but tests will now assert Stage 3 current-state text.
- `src/receiver/sidecar/status_panel.cpp`
  - Replaces "链路判定" cumulative wording with current traffic state block and timestamp lines.
- `tests/unit/test_owner_loop_summary.cpp`
  - Updates panel expectations to Stage 3 semantics.
- `tests/integration/test_receive_runner_fake.cpp`
  - Extends fake runner coverage to include interrupted/recovered panel content in a non-authoritative local harness.

### Existing Files To Read Before Editing

- `src/receiver/sidecar/status_panel.cpp`
- `src/receiver/core/internal/owner_loop_runtime_state.h`
- `src/receiver/core/owner_loop.cpp`
- `src/receiver/sidecar/runtime_status_reporter.cpp`
- `include/rxtech/metrics.h`
- `tests/unit/test_owner_loop_summary.cpp`
- `tests/integration/test_receive_runner_fake.cpp`

---

### Task 1: Write Failing Tests For The Traffic State Machine

**Files:**
- Create: `tests/unit/test_traffic_state_tracker.cpp`
- Modify: `tests/unit/CMakeLists.txt`
- Modify: `src/receiver/CMakeLists.txt`

- [ ] **Step 1: Add the failing tracker test**

Create `tests/unit/test_traffic_state_tracker.cpp`:

```cpp
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>

#include "traffic_state_tracker.h"

int main()
{
    rxtech::TrafficStateTracker tracker(3000U);

    const auto initial = tracker.snapshot();
    assert(initial.state == rxtech::TrafficState::idle);
    assert(!initial.first_seen_wall.has_value());

    tracker.observe_valid_business_packet(1000U, "2026-04-12 00:00:01");
    const auto active = tracker.snapshot();
    assert(active.state == rxtech::TrafficState::active);
    assert(active.first_seen_wall.has_value());
    assert(active.last_seen_wall.has_value());

    const auto interrupted = tracker.observe_timeout(5001U, "2026-04-12 00:00:05");
    assert(interrupted.has_value());
    assert(interrupted->transition == rxtech::TrafficTransition::interrupted);
    assert(tracker.snapshot().state == rxtech::TrafficState::interrupted);

    const auto resumed = tracker.observe_valid_business_packet(6000U, "2026-04-12 00:00:06");
    assert(resumed.has_value());
    assert(resumed->transition == rxtech::TrafficTransition::resumed);
    assert(tracker.snapshot().state == rxtech::TrafficState::active);
    assert(tracker.snapshot().last_interrupted_wall == "2026-04-12 00:00:05");
    assert(tracker.snapshot().last_resumed_wall == "2026-04-12 00:00:06");
    return 0;
}
```

Update `tests/unit/CMakeLists.txt`:

```cmake
add_executable(test_traffic_state_tracker test_traffic_state_tracker.cpp)
target_link_libraries(test_traffic_state_tracker PRIVATE rx_receiver_core)
target_include_directories(test_traffic_state_tracker PRIVATE ${CMAKE_SOURCE_DIR}/src/receiver/sidecar/internal)
add_test(NAME test_traffic_state_tracker COMMAND test_traffic_state_tracker)
set_tests_properties(test_traffic_state_tracker PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
rxtech_apply_warnings(test_traffic_state_tracker)
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_traffic_state_tracker
```

Expected: build failure because `traffic_state_tracker.h` and its implementation do not exist yet.

- [ ] **Step 3: Wire the new source target**

Update `src/receiver/CMakeLists.txt`:

```cmake
    sidecar/traffic_state_tracker.cpp
```

- [ ] **Step 4: Move on without committing**

No commit in the RED phase. Continue directly to Task 2 after verifying the failure reason.

---

### Task 2: Implement `TrafficStateTracker` And `TrafficFlowSummary`

**Files:**
- Create: `src/receiver/sidecar/internal/traffic_state_tracker.h`
- Create: `src/receiver/sidecar/traffic_state_tracker.cpp`
- Modify: `include/rxtech/metrics.h`

- [ ] **Step 1: Add the Stage 3 types**

Create `src/receiver/sidecar/internal/traffic_state_tracker.h`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rxtech
{
    enum class TrafficState
    {
        idle,
        active,
        interrupted,
    };

    enum class TrafficTransition
    {
        first_seen,
        interrupted,
        resumed,
    };

    struct TrafficTransitionEvent
    {
        TrafficTransition transition;
        std::string wall_time;
        std::uint64_t monotonic_ns = 0U;
    };

    struct TrafficStateSnapshot
    {
        TrafficState state = TrafficState::idle;
        std::optional<std::string> first_seen_wall;
        std::optional<std::string> last_seen_wall;
        std::optional<std::string> last_interrupted_wall;
        std::optional<std::string> last_resumed_wall;
        std::uint64_t last_seen_monotonic_ns = 0U;
    };

    class TrafficStateTracker
    {
      public:
        explicit TrafficStateTracker(std::uint32_t interrupt_timeout_ms);
        std::optional<TrafficTransitionEvent> observe_valid_business_packet(std::uint64_t monotonic_ns,
                                                                            const std::string& wall_time);
        std::optional<TrafficTransitionEvent> observe_timeout(std::uint64_t monotonic_ns,
                                                              const std::string& wall_time);
        TrafficStateSnapshot snapshot() const noexcept;

      private:
        std::uint64_t interrupt_timeout_ns_ = 0U;
        TrafficStateSnapshot snapshot_{};
    };
}
```

Extend `include/rxtech/metrics.h` with:

```cpp
    struct TrafficFlowSummary
    {
        std::string state = "idle";
        std::string first_seen_wall;
        std::string last_seen_wall;
        std::string last_interrupted_wall;
        std::string last_resumed_wall;
    };
```

And add it to `RunSummary`:

```cpp
        TrafficFlowSummary traffic_flow;
```

- [ ] **Step 2: Implement the minimal tracker**

Create `src/receiver/sidecar/traffic_state_tracker.cpp`:

```cpp
#include "internal/traffic_state_tracker.h"

namespace rxtech
{
    TrafficStateTracker::TrafficStateTracker(std::uint32_t interrupt_timeout_ms)
        : interrupt_timeout_ns_(static_cast<std::uint64_t>(interrupt_timeout_ms) * 1000ULL * 1000ULL) {}

    std::optional<TrafficTransitionEvent>
    TrafficStateTracker::observe_valid_business_packet(std::uint64_t monotonic_ns, const std::string& wall_time)
    {
        snapshot_.last_seen_monotonic_ns = monotonic_ns;
        snapshot_.last_seen_wall = wall_time;

        if (!snapshot_.first_seen_wall.has_value())
        {
            snapshot_.first_seen_wall = wall_time;
            snapshot_.state = TrafficState::active;
            return TrafficTransitionEvent{TrafficTransition::first_seen, wall_time, monotonic_ns};
        }

        if (snapshot_.state == TrafficState::interrupted)
        {
            snapshot_.state = TrafficState::active;
            snapshot_.last_resumed_wall = wall_time;
            return TrafficTransitionEvent{TrafficTransition::resumed, wall_time, monotonic_ns};
        }

        snapshot_.state = TrafficState::active;
        return std::nullopt;
    }

    std::optional<TrafficTransitionEvent>
    TrafficStateTracker::observe_timeout(std::uint64_t monotonic_ns, const std::string& wall_time)
    {
        if (snapshot_.state != TrafficState::active)
        {
            return std::nullopt;
        }
        if (snapshot_.last_seen_monotonic_ns == 0U || monotonic_ns <= snapshot_.last_seen_monotonic_ns)
        {
            return std::nullopt;
        }
        if (interrupt_timeout_ns_ == 0U || monotonic_ns - snapshot_.last_seen_monotonic_ns < interrupt_timeout_ns_)
        {
            return std::nullopt;
        }

        snapshot_.state = TrafficState::interrupted;
        snapshot_.last_interrupted_wall = wall_time;
        return TrafficTransitionEvent{TrafficTransition::interrupted, wall_time, monotonic_ns};
    }

    TrafficStateSnapshot TrafficStateTracker::snapshot() const noexcept
    {
        return snapshot_;
    }
}
```

- [ ] **Step 3: Run the targeted test**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_traffic_state_tracker
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_traffic_state_tracker
```

Expected: `test_traffic_state_tracker` passes.

- [ ] **Step 4: Commit**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add include/rxtech/metrics.h src/receiver/CMakeLists.txt src/receiver/sidecar/internal/traffic_state_tracker.h src/receiver/sidecar/traffic_state_tracker.cpp tests/unit/CMakeLists.txt tests/unit/test_traffic_state_tracker.cpp
git commit -m "feat: add stage3 traffic state tracker"
```

---

### Task 3: Feed Traffic Events From The Owner Loop And Runtime Reporter

**Files:**
- Modify: `src/receiver/core/owner_loop.cpp`
- Modify: `src/receiver/sidecar/internal/runtime_status_reporter.h`
- Modify: `src/receiver/sidecar/runtime_status_reporter.cpp`
- Modify: `tests/unit/test_event_logger.cpp`

- [ ] **Step 1: Add a failing event-shape test for traffic transitions**

Append to `tests/unit/test_event_logger.cpp`:

```cpp
    {
        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.structured_log_output = "file";
        config.operations.structured_log_file_path = "test_traffic_transition.jsonl";
        std::remove(config.operations.structured_log_file_path.c_str());

        rxtech::configure_structured_logger(config);
        rxtech::structured_log(rxtech::StructuredLogLevel::warn, "traffic.interrupted",
                               {{"backend", "socket"},
                                {"last_valid_business_packet_wall", "2026-04-12 00:00:04"},
                                {"interrupt_timeout_ms", 3000},
                                {"current_state", "interrupted"}});
        rxtech::shutdown_structured_logger();

        std::ifstream input(config.operations.structured_log_file_path);
        std::string line;
        std::getline(input, line);
        const nlohmann::json parsed = nlohmann::json::parse(line);
        assert(parsed.at("event") == "traffic.interrupted");
        assert(parsed.at("payload").at("current_state") == "interrupted");
        std::remove(config.operations.structured_log_file_path.c_str());
    }
```

- [ ] **Step 2: Run the test to verify it is still green before integration**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_event_logger
```

Expected: still green, confirming the compatibility path can carry the new event names.

- [ ] **Step 3: Wire the tracker into `OwnerLoop` and `RuntimeStatusReporter`**

In `src/receiver/sidecar/internal/runtime_status_reporter.h`, add the tracker include and update signatures:

```cpp
#include "traffic_state_tracker.h"
```

Change methods:

```cpp
        void emit_periodic(ReceiveContext &context, CaptureArtifacts &artifacts,
                           const OwnerLoopRuntimeState &runtime_state, const DataOrderTracker &data_order_tracker,
                           const TrafficStateTracker &traffic_tracker,
                           const std::chrono::steady_clock::time_point &now);

        RunSummary build_final_summary(ReceiveContext &context, CaptureArtifacts &artifacts,
                                       const OwnerLoopRuntimeState &runtime_state,
                                       const DataOrderTracker &data_order_tracker,
                                       const TrafficStateTracker &traffic_tracker,
                                       const std::chrono::steady_clock::time_point &end_time) const;
```

In `src/receiver/sidecar/runtime_status_reporter.cpp`, add:

```cpp
    namespace
    {
        std::string format_traffic_state(TrafficState state)
        {
            switch (state)
            {
            case TrafficState::idle: return "idle";
            case TrafficState::active: return "active";
            case TrafficState::interrupted: return "interrupted";
            }
            return "idle";
        }
    }
```

Inside `build_summary(...)`, after `metrics_exporter_.populate_summary(summary);`, populate:

```cpp
        const TrafficStateSnapshot traffic_snapshot = traffic_tracker.snapshot();
        summary.traffic_flow.state = format_traffic_state(traffic_snapshot.state);
        summary.traffic_flow.first_seen_wall = traffic_snapshot.first_seen_wall.value_or(std::string{});
        summary.traffic_flow.last_seen_wall = traffic_snapshot.last_seen_wall.value_or(std::string{});
        summary.traffic_flow.last_interrupted_wall = traffic_snapshot.last_interrupted_wall.value_or(std::string{});
        summary.traffic_flow.last_resumed_wall = traffic_snapshot.last_resumed_wall.value_or(std::string{});
```

In `src/receiver/core/owner_loop.cpp`, create the tracker near `RuntimeStatusReporter status_reporter(...)`:

```cpp
        TrafficStateTracker traffic_tracker(3000U);
```

Inside the packet callback, after `runtime_state.record_protocol_packet(processed.interpreted);`, add:

```cpp
                        if (processed.interpreted.valid)
                        {
                            if (const auto transition =
                                    traffic_tracker.observe_valid_business_packet(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                                                     std::chrono::steady_clock::now().time_since_epoch())
                                                                                     .count(),
                                                                                 ""))
                            {
                                switch (transition->transition)
                                {
                                case TrafficTransition::first_seen:
                                    structured_log(StructuredLogLevel::info, "traffic.first_seen",
                                                   {{"backend", context.backend->name()},
                                                    {"current_state", "active"}});
                                    break;
                                case TrafficTransition::resumed:
                                    structured_log(StructuredLogLevel::info, "traffic.resumed",
                                                   {{"backend", context.backend->name()},
                                                    {"current_state", "active"}});
                                    break;
                                case TrafficTransition::interrupted:
                                    break;
                                }
                            }
                        }
```

Before calling `status_reporter.emit_periodic(...)`, add:

```cpp
            if (const auto transition = traffic_tracker.observe_timeout(steady_clock_now_ns(), ""))
            {
                structured_log(StructuredLogLevel::warn, "traffic.interrupted",
                               {{"backend", context.backend->name()},
                                {"interrupt_timeout_ms", 3000},
                                {"current_state", "interrupted"}});
            }
```

Update reporter calls so both periodic and final summary receive `traffic_tracker`.

- [ ] **Step 4: Run the targeted unit gate**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger test_traffic_state_tracker
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R "test_event_logger|test_traffic_state_tracker"
```

Expected: both tests pass.

- [ ] **Step 5: Commit**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add src/receiver/core/owner_loop.cpp src/receiver/sidecar/internal/runtime_status_reporter.h src/receiver/sidecar/runtime_status_reporter.cpp tests/unit/test_event_logger.cpp
git commit -m "feat: emit stage3 traffic transition events"
```

---

### Task 4: Rebuild The Status Panel Around Current Traffic State

**Files:**
- Modify: `src/receiver/sidecar/status_panel.cpp`
- Modify: `tests/unit/test_owner_loop_summary.cpp`
- Modify: `tests/integration/test_receive_runner_fake.cpp`

- [ ] **Step 1: Update the unit expectations first**

In `tests/unit/test_owner_loop_summary.cpp`, replace the old assertions:

```cpp
        if (line.find("链路判定") != std::string::npos && line.find("已检测到业务协议流量") != std::string::npos)
```

with Stage 3 expectations:

```cpp
        if (line.find("业务流状态") != std::string::npos && line.find("正常") != std::string::npos)
```

For the pre-business summary, replace:

```cpp
        if (line.find("链路判定") != std::string::npos && line.find("尚未检测到业务协议流量") != std::string::npos)
```

with:

```cpp
        if (line.find("业务流状态") != std::string::npos && line.find("未检测到") != std::string::npos)
```

Also seed the traffic summary at the top of the test:

```cpp
    summary.traffic_flow.state = "active";
    summary.traffic_flow.first_seen_wall = "2026-04-12 00:00:01";
    summary.traffic_flow.last_seen_wall = "2026-04-12 00:00:04";
```

- [ ] **Step 2: Run the summary test to verify it fails**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_owner_loop_summary
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_owner_loop_summary
```

Expected: test fails because the panel still renders the old `链路判定` lines.

- [ ] **Step 3: Rewrite the panel to current-state blocks**

In `src/receiver/sidecar/status_panel.cpp`, replace `describe_link_state(...)` with:

```cpp
        std::string describe_traffic_state(const RunSummary &summary)
        {
            if (summary.traffic_flow.state == "active")
            {
                return "正常";
            }
            if (summary.traffic_flow.state == "interrupted")
            {
                return "中断";
            }
            return "未检测到";
        }
```

Then replace:

```cpp
            lines.push_back(build_metric_line("链路判定", describe_link_state(summary)));
```

with:

```cpp
            lines.push_back(build_metric_line("业务流状态", describe_traffic_state(summary)));
            if (!summary.traffic_flow.first_seen_wall.empty())
            {
                lines.push_back(build_metric_line("首次检测时间", summary.traffic_flow.first_seen_wall));
            }
            if (!summary.traffic_flow.last_seen_wall.empty())
            {
                lines.push_back(build_metric_line("最近有效流量", summary.traffic_flow.last_seen_wall));
            }
            if (!summary.traffic_flow.last_interrupted_wall.empty())
            {
                lines.push_back(build_metric_line("最近中断时间", summary.traffic_flow.last_interrupted_wall));
            }
            if (!summary.traffic_flow.last_resumed_wall.empty())
            {
                lines.push_back(build_metric_line("最近恢复时间", summary.traffic_flow.last_resumed_wall));
            }
```

Keep the rest of the panel mostly intact for Stage 3; the goal is semantic correction, not full Stage 4 redesign.

- [ ] **Step 4: Run the unit and fake integration checks**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_owner_loop_summary test_receive_runner_fake
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_owner_loop_summary
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/integration
ctest --output-on-failure -R test_receive_runner_fake
```

Expected:

- `test_owner_loop_summary` passes with Stage 3 wording
- `test_receive_runner_fake` either passes or exposes a precise fake-runner assumption that needs one targeted fix

- [ ] **Step 5: Commit**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add src/receiver/sidecar/status_panel.cpp tests/unit/test_owner_loop_summary.cpp tests/integration/test_receive_runner_fake.cpp
git commit -m "feat: show current traffic state in stage3 panel"
```

---

### Task 5: Stage 3 Local Validation Checkpoint

**Files:**
- Modify: `docs/superpowers/specs/2026-04-11-observability-logging-redesign-design.md`
  - Only if Stage 3 implementation reveals a real mismatch against the spec

- [ ] **Step 1: Run the Stage 3 local unit gate**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_traffic_state_tracker test_event_logger test_summary_renderer test_owner_loop_summary test_rx_config test_metrics_exporter rx_receiver_socket
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R "test_traffic_state_tracker|test_event_logger|test_summary_renderer|test_owner_loop_summary|test_rx_config|test_metrics_exporter"
```

Expected: all listed unit tests pass in the WSL fallback environment.

- [ ] **Step 2: Run the Stage 3 runtime artifact check**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
./build-preset-werror/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d | sort | tail -n 1)"
grep -n "\"traffic.interrupted\"\\|\"traffic.resumed\"\\|\"status.snapshot\"" "${LATEST_RUN_DIR}/events.jsonl" || true
sed -n '1,20p' "${LATEST_RUN_DIR}/summary.txt"
```

Expected:

- `status.snapshot` exists
- panel/summary wording uses Stage 3 current-state semantics
- if no real traffic arrives in the socket loopback run, `traffic.interrupted` and `traffic.resumed` may legitimately be absent; document that as a local fallback limitation rather than a failure

- [ ] **Step 3: Record Stage 3 residual gaps**

Document in the execution log or PR summary:

- no authoritative server validation yet
- no real 10G NIC / sender interruption path yet
- local fallback can validate state-machine code paths and text semantics, but not real traffic interruption behavior on the production network path

- [ ] **Step 4: Final commit only if a spec correction is required**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add docs/superpowers/specs/2026-04-11-observability-logging-redesign-design.md
git commit -m "docs: align stage3 traffic panel spec"
```

Skip this commit if the spec still matches implementation.

---

## Spec Coverage Check

- Spec Section 10.2
  - Covered by Task 1, Task 2, and Task 3 through the explicit traffic state machine and transition events.
- Spec Section 10.3 and 10.4
  - Covered by Task 4 through current-state panel wording and continued single-writer rendering.
- Spec Section 14
  - Covered partially by Task 5; real network interruption verification remains a later authoritative validation item.

## Self-Review Notes

- Stage 3 stays focused on traffic-state semantics and panel rendering.
- Stage 4 debug artifact policy is not included.
- The plan assumes Stage 2 artifacts already exist on the branch:
  - `run.header`
  - `status.snapshot`
  - `summary.json`
  - `summary.txt`
