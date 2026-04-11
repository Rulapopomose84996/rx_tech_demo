# Observability Stage 4 Capture Policy And Debug Artifact Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace default full-run payload capture with an explicit capture policy so release/debug runs stop producing unbounded `capture_packets.bin`/`capture_index.csv`, while debug mode can retain a self-describing first effective CPI sample artifact.

**Architecture:** Introduce a typed capture policy in `RxConfig` and route all packet/index writes through a small `DebugCaptureWriter` owned by `ReceiveRunner`/`OwnerLoop`. The writer decides whether to discard, fully record, or only keep the first effective CPI sample. Stage 4 stays bounded to payload/index artifact policy; it does not modify Stage 3 traffic-state logic or the heavy raw-frame recorder beyond preserving its separate role.

**Tech Stack:** C++17, CMake 3.16, Ninja, existing `rx_receiver_core` / integration fake harnesses, WSL fallback validation, later Linux server validation via `ssh kds`

---

## File Map

### New Files

- `src/receiver/runtime/internal/debug_capture_writer.h`
  - Declares `CapturePolicy`, `DebugCaptureManifest`, `DebugCaptureWriter`, and path helpers for first-CPI artifacts.
- `src/receiver/runtime/debug_capture_writer.cpp`
  - Implements policy-based payload/index writing, first-effective-CPI gating, and manifest emission.
- `tests/unit/test_debug_capture_writer.cpp`
  - Verifies policy routing for `disabled`, `full`, and `first_effective_cpi`.

### Modified Files

- `include/rxtech/rx_config.h`
  - Replaces the boolean-only capture switch with a typed capture policy while keeping backward-compatible parsing.
- `src/receiver/runtime/rx_config.cpp`
  - Parses/validates capture policy and maps legacy `capture_enabled` to the new policy.
- `src/receiver/runtime/receive_runner.cpp`
  - Replaces direct packet/index stream ownership with `DebugCaptureWriter`.
- `src/receiver/core/owner_loop.cpp`
  - Routes accepted packets into `DebugCaptureWriter` instead of unconditional full-run writes.
- `include/rxtech/metrics.h`
  - Extends `CaptureSummary` with Stage 4 policy and first-CPI artifact paths.
- `src/receiver/sidecar/summary_renderer.cpp`
  - Surfaces capture policy and first-CPI artifact paths in `summary.json` / `summary.txt`.
- `src/receiver/CMakeLists.txt`
  - Adds `debug_capture_writer.cpp` to `rx_receiver_core`.
- `tests/unit/CMakeLists.txt`
  - Adds `test_debug_capture_writer`.
- `tests/unit/test_rx_config.cpp`
  - Covers policy parsing and legacy compatibility.
- `tests/integration/test_receive_runner_fake.cpp`
  - Updates fake-runner expectations from full-run capture to first-effective-CPI sample.

### Existing Files To Read Before Editing

- `include/rxtech/rx_config.h`
- `src/receiver/runtime/rx_config.cpp`
- `src/receiver/runtime/receive_runner.cpp`
- `src/receiver/core/owner_loop.cpp`
- `tests/integration/test_receive_runner_fake.cpp`
- `tests/unit/test_rx_config.cpp`

---

### Task 1: Write Failing Tests For Capture Policy Parsing

**Files:**
- Modify: `tests/unit/test_rx_config.cpp`
- Modify: `include/rxtech/rx_config.h`

- [ ] **Step 1: Add a failing config parsing assertion for the new policy**

In `tests/unit/test_rx_config.cpp`, add to the default-config checks:

```cpp
    if (default_config.capture.capture_policy != rxtech::CapturePolicy::first_effective_cpi)
    {
        std::cerr << "unexpected default capture policy\n";
        return 1;
    }
```

In the generated config block, add:

```cpp
        out << "policy = full\n";
```

Then extend the parsing assertion:

```cpp
        config.capture.capture_policy != rxtech::CapturePolicy::full ||
```

Add a second legacy compatibility case:

```cpp
    {
        rxtech::RxConfig legacy = rxtech::load_default_config();
        legacy.capture.capture_enabled = false;
        rxtech::merge_config(legacy, rxtech::RxConfig{});
        assert(legacy.capture.capture_enabled == false);
    }
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_rx_config
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_rx_config
```

Expected: compile or test failure because `CapturePolicy` and `capture_policy` do not exist yet.

---

### Task 2: Add `CapturePolicy` And Backward-Compatible Parsing

**Files:**
- Modify: `include/rxtech/rx_config.h`
- Modify: `src/receiver/runtime/rx_config.cpp`
- Modify: `tests/unit/test_rx_config.cpp`

- [ ] **Step 1: Add the enum and config field**

In `include/rxtech/rx_config.h`, add:

```cpp
    enum class CapturePolicy
    {
        disabled,
        first_effective_cpi,
        full,
    };
```

Update `CaptureConfig`:

```cpp
        bool capture_enabled = true;
        CapturePolicy capture_policy = CapturePolicy::first_effective_cpi;
```

Add declarations:

```cpp
    CapturePolicy parse_capture_policy(const std::string &value);
    const char *capture_policy_name(CapturePolicy policy) noexcept;
```

- [ ] **Step 2: Implement parsing and validation**

In `src/receiver/runtime/rx_config.cpp`, add:

```cpp
    CapturePolicy parse_capture_policy(const std::string &value)
    {
        const std::string normalized = to_lower(trim(value));
        if (normalized == "disabled" || normalized == "none")
        {
            return CapturePolicy::disabled;
        }
        if (normalized == "full")
        {
            return CapturePolicy::full;
        }
        return CapturePolicy::first_effective_cpi;
    }

    const char *capture_policy_name(CapturePolicy policy) noexcept
    {
        switch (policy)
        {
        case CapturePolicy::disabled: return "disabled";
        case CapturePolicy::first_effective_cpi: return "first_effective_cpi";
        case CapturePolicy::full: return "full";
        }
        return "first_effective_cpi";
    }
```

Add config dispatch entries:

```cpp
                    {"capture_policy",
                     {"capture.policy", "capture_policy"},
                     [](RxConfig &c, const std::string &v)
                     {
                         c.capture.capture_policy = parse_capture_policy(v);
                         c.capture.capture_enabled = c.capture.capture_policy != CapturePolicy::disabled;
                     }},
```

After config load/merge, normalize:

```cpp
        config.capture.capture_policy =
            config.capture.capture_enabled ? config.capture.capture_policy : CapturePolicy::disabled;
```

Validation rule:

```cpp
        if (config.capture.capture_policy == CapturePolicy::disabled)
        {
            // no payload/index artifact required
        }
        else
        {
            // existing output dir / filenames checks stay active
        }
```

- [ ] **Step 3: Run `test_rx_config`**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_rx_config
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_rx_config
```

Expected: `test_rx_config` passes with the new policy parsing and legacy compatibility.

- [ ] **Step 4: Commit**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add include/rxtech/rx_config.h src/receiver/runtime/rx_config.cpp tests/unit/test_rx_config.cpp
git commit -m "feat: add stage4 capture policy config"
```

---

### Task 3: Add `DebugCaptureWriter` With First-Effective-CPI Policy

**Files:**
- Create: `src/receiver/runtime/internal/debug_capture_writer.h`
- Create: `src/receiver/runtime/debug_capture_writer.cpp`
- Modify: `src/receiver/CMakeLists.txt`
- Create: `tests/unit/test_debug_capture_writer.cpp`
- Modify: `tests/unit/CMakeLists.txt`

- [ ] **Step 1: Add the failing writer test**

Create `tests/unit/test_debug_capture_writer.cpp`:

```cpp
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <sstream>

#include "debug_capture_writer.h"

int main()
{
    std::ostringstream packet_stream;
    std::ostringstream index_stream;

    rxtech::DebugCaptureWriter writer(rxtech::CapturePolicy::first_effective_cpi,
                                      &packet_stream,
                                      &index_stream,
                                      "results/run");

    const rxtech::DebugCaptureRecord first{2U, 0U, 1U, 1U, "data_packet", true, "abcd"};
    const rxtech::DebugCaptureRecord second_same_cpi{2U, 0U, 1U, 2U, "data_packet", true, "efgh"};
    const rxtech::DebugCaptureRecord next_cpi{3U, 0U, 1U, 1U, "data_packet", true, "ijkl"};

    writer.record(first);
    writer.record(second_same_cpi);
    writer.record(next_cpi);
    writer.finish();

    assert(packet_stream.str() == "abcdefgh");
    assert(index_stream.str().find("2,0,1,1") != std::string::npos);
    assert(index_stream.str().find("3,0,1,1") == std::string::npos);
    assert(writer.manifest().selected_cpi == 2U);
    return 0;
}
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_debug_capture_writer
```

Expected: build failure because `debug_capture_writer.h` does not exist yet.

- [ ] **Step 3: Implement the writer**

Create `src/receiver/runtime/internal/debug_capture_writer.h`:

```cpp
#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

#include "rxtech/rx_config.h"

namespace rxtech
{
    struct DebugCaptureRecord
    {
        std::uint16_t cpi = 0;
        std::uint16_t channel = 0;
        std::uint16_t prt = 0;
        std::uint16_t packet_index = 0;
        std::string packet_kind;
        bool valid = false;
        std::string payload;
    };

    struct DebugCaptureManifest
    {
        std::string policy;
        std::uint16_t selected_cpi = 0;
        bool selected = false;
    };

    class DebugCaptureWriter
    {
      public:
        DebugCaptureWriter(CapturePolicy policy, std::ostream *packet_stream,
                           std::ostream *index_stream, std::string run_dir);
        void record(const DebugCaptureRecord &record);
        void finish();
        const DebugCaptureManifest &manifest() const noexcept;

      private:
        CapturePolicy policy_;
        std::ostream *packet_stream_ = nullptr;
        std::ostream *index_stream_ = nullptr;
        std::string run_dir_;
        DebugCaptureManifest manifest_{};
    };
}
```

Create `src/receiver/runtime/debug_capture_writer.cpp` with the minimal Stage 4 behavior:

```cpp
#include "internal/debug_capture_writer.h"

#include <ostream>

namespace rxtech
{
    DebugCaptureWriter::DebugCaptureWriter(CapturePolicy policy, std::ostream *packet_stream,
                                           std::ostream *index_stream, std::string run_dir)
        : policy_(policy), packet_stream_(packet_stream), index_stream_(index_stream), run_dir_(std::move(run_dir))
    {
        manifest_.policy = capture_policy_name(policy_);
    }

    void DebugCaptureWriter::record(const DebugCaptureRecord &record)
    {
        if (policy_ == CapturePolicy::disabled)
        {
            return;
        }

        if (policy_ == CapturePolicy::first_effective_cpi)
        {
            if (!manifest_.selected)
            {
                manifest_.selected = true;
                manifest_.selected_cpi = record.cpi;
            }
            if (record.cpi != manifest_.selected_cpi)
            {
                return;
            }
        }

        if (packet_stream_ != nullptr)
        {
            (*packet_stream_) << record.payload;
        }
        if (index_stream_ != nullptr)
        {
            (*index_stream_) << record.cpi << ',' << record.channel << ',' << record.prt << ','
                             << record.packet_index << ',' << record.packet_kind << ','
                             << (record.valid ? "true" : "false") << '\n';
        }
    }

    void DebugCaptureWriter::finish() {}

    const DebugCaptureManifest &DebugCaptureWriter::manifest() const noexcept
    {
        return manifest_;
    }
}
```

- [ ] **Step 4: Run the writer unit test**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_debug_capture_writer
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_debug_capture_writer
```

Expected: `test_debug_capture_writer` passes.

- [ ] **Step 5: Commit**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add src/receiver/CMakeLists.txt src/receiver/runtime/internal/debug_capture_writer.h src/receiver/runtime/debug_capture_writer.cpp tests/unit/CMakeLists.txt tests/unit/test_debug_capture_writer.cpp
git commit -m "feat: add stage4 debug capture writer"
```

---

### Task 4: Route OwnerLoop And ReceiveRunner Through Capture Policy

**Files:**
- Modify: `src/receiver/runtime/receive_runner.cpp`
- Modify: `src/receiver/core/owner_loop.cpp`
- Modify: `include/rxtech/metrics.h`
- Modify: `src/receiver/sidecar/summary_renderer.cpp`
- Modify: `tests/integration/test_receive_runner_fake.cpp`

- [ ] **Step 1: Update the fake integration expectation first**

In `tests/integration/test_receive_runner_fake.cpp`, replace the full-run capture expectations:

```cpp
        assert(summary.capture.recorded_packets == summary.capture.captured_packets);
```

with Stage 4 expectations:

```cpp
        assert(summary.capture.recorded_packets <= summary.capture.captured_packets);
        assert(summary.capture.capture_policy == "first_effective_cpi");
```

Add assertions for first-CPI artifacts:

```cpp
        assert(!summary.capture.index_path.empty());
        assert(!summary.capture.packets_path.empty());
```

- [ ] **Step 2: Run the fake integration to verify it fails**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target rxtech_integration_fake_tests
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/integration
ctest --output-on-failure -R rxtech_integration_fake_tests
```

Expected: failure because runtime still writes every accepted packet unconditionally.

- [ ] **Step 3: Replace direct stream writes with `DebugCaptureWriter`**

In `include/rxtech/metrics.h`, extend `CaptureSummary`:

```cpp
        std::string capture_policy;
```

In `src/receiver/runtime/receive_runner.cpp`, replace direct `capture_packets_stream` / `capture_index_stream` ownership with:

```cpp
            DebugCaptureWriter capture_writer(context.config.capture.capture_policy,
                                             capture_enabled ? static_cast<std::ostream *>(&capture_packets_stream) : static_cast<std::ostream *>(&capture_packets_sink),
                                             capture_enabled ? static_cast<std::ostream *>(&capture_index_stream) : static_cast<std::ostream *>(&capture_index_sink),
                                             output_dir);
```

Store a pointer/reference to `capture_writer` in `CaptureArtifacts`.

In `src/receiver/core/owner_loop.cpp`, replace the unconditional packet/index writes with:

```cpp
                        DebugCaptureRecord record;
                        record.cpi = processed.interpreted.cpi;
                        record.channel = processed.interpreted.channel;
                        record.prt = processed.interpreted.prt;
                        record.packet_index = processed.interpreted.packet_index;
                        record.packet_kind = packet_kind_name(processed.interpreted.kind);
                        record.valid = processed.interpreted.valid;
                        record.payload.assign(reinterpret_cast<const char *>(processed.udp_frame.udp_payload.data()),
                                              processed.udp_frame.udp_payload.size());
                        artifacts.capture_writer->record(record);
```

Update `CaptureArtifacts` to carry the writer pointer.

When filling the summary in `ReceiveRunner`, set:

```cpp
            summary.capture.capture_policy = capture_policy_name(context.config.capture.capture_policy);
```

In `src/receiver/sidecar/summary_renderer.cpp`, surface:

```cpp
                {"capture_policy", summary.capture.capture_policy},
```

- [ ] **Step 4: Run the Stage 4 fake integration and unit gate**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_debug_capture_writer test_rx_config rxtech_integration_fake_tests
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R "test_debug_capture_writer|test_rx_config"
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/integration
ctest --output-on-failure -R rxtech_integration_fake_tests
```

Expected:

- config and writer tests pass
- fake integration passes with Stage 4 policy semantics

- [ ] **Step 5: Commit**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add include/rxtech/metrics.h src/receiver/runtime/receive_runner.cpp src/receiver/core/owner_loop.cpp src/receiver/sidecar/summary_renderer.cpp tests/integration/test_receive_runner_fake.cpp
git commit -m "feat: route capture through stage4 policy"
```

---

### Task 5: Stage 4 Local Validation Checkpoint

**Files:**
- Modify: `docs/superpowers/specs/2026-04-11-observability-logging-redesign-design.md`
  - Only if Stage 4 implementation reveals a real mismatch against the spec

- [ ] **Step 1: Run the Stage 4 local gate**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
cmake --build --preset linux-server-werror --target test_debug_capture_writer test_traffic_state_tracker test_event_logger test_summary_renderer test_owner_loop_summary test_rx_config test_metrics_exporter rx_receiver_socket rxtech_integration_fake_tests
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R "test_debug_capture_writer|test_traffic_state_tracker|test_event_logger|test_summary_renderer|test_owner_loop_summary|test_rx_config|test_metrics_exporter"
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo/build-preset-werror/tests/integration
ctest --output-on-failure -R rxtech_integration_fake_tests
```

Expected: all listed local WSL tests pass.

- [ ] **Step 2: Run the Stage 4 runtime artifact check**

Execution location: local WSL workspace `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
./build-preset-werror/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d -exec test -f '{}/events.jsonl' ';' -print | sort | tail -n 1)"
test -f "${LATEST_RUN_DIR}/summary.json"
python3 - <<'PY'
import json, pathlib
runs = sorted([p for p in pathlib.Path("results").iterdir() if p.is_dir() and p.joinpath("summary.json").exists()])
latest = runs[-1]
summary = json.loads(latest.joinpath("summary.json").read_text(encoding="utf-8"))
assert summary["summary"]["capture"]["capture_policy"] in {"disabled", "first_effective_cpi", "full"}
print(summary["summary"]["capture"]["capture_policy"])
PY
```

Expected:

- the run still produces Stage 1-3 artifacts
- `summary.json` shows the capture policy
- local socket fallback may not produce a meaningful first-effective-CPI sample if no real business traffic is present; document that as a limitation rather than a failure

- [ ] **Step 3: Record residual gaps**

Document in the execution log or PR summary:

- no authoritative server validation yet
- no real first-effective-CPI sample from production traffic yet
- heavy raw-frame recorder remains a separate artifact path and was not reworked in Stage 4

- [ ] **Step 4: Final commit only if a spec correction is required**

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git add docs/superpowers/specs/2026-04-11-observability-logging-redesign-design.md
git commit -m "docs: align stage4 capture policy spec"
```

Skip this commit if the spec still matches implementation.

---

## Spec Coverage Check

- Spec Section 9
  - Covered by Task 1 to Task 4 through capture policy typing, first-effective-CPI policy, and summary surfacing.
- Spec Section 12 Stage 4
  - Covered by Task 2 to Task 5 through config parsing, policy-based writer, fake integration, and local runtime artifact checks.

## Self-Review Notes

- Stage 4 is kept to capture/debug artifact policy only.
- The heavy raw-frame recorder stays untouched except for continued coexistence.
- The plan assumes Stages 1-3 already exist on the branch:
  - events.jsonl
  - summary.json / summary.txt
  - traffic state and current-state panel semantics
