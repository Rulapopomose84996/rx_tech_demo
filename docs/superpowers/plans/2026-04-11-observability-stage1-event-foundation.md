# Observability Stage 1 Event Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Stage 1 event logging foundation for the receiver so `events.jsonl` becomes a real runtime artifact with testable event schema, injectable sinks, and startup/shutdown event emission.

**Architecture:** Keep Stage 1 intentionally narrow: introduce `EventEnvelope` and a testable `IEventLogger` abstraction inside `src/receiver/sidecar`, retain the current `structured_log(...)` compatibility surface, and route it through a new async event logger that always writes the primary `events.jsonl` artifact when structured logging is enabled. Do not pull Stage 2 summary/panel aggregation into this phase; only create the minimum event pipeline and Linux-server validation path required by the spec.

**Tech Stack:** C++17, CMake 3.16, Ninja, `spdlog`, `nlohmann/json`, CTest, Linux server validation via `ssh kds`

---

## File Map

### New Files

- `src/receiver/sidecar/internal/event_schema.h`
  - Defines `EventEnvelope`, schema version constants, level-to-string helpers, and JSON rendering helpers that do not depend on `spdlog`.
- `src/receiver/sidecar/internal/event_logger.h`
  - Defines `IEventSink`, `IEventLogger`, `EventLoggerConfig`, and testable logger interfaces.
- `src/receiver/sidecar/event_logger.cpp`
  - Implements the event logger, sink fan-out, JSONL serialization, level filtering, and sink factory glue.
- `tests/unit/test_event_logger.cpp`
  - Covers schema rendering, default path derivation, level filtering, injectable fake sink behavior, and file sink smoke behavior.

### Modified Files

- `src/receiver/sidecar/internal/structured_logger.h`
  - Keeps the current external compatibility API but forwards to the new event logger internals.
- `src/receiver/sidecar/structured_logger.cpp`
  - Shrinks into compatibility/bootstrap glue that configures the Stage 1 event logger and preserves `structured_logger_backend_name()`.
- `src/receiver/app/run_app.cpp`
  - Emits `run.started`, `run.stopped`, and `run.failed`, and records the effective `events.jsonl` path in startup/shutdown payloads.
- `src/receiver/CMakeLists.txt`
  - Adds `event_logger.cpp` to `rx_receiver_core`.
- `tests/unit/CMakeLists.txt`
  - Adds the new `test_event_logger` target.

### Existing Files To Read Before Editing

- `src/receiver/sidecar/structured_logger.cpp`
- `src/receiver/sidecar/internal/structured_logger.h`
- `src/receiver/app/run_app.cpp`
- `src/receiver/runtime/receive_runner.cpp`
- `tests/unit/test_metrics_exporter.cpp`
- `tests/unit/test_rx_config.cpp`

---

### Task 1: Define the Stage 1 Event Schema and Test Harness

**Files:**
- Create: `src/receiver/sidecar/internal/event_schema.h`
- Create: `tests/unit/test_event_logger.cpp`
- Modify: `tests/unit/CMakeLists.txt`
- Modify: `src/receiver/CMakeLists.txt`

- [ ] **Step 1: Write the failing schema and fake-sink tests**

Add this new test file skeleton to `tests/unit/test_event_logger.cpp`:

```cpp
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "event_schema.h"
#include "event_logger.h"

namespace
{
    class VectorSink final : public rxtech::IEventSink
    {
      public:
        void write_line(const std::string& line) override
        {
            lines.push_back(line);
        }

        void flush() override {}

        std::vector<std::string> lines;
    };
}

int main()
{
    rxtech::EventEnvelope event;
    event.event = "run.started";
    event.level = rxtech::StructuredLogLevel::info;
    event.ts_monotonic_ns = 123456789ULL;
    event.ts_wall = "2026-04-11 21:00:00";
    event.run_id = "20260411_210000_socket_loopback";
    event.backend = "socket";
    event.build_mode = "debug";
    event.payload = {{"config_path", "configs/socket_loopback.conf"}};

    const std::string line = rxtech::render_event_jsonl(event, rxtech::kEventSchemaVersion);
    const nlohmann::json parsed = nlohmann::json::parse(line);
    assert(parsed.at("schema_version") == rxtech::kEventSchemaVersion);
    assert(parsed.at("event") == "run.started");
    assert(parsed.at("backend") == "socket");
    assert(parsed.at("payload").at("config_path") == "configs/socket_loopback.conf");

    VectorSink sink;
    rxtech::EventLoggerConfig config;
    config.min_level = rxtech::StructuredLogLevel::info;
    config.sinks.push_back(&sink);

    rxtech::EventLogger logger(config);
    logger.emit(event);
    assert(sink.lines.size() == 1U);

    event.level = rxtech::StructuredLogLevel::debug;
    logger.emit(event);
    assert(sink.lines.size() == 1U);
    return 0;
}
```

Update `tests/unit/CMakeLists.txt` to add:

```cmake
add_executable(test_event_logger test_event_logger.cpp)
target_link_libraries(test_event_logger PRIVATE rx_receiver_core nlohmann_json)
target_include_directories(test_event_logger PRIVATE ${CMAKE_SOURCE_DIR}/src/receiver/sidecar/internal)
add_test(NAME test_event_logger COMMAND test_event_logger)
set_tests_properties(test_event_logger PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
rxtech_apply_warnings(test_event_logger)
```

- [ ] **Step 2: Run the targeted test to verify it fails**

On the Linux server after `ssh kds`, run:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cmake --preset linux-server-werror
cmake --build --preset linux-server-werror --target test_event_logger
```

Expected: build failure because `event_schema.h`, `event_logger.h`, `EventLoggerConfig`, and `EventLogger` do not exist yet.

- [ ] **Step 3: Add the minimal schema and interface definitions**

Create `src/receiver/sidecar/internal/event_schema.h` with:

```cpp
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "structured_logger.h"

namespace rxtech
{
    inline constexpr std::uint32_t kEventSchemaVersion = 1U;

    struct EventEnvelope
    {
        std::string event;
        StructuredLogLevel level = StructuredLogLevel::info;
        std::uint64_t ts_monotonic_ns = 0U;
        std::string ts_wall;
        std::string run_id;
        std::string backend;
        std::string build_mode;
        nlohmann::json payload = nlohmann::json::object();
    };

    const char* structured_log_level_name(StructuredLogLevel level) noexcept;
    std::string render_event_jsonl(const EventEnvelope& event, std::uint32_t schema_version);
}
```

Create `src/receiver/sidecar/internal/event_logger.h` with:

```cpp
#pragma once

#include <string>
#include <vector>

#include "event_schema.h"

namespace rxtech
{
    class IEventSink
    {
      public:
        virtual ~IEventSink() = default;
        virtual void write_line(const std::string& line) = 0;
        virtual void flush() = 0;
    };

    struct EventLoggerConfig
    {
        StructuredLogLevel min_level = StructuredLogLevel::info;
        std::vector<IEventSink*> sinks;
        std::uint32_t schema_version = kEventSchemaVersion;
    };

    class IEventLogger
    {
      public:
        virtual ~IEventLogger() = default;
        virtual void emit(const EventEnvelope& event) = 0;
        virtual void flush() = 0;
    };

    class EventLogger final : public IEventLogger
    {
      public:
        explicit EventLogger(EventLoggerConfig config);
        void emit(const EventEnvelope& event) override;
        void flush() override;

      private:
        EventLoggerConfig config_;
    };
}
```

Update `src/receiver/CMakeLists.txt` so `rx_receiver_core` includes:

```cmake
    sidecar/event_logger.cpp
```

- [ ] **Step 4: Implement the minimum code to make the test pass**

Create `src/receiver/sidecar/event_logger.cpp` with the first working implementation:

```cpp
#include "internal/event_logger.h"

#include <utility>

namespace rxtech
{
    namespace
    {
        int level_rank(StructuredLogLevel level)
        {
            switch (level)
            {
            case StructuredLogLevel::debug: return 0;
            case StructuredLogLevel::info: return 1;
            case StructuredLogLevel::warn: return 2;
            case StructuredLogLevel::error: return 3;
            }
            return 1;
        }
    }

    const char* structured_log_level_name(StructuredLogLevel level) noexcept
    {
        switch (level)
        {
        case StructuredLogLevel::debug: return "debug";
        case StructuredLogLevel::info: return "info";
        case StructuredLogLevel::warn: return "warn";
        case StructuredLogLevel::error: return "error";
        }
        return "info";
    }

    std::string render_event_jsonl(const EventEnvelope& event, std::uint32_t schema_version)
    {
        nlohmann::json record = event.payload.is_object() ? event.payload : nlohmann::json::object();
        record["ts_wall"] = event.ts_wall;
        record["ts_monotonic_ns"] = event.ts_monotonic_ns;
        record["run_id"] = event.run_id;
        record["backend"] = event.backend;
        record["build_mode"] = event.build_mode;
        record["schema_version"] = schema_version;
        record["event"] = event.event;
        record["level"] = structured_log_level_name(event.level);
        record["payload"] = event.payload.is_object() ? event.payload : nlohmann::json::object();
        return record.dump();
    }

    EventLogger::EventLogger(EventLoggerConfig config) : config_(std::move(config)) {}

    void EventLogger::emit(const EventEnvelope& event)
    {
        if (level_rank(event.level) < level_rank(config_.min_level))
        {
            return;
        }

        const std::string line = render_event_jsonl(event, config_.schema_version);
        for (IEventSink* sink : config_.sinks)
        {
            if (sink != nullptr)
            {
                sink->write_line(line);
            }
        }
    }

    void EventLogger::flush()
    {
        for (IEventSink* sink : config_.sinks)
        {
            if (sink != nullptr)
            {
                sink->flush();
            }
        }
    }
}
```

- [ ] **Step 5: Run the test to verify it passes**

On the Linux server after `ssh kds`, run:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger
cd /home/devuser/WorkSpace/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_event_logger
```

Expected: `test_event_logger` passes.

- [ ] **Step 6: Commit**

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
git add src/receiver/CMakeLists.txt src/receiver/sidecar/internal/event_schema.h src/receiver/sidecar/internal/event_logger.h src/receiver/sidecar/event_logger.cpp tests/unit/CMakeLists.txt tests/unit/test_event_logger.cpp
git commit -m "feat: add stage1 event schema foundation"
```

---

### Task 2: Add Sink Factory and Preserve the Structured Logger Compatibility API

**Files:**
- Modify: `src/receiver/sidecar/internal/structured_logger.h`
- Modify: `src/receiver/sidecar/structured_logger.cpp`
- Modify: `tests/unit/test_event_logger.cpp`

- [ ] **Step 1: Extend the test with file-sink and compatibility coverage**

Append these checks to `tests/unit/test_event_logger.cpp`:

```cpp
    const char* temp_path = "test_event_logger.jsonl";
    {
        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.structured_log_output = "file";
        config.operations.structured_log_file_path = temp_path;
        config.operations.structured_log_format = "json";
        config.operations.log_level = "info";

        rxtech::configure_structured_logger(config);
        rxtech::structured_log(rxtech::StructuredLogLevel::info, "run.started",
                               {{"backend", "socket"}, {"config_path", "configs/socket_loopback.conf"}});
        rxtech::shutdown_structured_logger();

        std::ifstream input(temp_path);
        assert(input.is_open());
        std::string line;
        std::getline(input, line);
        const nlohmann::json parsed_file = nlohmann::json::parse(line);
        assert(parsed_file.at("event") == "run.started");
        assert(parsed_file.at("payload").at("backend") == "socket");
    }
    std::remove(temp_path);
```

Add missing includes:

```cpp
#include <cstdio>
#include <fstream>
#include "rxtech/rx_config.h"
#include "structured_logger.h"
```

- [ ] **Step 2: Run the test to verify it fails**

On the Linux server after `ssh kds`, run:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger
cd /home/devuser/WorkSpace/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_event_logger
```

Expected: `test_event_logger` fails because `structured_log(...)` still writes the old record format and the payload shape does not match Stage 1 JSONL.

- [ ] **Step 3: Convert the compatibility header to expose the Stage 1 logger backend**

Update `src/receiver/sidecar/internal/structured_logger.h` to keep the public API but add explicit Stage 1 helpers:

```cpp
namespace rxtech
{
    struct StructuredLoggerRuntimeInfo
    {
        std::string backend_name = "disabled";
        std::string events_path;
    };

    void configure_structured_logger(const RxConfig& config);
    void shutdown_structured_logger();
    const char* structured_logger_backend_name() noexcept;
    std::string structured_logger_events_path();
    void structured_log(StructuredLogLevel level, const std::string& event,
                        const nlohmann::json& fields = nlohmann::json::object());
}
```

- [ ] **Step 4: Replace the old global formatter with the Stage 1 sink-based implementation**

Rewrite `src/receiver/sidecar/structured_logger.cpp` around the new event logger:

```cpp
#include "internal/structured_logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>

#include "internal/event_logger.h"

namespace rxtech
{
    namespace
    {
        class OstreamEventSink final : public IEventSink
        {
          public:
            explicit OstreamEventSink(std::ostream* stream) : stream_(stream) {}
            void write_line(const std::string& line) override
            {
                (*stream_) << line << '\n';
            }
            void flush() override
            {
                stream_->flush();
            }
          private:
            std::ostream* stream_ = nullptr;
        };

        class FileEventSink final : public IEventSink
        {
          public:
            explicit FileEventSink(const std::string& path) : stream_(path, std::ios::out | std::ios::app) {}
            bool is_open() const noexcept { return stream_.is_open(); }
            void write_line(const std::string& line) override
            {
                stream_ << line << '\n';
            }
            void flush() override
            {
                stream_.flush();
            }
          private:
            std::ofstream stream_;
        };

        std::mutex g_logger_mutex;
        std::unique_ptr<EventLogger> g_event_logger;
        std::vector<std::unique_ptr<IEventSink>> g_owned_sinks;
        std::string g_backend_name = "disabled";
        std::string g_events_path;
    }
}
```

Then implement `configure_structured_logger(...)` so it:

- Parses `config.operations.log_level`
- Always creates the primary `events.jsonl` sink at `default_events_log_path(config)` when `structured_log_output != "disabled"`
- Adds an optional mirror sink for `stdout` / `stderr` / explicit structured log file output
- Stores the effective `g_events_path`
- Builds `EventLoggerConfig`
- Preserves `structured_logger_backend_name()`

Implement `structured_log(...)` so it wraps `fields` into `EventEnvelope.payload` and forwards through `g_event_logger`.

- [ ] **Step 5: Run the updated unit test**

On the Linux server after `ssh kds`, run:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger
cd /home/devuser/WorkSpace/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_event_logger
```

Expected: `test_event_logger` passes and the file sink writes Stage 1 JSONL records with `payload`.

- [ ] **Step 6: Commit**

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
git add src/receiver/sidecar/internal/structured_logger.h src/receiver/sidecar/structured_logger.cpp tests/unit/test_event_logger.cpp
git commit -m "refactor: route structured logging through event logger"
```

---

### Task 3: Emit `run.started` / `run.stopped` / `run.failed` into `events.jsonl`

**Files:**
- Modify: `src/receiver/app/run_app.cpp`
- Modify: `src/receiver/sidecar/structured_logger.cpp`
- Modify: `tests/unit/test_event_logger.cpp`

- [ ] **Step 1: Add a failing test for the default events path helper**

Append this to `tests/unit/test_event_logger.cpp`:

```cpp
    {
        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.output_dir = "results/stage1_case";
        config.operations.structured_log_output = "stderr";
        if (rxtech::default_events_log_path(config) != "results/stage1_case/events.jsonl")
        {
            return 1;
        }
    }
```

Add this declaration to `src/receiver/sidecar/internal/structured_logger.h`:

```cpp
std::string default_events_log_path(const RxConfig& config);
```

- [ ] **Step 2: Run the test to verify it fails**

On the Linux server after `ssh kds`, run:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger
cd /home/devuser/WorkSpace/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_event_logger
```

Expected: build or test failure because `default_events_log_path(...)` is not implemented.

- [ ] **Step 3: Implement the default path helper and app bootstrap**

In `src/receiver/sidecar/structured_logger.cpp`, add:

```cpp
std::string default_events_log_path(const RxConfig& config)
{
    const std::string output_dir = config.operations.output_dir.empty() ? "results" : config.operations.output_dir;
    return output_dir + "/events.jsonl";
}
```

Inside `configure_structured_logger(...)`, derive the primary event file sink path for all enabled structured logging modes:

```cpp
g_events_path = default_events_log_path(config);
```

Then apply this Stage 1 rule set:

- If `structured_log_output == "disabled"`, skip Stage 1 event sink creation.
- Otherwise, always create the primary JSONL file sink at `g_events_path`.
- If `structured_log_output == "stdout"` or `structured_log_output == "stderr"`, add the matching mirror sink.
- If `structured_log_output == "file"` and `structured_log_file_path` is non-empty, add that file as a mirror sink in addition to `events.jsonl`.

In `src/receiver/app/run_app.cpp`, replace the current start/stop exception calls with Stage 1 event payloads:

```cpp
structured_log(StructuredLogLevel::info, "run.started",
               {{"backend", context.config.process.backend_name},
                {"config_path", context.config.process.config_path},
                {"events_path", structured_logger_events_path()}});
```

```cpp
structured_log(StructuredLogLevel::info, "run.stopped",
               {{"status", summary.run.status},
                {"backend", summary.run.backend_name},
                {"events_path", structured_logger_events_path()}});
```

```cpp
structured_log(StructuredLogLevel::error, "run.failed",
               {{"message", ex.what()},
                {"events_path", structured_logger_events_path()}});
```

Also ensure event logging calls `path_utils::ensure_parent_directory(g_events_path)` before opening the primary `events.jsonl` sink.

- [ ] **Step 4: Run the unit test and a file-output smoke test**

On the Linux server after `ssh kds`, run:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger rx_receiver_socket
cd /home/devuser/WorkSpace/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R test_event_logger
```

Expected: `test_event_logger` passes and the application still links.

- [ ] **Step 5: Validate the runtime artifact on the Linux server**

On the Linux server after `ssh kds`, run:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build-preset-werror/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d | sort | tail -n 1)"
test -f "${LATEST_RUN_DIR}/events.jsonl"
head -n 3 "${LATEST_RUN_DIR}/events.jsonl"
```

Expected:

- `events.jsonl` exists under the run artifact directory
- The first lines include `run.started`
- The file is valid JSONL and contains `run.stopped` on normal exit

- [ ] **Step 6: Commit**

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
git add src/receiver/app/run_app.cpp src/receiver/sidecar/internal/structured_logger.h src/receiver/sidecar/structured_logger.cpp tests/unit/test_event_logger.cpp
git commit -m "feat: emit stage1 runtime events to events jsonl"
```

---

### Task 4: Stage 1 Linux-Server Verification and Regression Gate

**Files:**
- Modify: `docs/superpowers/specs/2026-04-11-observability-logging-redesign-design.md`
  - Only if implementation reality requires correcting the spec wording after validation

- [ ] **Step 1: Run the targeted unit regression gate**

On the Linux server after `ssh kds`, run:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cmake --build --preset linux-server-werror --target test_event_logger test_rx_config test_metrics_exporter rx_receiver_socket
cd /home/devuser/WorkSpace/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R "test_event_logger|test_rx_config|test_metrics_exporter"
```

Expected: all three tests pass.

- [ ] **Step 2: Run the Stage 1 manual runtime check**

On the Linux server after `ssh kds`, run:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build-preset-werror/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d | sort | tail -n 1)"
wc -l "${LATEST_RUN_DIR}/events.jsonl"
python3 - <<'PY'
import json
from pathlib import Path
latest = sorted(Path("results").glob("*"))[-1]
lines = latest.joinpath("events.jsonl").read_text(encoding="utf-8").strip().splitlines()
events = [json.loads(line)["event"] for line in lines]
assert "run.started" in events
assert "run.stopped" in events
print(events[:5])
PY
```

Expected: JSON parse succeeds, `run.started` and `run.stopped` both exist, and no malformed lines appear.

- [ ] **Step 3: Record the Stage 1 verification note**

If validation exposed a mismatch between the spec and actual Stage 1 implementation, update the spec with a narrow correction. If no spec correction is needed, skip file edits and record the result in the implementation PR or execution log instead.

- [ ] **Step 4: Final commit if any validation-driven doc correction was needed**

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
git add docs/superpowers/specs/2026-04-11-observability-logging-redesign-design.md
git commit -m "docs: align observability spec with stage1 validation"
```

Skip this step if no documentation change was required.

---

## Spec Coverage Check

- Spec Section 4.1 and 5.2
  - Covered by Task 2 and Task 3 through the async event logger and `events.jsonl` bootstrap path.
- Spec Section 5.4 and 5.5
  - Covered by Task 1 and Task 2 through the explicit interfaces and thread-model-constrained implementation.
- Spec Section 6.2 to 6.9
  - Covered by Task 1 and Task 2 through `EventEnvelope`, JSONL rendering, and file sink assertions.
- Spec Section 12 Stage 1
  - Covered by Task 1 to Task 4 with unit tests, Linux build commands, runtime artifact validation, and commit boundaries.
- Spec Section 14.2 negative testing
  - Not fully implemented in Stage 1. Queue-full and sink-failure degradation remain Stage 2+ follow-up work and should be called out as residual scope when executing this plan.

## Self-Review Notes

- No placeholders such as `TODO`, `TBD`, or “similar to Task N” remain in the actionable tasks.
- All code identifiers referenced in later tasks are introduced earlier:
  - `EventEnvelope`
  - `EventLoggerConfig`
  - `EventLogger`
  - `default_events_log_path`
- Stage 1 scope stays intentionally narrow:
  - No `status_aggregator`
  - No traffic state machine
  - No summary renderer rewrite
  - No debug artifact writer
