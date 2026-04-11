# Observability Stage 2 Release Convergence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Stage 2 release observability outputs so each run emits `run.header`, periodic `status.snapshot`, and stable `summary.json` / `summary.txt` artifacts derived from the existing `RunSummary`.

**Architecture:** Reuse the Stage 1 event pipeline already living behind `structured_log(...)`, then add a narrow Stage 2 layer for release artifacts: a `run_context_snapshot` builder for `run.header`, a `summary_renderer` for JSON/text outputs, and runtime wiring in `RuntimeStatusReporter` / `ReceiveRunner`. Do not introduce the Stage 3 traffic state machine in this phase; `status.snapshot` should be driven by current `RunSummary` fields only.

**Tech Stack:** C++17, CMake 3.16, Ninja, `nlohmann/json`, existing `rx_receiver_core` / `rx_receiver_app` libraries, WSL local validation plus later Linux server validation via `ssh kds`

---

## File Map

### New Files

- `src/receiver/sidecar/internal/run_context_snapshot.h`
  - Defines `RunHeaderSnapshot` plus helpers that extract release header fields from `RxConfig`.
- `src/receiver/sidecar/run_context_snapshot.cpp`
  - Implements `build_run_header_snapshot(...)` and `render_run_header_event_payload(...)`.
- `src/receiver/sidecar/internal/summary_renderer.h`
  - Declares `render_summary_json(...)`, `render_summary_text(...)`, and summary artifact path helpers.
- `src/receiver/sidecar/summary_renderer.cpp`
  - Implements `summary.json` and `summary.txt` rendering from `RunSummary`.
- `tests/unit/test_summary_renderer.cpp`
  - Verifies header payload fields, summary JSON shape, text summary sections, and artifact path derivation.

### Modified Files

- `include/rxtech/metrics.h`
  - Extends `RunInfoSummary` with Stage 2 header/runtime fields needed by release artifacts.
- `src/receiver/sidecar/internal/runtime_status_reporter.h`
  - Adds Stage 2 methods for emitting `run.header` and `status.snapshot`.
- `src/receiver/sidecar/runtime_status_reporter.cpp`
  - Emits `run.header` once, `status.snapshot` periodically, and returns summary data suitable for artifact rendering.
- `src/receiver/runtime/receive_runner.cpp`
  - Writes `summary.json` and `summary.txt` into the run artifact directory after `OwnerLoop` returns.
- `src/receiver/core/owner_loop_summary.cpp`
  - Refactors text summary assembly so `summary_renderer` can reuse the human summary content instead of duplicating it.
- `src/receiver/CMakeLists.txt`
  - Adds `run_context_snapshot.cpp` and `summary_renderer.cpp` to `rx_receiver_core`.
- `tests/unit/CMakeLists.txt`
  - Adds `test_summary_renderer`.
- `tests/unit/test_owner_loop_summary.cpp`
  - Updates existing assertions if Stage 2 text summary rendering reuses `build_run_human_summary(...)`.

### Existing Files To Read Before Editing

- `src/receiver/sidecar/runtime_status_reporter.cpp`
- `src/receiver/core/owner_loop_summary.cpp`
- `src/receiver/runtime/receive_runner.cpp`
- `include/rxtech/metrics.h`
- `tests/unit/test_event_logger.cpp`
- `tests/unit/test_owner_loop_summary.cpp`

---

### Task 1: Add Failing Tests for `run.header` and Summary Rendering

**Files:**
- Create: `tests/unit/test_summary_renderer.cpp`
- Modify: `tests/unit/CMakeLists.txt`
- Modify: `src/receiver/CMakeLists.txt`

- [ ] **Step 1: Write the failing Stage 2 renderer test**

Create `tests/unit/test_summary_renderer.cpp` with:

```cpp
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>

#include <nlohmann/json.hpp>

#include "run_context_snapshot.h"
#include "summary_renderer.h"
#include "rxtech/rx_config.h"
#include "rxtech/metrics.h"

int main()
{
    rxtech::RxConfig config = rxtech::load_default_config();
    config.process.backend_name = "socket";
    config.process.config_path = "configs/socket_loopback.conf";
    config.process.run_label = "20260411_235000_socket_loopback";
    config.operations.output_dir = "results/20260411_235000_socket_loopback";
    config.runtime.run_until_stopped = true;

    const rxtech::RunHeaderSnapshot header = rxtech::build_run_header_snapshot(config);
    const nlohmann::json header_payload = rxtech::render_run_header_event_payload(header);
    assert(header_payload.at("backend") == "socket");
    assert(header_payload.at("artifacts").at("run_dir") == "results/20260411_235000_socket_loopback");
    assert(header_payload.at("logging").at("events_path") == "results/20260411_235000_socket_loopback/events.jsonl");

    rxtech::RunSummary summary;
    summary.run.backend_name = "socket";
    summary.run.status = "success";
    summary.run.error_message.clear();
    summary.capture.run_artifact_dir = "results/20260411_235000_socket_loopback";
    summary.capture.packets_path = "results/20260411_235000_socket_loopback/capture_packets.bin";
    summary.capture.index_path = "results/20260411_235000_socket_loopback/capture_index.csv";
    summary.protocol.rx_packets = 10U;
    summary.protocol.parsed_packets = 8U;
    summary.protocol.data_packets = 7U;
    summary.protocol.control_table_packets = 1U;
    summary.protocol.dropped_packets = 2U;
    summary.performance.cpu_metrics_available = true;
    summary.performance.cpu_user_pct = 12.5;
    summary.performance.cpu_sys_pct = 1.5;
    summary.performance.cpu_peak_pct = 13.0;
    summary.metrics_export.enabled = false;

    const std::string json_text = rxtech::render_summary_json(summary, header);
    const nlohmann::json parsed_json = nlohmann::json::parse(json_text);
    assert(parsed_json.at("header").at("backend") == "socket");
    assert(parsed_json.at("summary").at("protocol").at("parsed_packets") == 8U);

    const std::string text_summary = rxtech::render_summary_text(summary, header);
    assert(text_summary.find("运行头部") != std::string::npos);
    assert(text_summary.find("后端类型： socket") != std::string::npos);
    assert(text_summary.find("解析有效包： 8 包") != std::string::npos);

    assert(rxtech::summary_json_path("results/20260411_235000_socket_loopback") ==
           "results/20260411_235000_socket_loopback/summary.json");
    assert(rxtech::summary_text_path("results/20260411_235000_socket_loopback") ==
           "results/20260411_235000_socket_loopback/summary.txt");
    return 0;
}
```

Update `tests/unit/CMakeLists.txt`:

```cmake
add_executable(test_summary_renderer test_summary_renderer.cpp)
target_link_libraries(test_summary_renderer PRIVATE rx_receiver_core nlohmann_json)
target_include_directories(test_summary_renderer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/receiver/sidecar/internal
)
add_test(NAME test_summary_renderer COMMAND test_summary_renderer)
set_tests_properties(test_summary_renderer PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
rxtech_apply_warnings(test_summary_renderer)
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_summary_renderer
```

Expected: build failure because `run_context_snapshot.h`, `summary_renderer.h`, and their functions do not exist yet.

- [ ] **Step 3: Wire the new target into `rx_receiver_core`**

Update `src/receiver/CMakeLists.txt` so `rx_receiver_core` includes:

```cmake
    sidecar/run_context_snapshot.cpp
    sidecar/summary_renderer.cpp
```

- [ ] **Step 4: Commit after RED is confirmed**

No commit in the RED phase. Move directly to Task 2 after confirming the failure reason is “missing Stage 2 files”.

---

### Task 2: Implement `RunHeaderSnapshot` and `summary_renderer`

**Files:**
- Create: `src/receiver/sidecar/internal/run_context_snapshot.h`
- Create: `src/receiver/sidecar/run_context_snapshot.cpp`
- Create: `src/receiver/sidecar/internal/summary_renderer.h`
- Create: `src/receiver/sidecar/summary_renderer.cpp`
- Modify: `include/rxtech/metrics.h`

- [ ] **Step 1: Add the Stage 2 header and renderer interfaces**

Create `src/receiver/sidecar/internal/run_context_snapshot.h`:

```cpp
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "rxtech/rx_config.h"

namespace rxtech
{
    struct RunHeaderSnapshot
    {
        std::string backend;
        std::string build_mode;
        std::string config_path;
        std::string run_id;
        std::string run_dir;
        std::string events_path;
        std::string summary_json_path;
        std::string summary_text_path;
        std::string host = "unknown";
    };

    RunHeaderSnapshot build_run_header_snapshot(const RxConfig& config);
    nlohmann::json render_run_header_event_payload(const RunHeaderSnapshot& snapshot);
}
```

Create `src/receiver/sidecar/internal/summary_renderer.h`:

```cpp
#pragma once

#include <string>

#include "run_context_snapshot.h"
#include "rxtech/metrics.h"

namespace rxtech
{
    std::string summary_json_path(const std::string& run_dir);
    std::string summary_text_path(const std::string& run_dir);
    std::string render_summary_json(const RunSummary& summary, const RunHeaderSnapshot& header);
    std::string render_summary_text(const RunSummary& summary, const RunHeaderSnapshot& header);
}
```

- [ ] **Step 2: Implement the minimal Stage 2 builders**

Create `src/receiver/sidecar/run_context_snapshot.cpp`:

```cpp
#include "internal/run_context_snapshot.h"

#include "internal/structured_logger.h"
#include "internal/summary_renderer.h"

namespace rxtech
{
    namespace
    {
        const char* build_mode_name() noexcept
        {
#ifdef NDEBUG
            return "release";
#else
            return "debug";
#endif
        }
    }

    RunHeaderSnapshot build_run_header_snapshot(const RxConfig& config)
    {
        RunHeaderSnapshot snapshot;
        snapshot.backend = config.process.backend_name;
        snapshot.build_mode = build_mode_name();
        snapshot.config_path = config.process.config_path;
        snapshot.run_id = config.process.run_label;
        snapshot.run_dir = config.operations.output_dir;
        snapshot.events_path = structured_logger_events_path();
        snapshot.summary_json_path = summary_json_path(snapshot.run_dir);
        snapshot.summary_text_path = summary_text_path(snapshot.run_dir);
        return snapshot;
    }

    nlohmann::json render_run_header_event_payload(const RunHeaderSnapshot& snapshot)
    {
        return {
            {"backend", snapshot.backend},
            {"build_mode", snapshot.build_mode},
            {"config_path", snapshot.config_path},
            {"run_id", snapshot.run_id},
            {"host", snapshot.host},
            {"logging", {{"events_path", snapshot.events_path}}},
            {"artifacts",
             {{"run_dir", snapshot.run_dir},
              {"summary_json_path", snapshot.summary_json_path},
              {"summary_text_path", snapshot.summary_text_path}}}
        };
    }
}
```

Create `src/receiver/sidecar/summary_renderer.cpp`:

```cpp
#include "internal/summary_renderer.h"

#include <sstream>

#include <nlohmann/json.hpp>

namespace rxtech
{
    std::string summary_json_path(const std::string& run_dir)
    {
        return run_dir + "/summary.json";
    }

    std::string summary_text_path(const std::string& run_dir)
    {
        return run_dir + "/summary.txt";
    }

    std::string render_summary_json(const RunSummary& summary, const RunHeaderSnapshot& header)
    {
        nlohmann::json json_summary = {
            {"header",
             {{"backend", header.backend},
              {"build_mode", header.build_mode},
              {"config_path", header.config_path},
              {"run_id", header.run_id},
              {"events_path", header.events_path},
              {"run_dir", header.run_dir}}},
            {"summary",
             {{"run", {{"status", summary.run.status}, {"error_message", summary.run.error_message}}},
              {"protocol",
               {{"rx_packets", summary.protocol.rx_packets},
                {"parsed_packets", summary.protocol.parsed_packets},
                {"data_packets", summary.protocol.data_packets},
                {"control_table_packets", summary.protocol.control_table_packets},
                {"dropped_packets", summary.protocol.dropped_packets}}},
              {"capture",
               {{"packets_path", summary.capture.packets_path},
                {"index_path", summary.capture.index_path},
                {"run_artifact_dir", summary.capture.run_artifact_dir}}},
              {"performance",
               {{"cpu_metrics_available", summary.performance.cpu_metrics_available},
                {"cpu_user_pct", summary.performance.cpu_user_pct},
                {"cpu_sys_pct", summary.performance.cpu_sys_pct},
                {"cpu_peak_pct", summary.performance.cpu_peak_pct}}}}}
        };
        return json_summary.dump(2);
    }

    std::string render_summary_text(const RunSummary& summary, const RunHeaderSnapshot& header)
    {
        std::ostringstream out;
        out << "========== 运行头部 ==========\n";
        out << "后端类型： " << header.backend << '\n';
        out << "构建模式： " << header.build_mode << '\n';
        out << "配置路径： " << header.config_path << '\n';
        out << "运行目录： " << header.run_dir << '\n';
        out << "事件文件： " << header.events_path << "\n\n";
        out << build_run_human_summary(summary);
        return out.str();
    }
}
```

In `include/rxtech/metrics.h`, extend `RunInfoSummary` with:

```cpp
        std::string run_id;
        std::string config_path;
        std::string events_path;
        std::string summary_json_path;
        std::string summary_text_path;
```

- [ ] **Step 3: Run the targeted Stage 2 test**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_summary_renderer
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_summary_renderer
```

Expected: `test_summary_renderer` passes.

- [ ] **Step 4: Commit**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add include/rxtech/metrics.h src/receiver/CMakeLists.txt src/receiver/sidecar/internal/run_context_snapshot.h src/receiver/sidecar/run_context_snapshot.cpp src/receiver/sidecar/internal/summary_renderer.h src/receiver/sidecar/summary_renderer.cpp tests/unit/CMakeLists.txt tests/unit/test_summary_renderer.cpp
git commit -m "feat: add stage2 run header and summary renderers"
```

---

### Task 3: Emit `run.header` and `status.snapshot` from `RuntimeStatusReporter`

**Files:**
- Modify: `src/receiver/sidecar/internal/runtime_status_reporter.h`
- Modify: `src/receiver/sidecar/runtime_status_reporter.cpp`
- Modify: `tests/unit/test_event_logger.cpp`

- [ ] **Step 1: Extend the event logger test with a `status.snapshot` shape check**

Append to `tests/unit/test_event_logger.cpp`:

```cpp
    {
        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.structured_log_output = "file";
        config.operations.structured_log_file_path = "test_event_logger_snapshot.jsonl";
        std::remove(config.operations.structured_log_file_path.c_str());

        rxtech::configure_structured_logger(config);
        rxtech::structured_log(rxtech::StructuredLogLevel::info, "status.snapshot",
                               {{"backend", "socket"},
                                {"traffic_state", "idle"},
                                {"window_rx_gbps", 0.0},
                                {"protocol_parsed_packets", 0U},
                                {"elapsed_seconds", 5U}});
        rxtech::shutdown_structured_logger();

        std::ifstream input(config.operations.structured_log_file_path);
        std::string line;
        std::getline(input, line);
        const nlohmann::json parsed_snapshot = nlohmann::json::parse(line);
        assert(parsed_snapshot.at("event") == "status.snapshot");
        assert(parsed_snapshot.at("payload").at("traffic_state") == "idle");
        std::remove(config.operations.structured_log_file_path.c_str());
    }
```

- [ ] **Step 2: Run the test to verify it still passes before integration**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_event_logger
```

Expected: still green, because this test only exercises the compatibility event path.

- [ ] **Step 3: Add Stage 2 runtime reporter helpers**

In `src/receiver/sidecar/internal/runtime_status_reporter.h`, add:

```cpp
        void emit_run_header() const;
        void emit_status_snapshot(const RunSummary& summary, std::uint32_t elapsed_seconds) const;
        RunHeaderSnapshot build_run_header() const;
```

Add includes:

```cpp
#include "run_context_snapshot.h"
```

In `src/receiver/sidecar/runtime_status_reporter.cpp`, implement:

```cpp
    RunHeaderSnapshot RuntimeStatusReporter::build_run_header() const
    {
        return build_run_header_snapshot(config_);
    }

    void RuntimeStatusReporter::emit_run_header() const
    {
        const RunHeaderSnapshot header = build_run_header();
        structured_log(StructuredLogLevel::info, "run.header", render_run_header_event_payload(header));
    }

    void RuntimeStatusReporter::emit_status_snapshot(const RunSummary& summary, std::uint32_t elapsed_seconds) const
    {
        structured_log(StructuredLogLevel::info, "status.snapshot",
                       {{"backend", summary.run.backend_name},
                        {"traffic_state", summary.protocol.parsed_packets > 0U ? "active" : "idle"},
                        {"window_rx_gbps", summary.performance.actual_rx_gbps},
                        {"protocol_rx_packets", summary.protocol.rx_packets},
                        {"protocol_parsed_packets", summary.protocol.parsed_packets},
                        {"protocol_dropped_packets", summary.protocol.dropped_packets},
                        {"backend_dropped_packets", summary.backend.dropped_packets},
                        {"output_backpressure_count", summary.performance.output_backpressure_count},
                        {"sequence_gap_count", summary.global_packet_sequence.gap_count},
                        {"active_cpi", summary.active_prt.cpi},
                        {"active_prt", summary.active_prt.prt},
                        {"cpu_user_pct", summary.performance.cpu_user_pct},
                        {"cpu_sys_pct", summary.performance.cpu_sys_pct},
                        {"elapsed_seconds", elapsed_seconds}});
    }
```

Then call `emit_run_header()` once from the constructor body and call `emit_status_snapshot(...)` inside `emit_periodic(...)` after `build_summary(...)`.

- [ ] **Step 4: Run Stage 2 unit coverage**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger test_summary_renderer
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R "test_event_logger|test_summary_renderer"
```

Expected: both tests pass.

- [ ] **Step 5: Commit**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add src/receiver/sidecar/internal/runtime_status_reporter.h src/receiver/sidecar/runtime_status_reporter.cpp tests/unit/test_event_logger.cpp
git commit -m "feat: emit stage2 release header and snapshots"
```

---

### Task 4: Write `summary.json` and `summary.txt` from `ReceiveRunner`

**Files:**
- Modify: `src/receiver/runtime/receive_runner.cpp`
- Modify: `src/receiver/core/owner_loop_summary.cpp`
- Modify: `tests/unit/test_owner_loop_summary.cpp`

- [ ] **Step 1: Extend the owner-loop summary test with Stage 2 text header coverage**

In `tests/unit/test_owner_loop_summary.cpp`, after `build_run_human_summary(summary)`, add:

```cpp
    summary.run.events_path = "results/run/events.jsonl";
    summary.run.summary_json_path = "results/run/summary.json";
    summary.run.summary_text_path = "results/run/summary.txt";
```

Then assert:

```cpp
    assert(human.find("后端类型： dpdk") != std::string::npos);
```

Do not over-expand this test yet; `test_summary_renderer` is the main Stage 2 renderer test.

- [ ] **Step 2: Add summary artifact writing in `ReceiveRunner`**

In `src/receiver/runtime/receive_runner.cpp`, after `summary.capture.run_artifact_dir = output_dir;`, add:

```cpp
            const RunHeaderSnapshot header = build_run_header_snapshot(context.config);
            summary.run.run_id = header.run_id;
            summary.run.config_path = header.config_path;
            summary.run.events_path = header.events_path;
            summary.run.summary_json_path = header.summary_json_path;
            summary.run.summary_text_path = header.summary_text_path;
```

Then before `context.backend->shutdown();`, write both artifacts:

```cpp
            path_utils::ensure_parent_directory(summary.run.summary_json_path);
            path_utils::ensure_parent_directory(summary.run.summary_text_path);

            {
                std::ofstream summary_json_stream(summary.run.summary_json_path, std::ios::trunc);
                if (!summary_json_stream.is_open())
                {
                    throw std::runtime_error("打开 summary.json 失败: " + summary.run.summary_json_path);
                }
                summary_json_stream << render_summary_json(summary, header);
            }

            {
                std::ofstream summary_text_stream(summary.run.summary_text_path, std::ios::trunc);
                if (!summary_text_stream.is_open())
                {
                    throw std::runtime_error("打开 summary.txt 失败: " + summary.run.summary_text_path);
                }
                summary_text_stream << render_summary_text(summary, header);
            }
```

Add the includes:

```cpp
#include "run_context_snapshot.h"
#include "summary_renderer.h"
```

- [ ] **Step 3: Run the Stage 2 unit gate**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_summary_renderer test_owner_loop_summary rx_receiver_socket
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R "test_summary_renderer|test_owner_loop_summary"
```

Expected: both tests pass.

- [ ] **Step 4: Run the local WSL runtime artifact check**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
./build-preset-werror/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d | sort | tail -n 1)"
test -f "${LATEST_RUN_DIR}/events.jsonl"
test -f "${LATEST_RUN_DIR}/summary.json"
test -f "${LATEST_RUN_DIR}/summary.txt"
python3 - <<'PY'
import json
from pathlib import Path
latest = sorted(Path("results").glob("*"))[-1]
summary = json.loads(latest.joinpath("summary.json").read_text(encoding="utf-8"))
assert summary["header"]["backend"] == "socket"
assert "summary" in summary
print(summary["header"]["events_path"])
PY
sed -n '1,12p' "${LATEST_RUN_DIR}/summary.txt"
```

Expected:

- three files exist
- `summary.json` parses and contains `header.backend == "socket"`
- `summary.txt` begins with the Stage 2 header block

- [ ] **Step 5: Commit**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add src/receiver/runtime/receive_runner.cpp src/receiver/core/owner_loop_summary.cpp tests/unit/test_owner_loop_summary.cpp
git commit -m "feat: write stage2 release summary artifacts"
```

---

### Task 5: Stage 2 Local Validation Checkpoint

**Files:**
- Modify: `docs/superpowers/specs/2026-04-11-observability-logging-redesign-design.md`
  - Only if Stage 2 implementation reveals a real spec mismatch

- [ ] **Step 1: Run the Stage 2 local unit gate**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger test_summary_renderer test_owner_loop_summary test_rx_config test_metrics_exporter rx_receiver_socket
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R "test_event_logger|test_summary_renderer|test_owner_loop_summary|test_rx_config|test_metrics_exporter"
```

Expected: all listed tests pass in the WSL fallback environment.

- [ ] **Step 2: Run the Stage 2 runtime artifact check**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
./build-preset-werror/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d | sort | tail -n 1)"
wc -l "${LATEST_RUN_DIR}/events.jsonl"
grep -n "\"run.header\"" "${LATEST_RUN_DIR}/events.jsonl"
grep -n "\"status.snapshot\"" "${LATEST_RUN_DIR}/events.jsonl"
python3 - <<'PY'
import json
from pathlib import Path
latest = sorted(Path("results").glob("*"))[-1]
events = [json.loads(line)["event"] for line in latest.joinpath("events.jsonl").read_text(encoding="utf-8").splitlines()]
assert "run.started" in events
assert "run.header" in events
assert "run.stopped" in events
print(events)
PY
```

Expected:

- `run.header` is present
- at least one `status.snapshot` is present when runtime interval allows it
- `summary.json` and `summary.txt` still exist and parse/read cleanly

- [ ] **Step 3: Record the remaining non-authoritative gaps**

Document in the execution log or PR summary:

- WSL validation is local fallback only
- no real 10G NIC path was exercised
- Stage 3 traffic interruption semantics remain out of scope

- [ ] **Step 4: Final commit only if a spec correction is required**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add docs/superpowers/specs/2026-04-11-observability-logging-redesign-design.md
git commit -m "docs: align stage2 release observability spec"
```

Skip this commit if the spec still matches implementation.

---

## Spec Coverage Check

- Spec Section 6.6 and 6.7
  - Covered by Task 1 and Task 2 via `run.header` payload tests and summary JSON/text rendering.
- Spec Section 7.1 and 8
  - Covered by Task 2 and Task 4 through release-only artifacts and fixed header output.
- Spec Section 12 Stage 2
  - Covered by Task 2 to Task 5 through unit tests, runtime artifact generation, and local fallback verification.
- Spec Section 14.1
  - Not fully closed in WSL; later server validation still needs to measure cost under the real Linux server environment.

## Self-Review Notes

- The plan keeps Stage 2 bounded to release observability convergence.
- No Stage 3 traffic state machine behavior is included.
- The plan assumes the Stage 1 branch already contains:
  - `EventEnvelope`
  - `EventLogger`
  - `structured_logger_events_path()`
  - `run.started/run.stopped/run.failed`
