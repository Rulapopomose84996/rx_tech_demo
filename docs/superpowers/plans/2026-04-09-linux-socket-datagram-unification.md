# Linux Socket Datagram Unification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current frame-first ingress boundary with a UDP-datagram-first boundary so DPDK and Linux Socket share the same protocol pipeline while Socket stops building synthetic Ethernet/IPv4/UDP frames.

**Architecture:** Introduce stable datagram descriptors and burst lifecycle primitives, then migrate protocol processing to consume datagrams directly. Keep DPDK-specific frame handling inside the DPDK ingress path, upgrade the Linux Socket ingress to a real datagram ingress with `recvmmsg()` plus a fixed arena, and expand unified backend stats so both paths can be compared under the same runtime and summary surfaces.

**Tech Stack:** C++17, CMake 3.16.x, Ninja, existing `rx_receiver_core` / ingress unit harnesses, Linux server validation via `ssh kds`

---

## File Map

- Create: `D:\WorkSpace\Company\Tower\rx_tech_demo\include\rxtech\udp_datagram.h`
  - Define `UdpDatagramDesc`, `UdpDatagramBurst`, `BackendKind`, and shared lifecycle metadata.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\include\rxtech\rx_backend.h`
  - Change the ingress contract from `RxBurst` to `UdpDatagramBurst`.
  - Expand `BackendStats` with burst-size and datagram-specific counters.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\include\rxtech\packet_desc.h`
  - Keep only the raw-frame descriptor types needed by frame-native paths and sidecars.
- Create: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\protocol\udp_datagram_pipeline.h`
  - Declare the datagram-first pipeline and processed packet output types.
- Create: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\protocol\udp_datagram_pipeline.cpp`
  - Implement packet filtering, parser/validator/interpreter entry, and processed datagram callbacks.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\protocol\packet_pipeline.h`
  - Shrink this header into a transitional adapter or remove frame-only responsibilities if no longer needed.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\protocol\packet_pipeline.cpp`
  - Keep only DPDK frame adaptation glue or thin compatibility logic.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\protocol\udp_payload_assembler.cpp`
  - Expose reusable frame-to-datagram extraction helpers instead of owning the only protocol entrypoint.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\owner_loop.cpp`
  - Swap `PacketPipeline` usage over to the new datagram pipeline.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\owner_loop_summary.cpp`
  - Merge new backend stats into `RunSummary`.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\sidecar\status_panel.cpp`
  - Render new burst/drop/kernel-drop diagnostics.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\ingress\dpdk\internal\dpdk_backend.h`
  - Update signatures and add datagram adapter helpers.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\ingress\dpdk\dpdk_backend.cpp`
  - Convert raw frames to datagrams inside ingress and preserve ARP handling plus mbuf lifecycle.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\ingress\socket\internal\linux_socket_backend.h`
  - Update signatures and declare fixed-arena internals.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\ingress\socket\linux_socket_backend.cpp`
  - Replace `recvfrom()` + synthetic frame construction with `recvmmsg()` + datagram arena.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\CMakeLists.txt`
  - Register datagram-pipeline and adapter tests.
- Create: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_udp_datagram_pipeline.cpp`
  - Cover datagram filtering and protocol equivalence to the current payload path.
- Create: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_dpdk_datagram_adapter.cpp`
  - Cover frame-to-datagram extraction and ARP/non-UDP bypass behavior.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_linux_socket_backend.cpp`
  - Rework the existing test to assert datagram semantics, burst behavior, and stats.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_owner_loop_summary.cpp`
  - Add summary assertions for the new backend counters.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\docs\当前接收端代码实现与执行逻辑详解.md`
  - Update ingress and protocol descriptions to the datagram-first design.
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\README.md`
  - Keep high-level architecture aligned with the new ingress boundary.

### Task 1: Introduce UDP Datagram Types And Backend Contract

**Files:**
- Create: `D:\WorkSpace\Company\Tower\rx_tech_demo\include\rxtech\udp_datagram.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\include\rxtech\rx_backend.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\include\rxtech\packet_desc.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\CMakeLists.txt`
- Create: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_udp_datagram_pipeline.cpp`

- [ ] **Step 1: Write the failing datagram lifecycle test**

Add a new unit test that locks in the datagram shape and burst lifecycle before changing production headers:

```cpp
int main() {
    rxtech::UdpDatagramBurst burst;
    std::array<std::uint8_t, 4> payload{0x03U, 0xFFU, 0xAAU, 0x55U};

    rxtech::UdpDatagramDesc desc;
    desc.payload_data = payload.data();
    desc.payload_len = static_cast<std::uint32_t>(payload.size());
    desc.src_ipv4_be = 0x7F000001U;
    desc.dst_ipv4_be = 0x7F000001U;
    desc.src_port = 40000U;
    desc.dst_port = 9999U;
    desc.backend_kind = rxtech::BackendKind::socket;

    burst.datagrams.push_back(desc);

    if (burst.datagrams.size() != 1U || burst.datagrams.front().payload_len != 4U) {
        return 1;
    }
    burst.datagrams.clear();
    return burst.datagrams.empty() ? 0 : 1;
}
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_udp_datagram_pipeline
```

Expected: compile or link failure because `UdpDatagramBurst`, `UdpDatagramDesc`, and `BackendKind` do not exist yet.

- [ ] **Step 3: Add the minimal datagram types and backend interface**

Define the new types and update the ingress interface:

```cpp
enum class BackendKind : std::uint8_t { unknown = 0, dpdk, socket, file_replay };

struct UdpDatagramDesc {
    const std::uint8_t* payload_data = nullptr;
    std::uint32_t payload_len = 0;
    std::uint32_t src_ipv4_be = 0;
    std::uint32_t dst_ipv4_be = 0;
    std::uint16_t src_port = 0;
    std::uint16_t dst_port = 0;
    std::uint64_t ts_ns = 0;
    std::uint32_t queue_id = 0;
    std::uintptr_t cookie = 0;
    BackendKind backend_kind = BackendKind::unknown;
    bool truncated = false;
};

struct UdpDatagramBurst {
    std::vector<UdpDatagramDesc> datagrams;
};
```

Update `IRxBackend::recv_burst(...)` and `IRxBackend::release_burst(...)` to use `UdpDatagramBurst`.

- [ ] **Step 4: Run the targeted test to verify it passes**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_udp_datagram_pipeline
```

Expected: `test_udp_datagram_pipeline` passes and no unrelated target fails to compile due to stale `RxBurst` usage in headers.

- [ ] **Step 5: Commit the contract change**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design"
git add include/rxtech/udp_datagram.h include/rxtech/rx_backend.h include/rxtech/packet_desc.h tests/unit/CMakeLists.txt tests/unit/test_udp_datagram_pipeline.cpp
git commit -m "refactor: add udp datagram ingress contract"
```

### Task 2: Build The Datagram-First Protocol Pipeline

**Files:**
- Create: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\protocol\udp_datagram_pipeline.h`
- Create: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\protocol\udp_datagram_pipeline.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\protocol\packet_pipeline.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\protocol\packet_pipeline.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\owner_loop.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_udp_datagram_pipeline.cpp`

- [ ] **Step 1: Extend the failing test to prove datagram and legacy payload paths behave identically**

Add a test that feeds the same UDP payload through the new pipeline entry and asserts:

```cpp
const auto stats = pipeline.process_datagram(datagram, metrics, nullptr, invalid_dumped, [&](const rxtech::ProcessedPacket& processed) {
    callback_count++;
    observed_kind = processed.interpreted.kind;
    observed_payload.assign(processed.udp_frame.udp_payload.begin(), processed.udp_frame.udp_payload.end());
});

assert(stats.accepted_packets == 1U);
assert(callback_count == 1U);
assert(observed_kind == rxtech::PacketKind::data_packet);
assert(observed_payload.size() == 2048U);
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_udp_datagram_pipeline
```

Expected: failure because `process_datagram(...)` and the datagram-first pipeline do not exist yet.

- [ ] **Step 3: Implement the minimal datagram-first protocol path**

Create `UdpDatagramPipeline` and keep the current parser/validator/interpreter flow intact:

```cpp
PacketProcessStats UdpDatagramPipeline::process_datagram(
    const UdpDatagramDesc& datagram,
    IMetricsCollector& metrics,
    std::ostream* diagnostic_output,
    std::uint32_t& invalid_dumped,
    const std::function<void(const ProcessedPacket&)>& on_packet);
```

The implementation should:

- build `UdpPayloadFrame` directly from `UdpDatagramDesc`
- reuse existing filter semantics
- reuse the parser, validator, and interpreter unchanged
- preserve callback payloads and source metadata

Update `OwnerLoop` to call the datagram pipeline instead of pushing raw frames through `PacketPipeline`.

- [ ] **Step 4: Run the targeted tests to verify they pass**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R "test_udp_datagram_pipeline|test_sample_packet_parser|test_sample_packet_validator|test_protocol_sequence_interpreter"
```

Expected: the new pipeline test passes and the core protocol tests remain green.

- [ ] **Step 5: Commit the pipeline migration**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design"
git add src/receiver/protocol/udp_datagram_pipeline.h src/receiver/protocol/udp_datagram_pipeline.cpp src/receiver/protocol/packet_pipeline.h src/receiver/protocol/packet_pipeline.cpp src/receiver/core/owner_loop.cpp tests/unit/test_udp_datagram_pipeline.cpp
git commit -m "refactor: add udp datagram protocol pipeline"
```

### Task 3: Move DPDK To Internal Frame-To-Datagram Adaptation

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\ingress\dpdk\internal\dpdk_backend.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\ingress\dpdk\dpdk_backend.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\protocol\udp_payload_assembler.cpp`
- Create: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_dpdk_datagram_adapter.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\CMakeLists.txt`

- [ ] **Step 1: Write the failing DPDK adapter tests**

Create tests that lock in three behaviors:

```cpp
// UDP frame becomes exactly one datagram.
assert(result.datagrams.size() == 1U);
assert(result.datagrams.front().dst_port == 9999U);

// ARP frame never becomes a datagram.
assert(result.datagrams.empty());
assert(stats.arp_request_packets == 1U);

// Non-UDP IPv4 frame never leaks into protocol pipeline.
assert(result.datagrams.empty());
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R "test_dpdk_datagram_adapter|test_arp_responder"
```

Expected: the new adapter test target fails to compile or link because the DPDK datagram path is not implemented.

- [ ] **Step 3: Implement the minimal DPDK datagram adapter**

Refactor the DPDK ingress so `recv_burst(...)` returns datagrams:

```cpp
if (impl_->maybe_reply_arp(mbuf)) {
    rte_pktmbuf_free(mbuf);
    continue;
}

UdpDatagramDesc datagram;
if (!impl_->extract_udp_datagram(mbuf, datagram)) {
    rte_pktmbuf_free(mbuf);
    continue;
}

datagram.cookie = reinterpret_cast<std::uintptr_t>(mbuf);
datagram.backend_kind = BackendKind::dpdk;
burst.datagrams.push_back(datagram);
```

Keep raw-frame parsing private to the DPDK ingress or its helper adapter. Do not push raw frames back into the protocol layer.

- [ ] **Step 4: Run the targeted tests to verify they pass**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R "test_dpdk_datagram_adapter|test_arp_responder|test_udp_datagram_pipeline"
```

Expected: the adapter tests pass and no regression appears in datagram protocol processing.

- [ ] **Step 5: Commit the DPDK ingress migration**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design"
git add src/receiver/ingress/dpdk/internal/dpdk_backend.h src/receiver/ingress/dpdk/dpdk_backend.cpp src/receiver/protocol/udp_payload_assembler.cpp tests/unit/CMakeLists.txt tests/unit/test_dpdk_datagram_adapter.cpp
git commit -m "refactor: adapt dpdk ingress to udp datagrams"
```

### Task 4: Upgrade Linux Socket Ingress To Real Datagram Burst I/O

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\ingress\socket\internal\linux_socket_backend.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\ingress\socket\linux_socket_backend.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_linux_socket_backend.cpp`

- [ ] **Step 1: Rewrite the socket backend test so it expects datagrams, not synthetic frames**

Update the existing Linux-only test to assert:

```cpp
rxtech::UdpDatagramBurst burst;
assert(backend.recv_burst(burst, 4U));
assert(burst.datagrams.size() == 1U);
assert(burst.datagrams.front().payload_len == 2048U);
assert(burst.datagrams.front().src_ipv4_be == 0x7F000001U);
assert(burst.datagrams.front().dst_port == port);
```

Also add assertions for at least one burst metric:

```cpp
const auto stats = backend.stats();
assert(stats.receive_batches >= 1U);
assert(stats.max_burst_size >= 1U);
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_linux_socket_backend
```

Expected: failure because the socket ingress still emits frame-shaped packets and lacks the new stats.

- [ ] **Step 3: Implement the minimal datagram-first socket ingress**

Refactor the Linux socket ingress internals around a fixed arena and `recvmmsg()`:

```cpp
struct SocketSlot {
    std::array<std::uint8_t, kMaxUdpPayloadBytes> payload{};
    sockaddr_in peer{};
    in_pktinfo pktinfo{};
    std::uint32_t payload_len = 0;
    bool truncated = false;
};

const int received = ::recvmmsg(impl_->socket_fd, impl_->msgs.data(), budget, flags, timeout_ptr);
```

Fill `UdpDatagramDesc` directly from each slot, attach `BackendKind::socket`, and remove all synthetic Ethernet/IPv4/UDP frame construction.

- [ ] **Step 4: Run the targeted tests to verify they pass**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R "test_linux_socket_backend|test_udp_datagram_pipeline"
```

Expected: socket backend tests pass without any synthetic frame dependency.

- [ ] **Step 5: Commit the socket ingress upgrade**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design"
git add src/receiver/ingress/socket/internal/linux_socket_backend.h src/receiver/ingress/socket/linux_socket_backend.cpp tests/unit/test_linux_socket_backend.cpp
git commit -m "refactor: make linux socket ingress datagram-first"
```

### Task 5: Expand Unified Stats And Runtime Reporting

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\include\rxtech\rx_backend.h`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\core\owner_loop_summary.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\src\receiver\sidecar\status_panel.cpp`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\tests\unit\test_owner_loop_summary.cpp`

- [ ] **Step 1: Write the failing summary test for datagram stats**

Extend `test_owner_loop_summary.cpp` to lock in new fields:

```cpp
rxtech::BackendStats backend{};
backend.receive_batches = 12U;
backend.max_burst_size = 6U;
backend.kernel_drop_count = 9U;

rxtech::RunSummary summary{};
rxtech::merge_backend_stats(summary, backend);

assert(summary.backend_receive_batches == 12U);
assert(summary.backend_max_burst_size == 6U);
assert(summary.backend_kernel_drops == 9U);
```

- [ ] **Step 2: Run the targeted test to verify it fails**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R test_owner_loop_summary
```

Expected: failure because the new summary fields and stat merges do not exist yet.

- [ ] **Step 3: Implement the minimal stats merge and rendering changes**

Add the fields to `BackendStats` and propagate them into runtime reporting:

```cpp
summary.backend_receive_batches = backend_stats.receive_batches;
summary.backend_max_burst_size = backend_stats.max_burst_size;
summary.backend_kernel_drops = backend_stats.kernel_drop_count;
```

Surface them in `status_panel.cpp` with labels that distinguish kernel drops from protocol drops.

- [ ] **Step 4: Run the targeted tests to verify they pass**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure -R "test_owner_loop_summary|test_metrics"
```

Expected: summary tests pass and metrics tests remain green.

- [ ] **Step 5: Commit the stats/reporting update**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design"
git add include/rxtech/rx_backend.h src/receiver/core/owner_loop_summary.cpp src/receiver/sidecar/status_panel.cpp tests/unit/test_owner_loop_summary.cpp
git commit -m "feat: report unified datagram backend stats"
```

### Task 6: Update Documentation And Run Authoritative Linux Validation

**Files:**
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\README.md`
- Modify: `D:\WorkSpace\Company\Tower\rx_tech_demo\docs\当前接收端代码实现与执行逻辑详解.md`

- [ ] **Step 1: Write the failing documentation checklist**

Before editing docs, make a checklist in the commit message draft or task notes that rejects these stale statements:

- "Socket backend constructs synthetic Ethernet/IPv4/UDP frame as the main path"
- "System-wide unified ingress boundary is raw frame"
- "Socket backend is only a minimal compatibility path"

- [ ] **Step 2: Update the docs to the datagram-first architecture**

Refresh README and the detailed implementation document so they state:

- DPDK handles frame-native adaptation internally
- Socket ingress is a datagram-first ingress
- protocol/CPI logic consumes UDP datagrams, not synthetic frames
- DPDK remains the current real-NIC authority path

- [ ] **Step 3: Run the full unit suite on the Linux server**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
cd /home/devuser/WorkSpace/rx_tech_demo/build/tests/unit
ctest --output-on-failure
```

Expected: the full unit suite passes, or any unrelated pre-existing failure is recorded explicitly before proceeding.

- [ ] **Step 4: Run the socket runtime smoke validation on the Linux server**

Execution location: Linux server via `ssh kds`, repository root `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1
```

What it verifies: the socket ingress still completes a Linux loopback runtime pass after the datagram-first migration and reports unified backend stats from the runtime surfaces.

- [ ] **Step 5: Commit the docs and validation notes**

Execution location: Windows PowerShell, worktree root `D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo\.worktrees\codex-linux-socket-datagram-design"
git add README.md docs/当前接收端代码实现与执行逻辑详解.md
git commit -m "docs: describe datagram-first ingress architecture"
```

## Notes For The Implementer

- Keep the hot path allocation-free after burst setup. The first regression check is whether Socket still resizes or reallocates per packet.
- Do not move protocol parsing into either backend. The backends only own ingress and datagram shaping.
- If the DPDK adapter needs a raw-frame helper type, keep it private to the DPDK ingress subtree rather than promoting it back into the protocol-wide interface.
- Treat server validation as authoritative. Windows compile or IDE success does not count.
