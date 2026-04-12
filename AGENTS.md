# AGENTS.md

## Project Identity

- This project is a Linux-only radar receiver demo.
- The long-term goal is to build a complete radar data receiving module.
- Development must proceed in phases. Every phase should have a clear, realistic intermediate goal.
- Do not describe an intermediate implementation as if the final receiver module is already complete.

## Non-Negotiable Execution Rules

- Do not compile, run, benchmark, or validate this project on Windows.
- Do not treat Windows build results, IDE analysis, or local dry-runs as authoritative validation.
- All authoritative build, test, runtime, and integration validation must happen on the Linux server.
- The default remote entrypoint is `ssh kds`.
- When server validation is requested, or when validation is necessary to support a completion claim, use the skill [$server-test-via-kds](C:\Users\Klein\.codex\skills\server-test-via-kds\SKILL.md).
- Follow this repository rule even if local tooling appears to work.

## Development Workflow

- Code reading, editing, refactoring, and documentation updates are performed locally on Windows.
- Build, test, runtime checks, and acceptance validation are performed on the Linux server.
- Sync local changes to the server using either:
  - `git push` + server-side `git pull`, or
  - `scp` / archive copy for isolated validation directories.
- Choose the sync method based on change scope and risk:
  - prefer `git push/pull` for normal branch-based work,
  - prefer isolated copy/sync when validation should not disturb an existing remote workspace.
- Make the sync step explicit before server validation when remote code must reflect local edits.
- Prefer non-destructive remote actions first.
- If `ssh kds` is unavailable before project commands can run, only then consider the WSL fallback path described by the server validation skill and project docs.

## Technical Baseline

- Language standard: `C++17`
- Build system baseline: `CMake 3.16.x`
- Preferred generator: `Ninja`
- Current server validation toolchain constraints:
  - server `cmake 3.16.5` does not support `cmake --preset`; when validating on server, expand preset parameters manually or use repo scripts
  - server `g++ 7.3.0` is part of the active compatibility baseline; do not assume `std::filesystem` is available just because code is compiled with `-std=c++17`
  - when adding path or directory utilities in hot codepaths, prefer compatibility helpers that work on the server baseline instead of newer standard library filesystem APIs
- Current runtime mainline:
  - `DPDK` in `src/receiver`
- Common third-party libraries may include:
  - `libdpdk`
  - `Google Test`
- Shared third-party cache reference:
  - `/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo`
- Do not silently upgrade language standard, build baseline, or dependency assumptions without updating project docs and validation flow.

## Code Organization Principles

- Keep the current mainline centered in `src/receiver`.
- Public headers live in `include/rxtech`.
- Private helper headers should stay in module-local `internal/` directories.
- Place new or changed logic in the smallest module that matches its responsibility:
  - `app`: process entry, CLI, startup assembly
  - `runtime`: config, context, run loop setup, output path preparation
  - `ingress`: backend-specific packet acquisition
  - `core`: hot-path orchestration
  - `protocol`: UDP payload assembly, parsing, validation, sequence interpretation
  - `sidecar`: metrics and observation
  - `storage`: CPI context, slot write, progress tracking
  - `admit`: CPI admission policy
  - `finalize`: CPI finalize and finalized output structure
- Avoid mixing these concerns in one file when extending the system.
- Prefer stable interfaces between layers, such as:
  - `IRxBackend`
  - `ReceiveContext`
  - `RunSummary`
  - parser/validator result types
- Keep the receiver mainline understandable as a pipeline:
  - acquire packets
  - extract UDP payload
  - parse
  - validate
  - record
  - summarize

## Runtime And Validation Rules

- Treat the current DPDK receiver path as the only active runtime path unless project code and docs explicitly change that.
- Do not reintroduce AF_XDP assumptions into mainline docs, config semantics, or runtime status fields without explicit user direction.
- Treat fake tests, parser tests, and isolated unit tests as useful evidence, but not as proof of real network-path success.
- For server validation commands derived from `CMakePresets.json`, remember that the preset file is authoritative for parameters but may not be invokable directly on the server because of the `cmake 3.16.5` baseline.
- Only claim “real closed-loop validation” when the required external sender, network path, and receiver runtime are actually validated on the server.
- If the external sender is unavailable, say so explicitly and limit claims to the level that was truly validated.
- When build or test guidance is needed, prefer active project docs and current repository instructions over historical planning text.
- When docs conflict, prefer the more specific and actively maintained build/deployment/validation guidance.

## Documentation Rules

- Server environment baseline references:
  - [docs/设计方案/服务器环境基线.md](D:\WorkSpace\Company\Tower\rx_tech_demo\docs\设计方案\服务器环境基线.md)
  - [docs/设计方案/平台与环境适配说明.md](D:\WorkSpace\Company\Tower\rx_tech_demo\docs\设计方案\平台与环境适配说明.md)
- These environment/baseline documents may be updated as the real platform and setup evolve.
- Code-logic summary document:
  - [docs/当前接收端代码实现与执行逻辑详解.md](D:\WorkSpace\Company\Tower\rx_tech_demo\docs\当前接收端代码实现与执行逻辑详解.md)
- Keep `README.md` aligned with:
  - current mainline entrypoints
  - current source layout
  - real build/test path
  - actual packet pipeline
  - current validation boundary
- Do not write future plans, ideal architecture, or intended end-state as if they are already implemented.

## Git And Branching Guidance

- Keep `main` focused on real product code and necessary project docs.
- Use separate branches for:
  - staged feature work
  - reading/annotation work
  - experimental validation work if needed
- Keep commits logically grouped:
  - behavior change
  - build/test wiring
  - docs
  - annotation-only changes

## Agent Behavior In This Repository

- Before making implementation or validation decisions, inspect the current repository state and the active docs relevant to the task.
- Prefer concise, enforceable project rules over broad generic advice.
- When uncertain, bias toward:
  - preserving Linux-server-first validation
  - keeping the DPDK receiver pipeline boundaries clear
  - reporting validated facts rather than assumptions
  - distinguishing current mainline from removed historical paths
