# 可观测性重构服务器验证清单

## 2026-04-12 执行记录

- 执行入口：`ssh kds`
- 本地分支：`codex/observability-logging-stage1`
- 本地提交：`067818e386fcb40a8f9cbadb03ef3393241f0ccf`
- 服务器工作区：`/home/devuser/WorkSpace/rx_tech_demo`
- 本次约束：
  - 已先推送当前分支并在服务器拉取
  - 本次没有安排真实 sender 配合
  - 本次不做真实链路接收测试

本次执行结果概览：

- `代码同步`：已完成
- `linux-server-werror` 权威构建：已完成
- `关键 unit 闸口`：已完成
- `fake integration 闸口`：已完成
- `socket loopback 验证`：已完成
- `DPDK / 真实网口路径验证`：未执行，原因是本次无真实 sender 且不做真实链路接收测试

本次中途发现并已修复的构建阻断点：

- 文件：`src/receiver/sidecar/structured_logger.cpp`
- 症状：`std::filesystem` 在服务器 `g++ 7.3.0` 基线下不可用
- 服务器工具链：
  - `cmake 3.16.5`
  - `g++ 7.3.0`
- 额外说明：
  - checklist 原始命令中的 `cmake --preset linux-server-werror` 在该服务器不可直接使用
  - 原因是 `cmake 3.16.5` 不支持 presets
  - 因此已改为按 `CMakePresets.json` 同等参数手动展开配置
  - 修复后已重新构建并通过

## 使用说明

- 本清单用于 `codex/observability-logging-stage1` 分支恢复服务器连通后执行
- 所有权威验证都应在 Linux 服务器进行
- 默认工作目录：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
```

- 若服务器工作区还未同步当前分支，先同步代码，再执行后续步骤

## 一、代码同步

### 1. 检查服务器工作区

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
git status --short --branch
git log --oneline -n 5
```

验证点：

- 当前不在脏工作区上盲跑验证
- 工作区分支与本地待验证分支一致

2026-04-12 结果：

- 服务器初始工作区在 `main`
- 工作区未见脏改动
- 已确认可以安全切换到待验证分支

### 2. 同步待验证分支

执行位置：本地仓库 `/mnt/d/WorkSpace/Company/Tower/rx_tech_demo`

```bash
cd /mnt/d/WorkSpace/Company/Tower/rx_tech_demo
git push origin codex/observability-logging-stage1
```

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
git fetch origin
git checkout codex/observability-logging-stage1
git pull --ff-only origin codex/observability-logging-stage1
```

验证点：

- 服务器 HEAD 与本地待验证提交一致

2026-04-12 结果：

- 本地 `git push gitea codex/observability-logging-stage1` 返回 `Everything up-to-date`
- 服务器已执行：
  - `git fetch gitea`
  - `git checkout codex/observability-logging-stage1`
  - `git pull --ff-only gitea codex/observability-logging-stage1`
- 服务器 HEAD 已对齐到：
  - `067818e386fcb40a8f9cbadb03ef3393241f0ccf`

## 二、服务器构建

### 3. 预设构建

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cmake --preset linux-server-werror
cmake --build --preset linux-server-werror
```

验证点：

- `rx_receiver_socket`
- `rx_receiver_dpdk`
- unit targets
- integration targets

2026-04-12 结果：

- 未直接使用 `cmake --preset linux-server-werror`
- 原因：服务器 `cmake 3.16.5` 不支持 presets
- 已改用等价展开命令：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cmake -S . -B build-preset-werror -G Ninja -DBUILD_TESTS=ON -DBUILD_REPLAY=OFF -DCMAKE_BUILD_TYPE=Debug -DRXTECH_THIRD_PARTY_CACHE=/home/devuser/WorkSpace/ThirdPartyCache -DRXTECH_WARNINGS_AS_ERRORS=ON
cmake --build build-preset-werror
```

- 构建失败，阻断于 `src/receiver/sidecar/structured_logger.cpp`
- 关键错误为：
  - `std::filesystem` 不是命名空间 `std` 中的类型名
  - `std::filesystem::create_directories(...)` 无法解析
- 之后已修复为兼容实现并重新执行同等构建
- 重新执行结果：
  - `rx_receiver_socket` 构建通过
  - `rx_receiver_dpdk` 构建通过
  - unit targets 构建通过
  - integration targets 构建通过

### 4. 若 preset 不可用，走仓库脚本

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/compile/server_shared_cache.sh
```

验证点：

- 与当前服务器第三方缓存兼容

2026-04-12 结果：

- 本次未继续切到 `server_shared_cache.sh`
- 原因：
  - 当前阻断点是源码对服务器 `g++ 7.3.0` 的 `std::filesystem` 兼容问题
  - 切换到脚本构建不改变该编译器基线，不能解除当前阻断

## 三、服务器测试

### 5. 运行关键 unit 闸口

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo/build-preset-werror/tests/unit`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/build-preset-werror/tests/unit
ctest --output-on-failure -R "test_event_logger|test_summary_renderer|test_traffic_state_tracker|test_debug_capture_writer|test_owner_loop_summary|test_rx_config|test_metrics_exporter"
```

验证点：

- 事件层
- release summary
- traffic state
- capture policy
- heavy recorder observability

2026-04-12 结果：

- 已执行并通过
- 通过项：
  - `test_event_logger`
  - `test_summary_renderer`
  - `test_traffic_state_tracker`
  - `test_debug_capture_writer`
  - `test_owner_loop_summary`
  - `test_rx_config`
  - `test_metrics_exporter`

### 6. 运行 fake integration 闸口

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo/build-preset-werror/tests/integration`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/build-preset-werror/tests/integration
ctest --output-on-failure -R rxtech_integration_fake_tests
```

验证点：

- fake backend 下的 events/summary/capture/raw_record 语义完整

2026-04-12 结果：

- 已执行并通过：
  - `rxtech_integration_fake_tests`

## 四、socket 路径权威运行验证

### 7. socket loopback 验证

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build-preset-werror/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1
```

2026-04-12 结果：

- 已执行并通过
- 命令：
  - `./build-preset-werror/src/receiver/rx_receiver_socket --config configs/socket_loopback.conf --duration 5 --status-interval 1`
- 运行目录：
  - `results/20260412_113036_socket_loopback`
- 结果：
  - 运行成功
  - 无真实业务流量，统计值为 0 符合本次场景

### 8. 检查运行目录产物

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d -exec test -f '{}/events.jsonl' ';' -print | sort | tail -n 1)"
printf '%s\n' "$LATEST_RUN_DIR"
ls -l "$LATEST_RUN_DIR"
```

验证点：

- `events.jsonl`
- `summary.json`
- `summary.txt`
- capture 相关产物

2026-04-12 结果：

- 已执行
- 目录内已确认存在：
  - `events.jsonl`
  - `summary.json`
  - `summary.txt`
  - `capture_index.csv`
  - `capture_packets.bin`

### 9. 检查事件文件

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d -exec test -f '{}/events.jsonl' ';' -print | sort | tail -n 1)"
grep -n "\"run.header\"\\|\"status.snapshot\"\\|\"traffic.first_seen\"\\|\"traffic.interrupted\"\\|\"traffic.resumed\"\\|\"raw_record.started\"\\|\"raw_record.stopped\"" "$LATEST_RUN_DIR/events.jsonl" || true
```

验证点：

- 至少有 `run.header`
- 至少有 `status.snapshot`
- 若本次没有业务流量，`traffic.*` 事件允许缺失
- 若启用了原始帧录制，`raw_record.started/stopped` 必须存在

2026-04-12 结果：

- 已执行
- 已从运行输出确认：
  - 有 `run.header`
  - 有持续 `status.snapshot`
- 本次无真实业务流量，因此：
  - `traffic.first_seen`
  - `traffic.interrupted`
  - `traffic.resumed`
  缺失符合预期

### 10. 检查 summary JSON

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d -exec test -f '{}/summary.json' ';' -print | sort | tail -n 1)"
python3 - <<'PY'
import json, pathlib
runs = sorted([p for p in pathlib.Path("results").iterdir() if p.is_dir() and p.joinpath("summary.json").exists()])
latest = runs[-1]
summary = json.loads(latest.joinpath("summary.json").read_text(encoding="utf-8"))
assert summary["header"]["backend"] in {"socket", "dpdk"}
assert "capture_policy" in summary["summary"]["capture"]
print(summary["summary"]["capture"]["capture_policy"])
print(summary["summary"]["capture"].get("raw_record_role", ""))
PY
```

验证点：

- `capture_policy`
- `raw_record_role`
- header / summary 基本结构可解析

2026-04-12 结果：

- 已执行
- 已确认：
  - `header.backend == "socket"`
  - `summary.capture.capture_policy == "first_effective_cpi"`
- 本次 socket loopback 运行未启用 raw record，因此 `raw_record_role` 不作为本步通过前提

### 11. 检查 summary 文本

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d -exec test -f '{}/summary.txt' ';' -print | sort | tail -n 1)"
sed -n '1,40p' "$LATEST_RUN_DIR/summary.txt"
```

验证点：

- 有 `运行头部`
- 若启用原始帧录制，有 `原始帧录制角色`
- 有 `原始帧录制定位： 重型专项录制`

2026-04-12 结果：

- 已执行
- 已确认：
  - 有 `运行头部`
  - 有接收结束汇总
  - 当前为 socket 无流量场景，文本结果与运行统计一致

## 五、DPDK / 真实网口路径验证

### 12. DPDK 固定时长运行

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build-preset-werror/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf --duration 30
```

验证点：

- 程序可启动
- 运行目录产物完整
- 若启用原始帧录制，其事件与 summary 记录齐全

2026-04-12 结果：

- 未执行
- 原因：本次没有安排真实 sender，且明确不做真实链路接收测试

### 13. 持续运行 + sender 中断/恢复联调

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./build-preset-werror/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf --run-until-stopped --status-interval 1
```

联调时人工观察与检查项：

- sender 首次开始发流后：
  - 事件里出现 `traffic.first_seen`
  - panel 进入“业务流状态： 正常”
- sender 中断后：
  - 事件里出现 `traffic.interrupted`
  - panel 进入“业务流状态： 中断”
  - 有“最近中断时间”
- sender 恢复后：
  - 事件里出现 `traffic.resumed`
  - panel 回到“业务流状态： 正常”
  - 有“最近恢复时间”

2026-04-12 结果：

- 未执行
- 原因：本次没有安排真实 sender，且明确不做真实链路接收测试

## 六、重型专项录制专项验证

### 14. 启用 raw_record 的专项检查

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
LATEST_RUN_DIR="$(find results -mindepth 1 -maxdepth 1 -type d -exec test -f '{}/events.jsonl' ';' -print | sort | tail -n 1)"
grep -n "\"raw_record.started\"\\|\"raw_record.failed\"\\|\"raw_record.stopped\"" "$LATEST_RUN_DIR/events.jsonl" || true
python3 - <<'PY'
import json, pathlib
runs = sorted([p for p in pathlib.Path("results").iterdir() if p.is_dir() and p.joinpath("summary.json").exists()])
latest = runs[-1]
summary = json.loads(latest.joinpath("summary.json").read_text(encoding="utf-8"))
print(summary["summary"]["capture"].get("raw_record_role", ""))
print(summary["summary"]["capture"].get("raw_record_output_dir", ""))
print(summary["summary"]["capture"].get("raw_record_latest_file_path", ""))
PY
```

验证点：

- `raw_record.started`
- `raw_record.stopped`
- 若失败则有 `raw_record.failed`
- summary 中有 `raw_record_role = heavy_debug_recorder`

2026-04-12 结果：

- 未执行
- 原因：没有新的服务器运行目录产物

### 15. retention / 分段检查

执行位置：Linux 服务器 `raw_record_output_dir`

```bash
cd /data/rx_tech_demo/raw_frames
ls -lh | tail -n 20
```

验证点：

- `.rawbin` 文件持续按分段生成
- 不出现无限制增长
- `latest_file_path` 与目录实际文件一致

2026-04-12 结果：

- 未执行
- 原因：本次未进入 raw_record 运行验证阶段

## 七、验证结论记录模板

服务器验证完成后，至少记录以下结论：

- 哪些验证是在 WSL fallback 完成的
- 哪些验证是在 Linux 服务器权威环境完成的
- 哪些验证涉及真实 sender / 真实 10G NIC
- `traffic.interrupted/resumed` 是否已在真实链路验证
- `first_effective_cpi` 是否已在真实业务流量下验证
- `raw_record.started/stopped/failed` 是否已在真实数据面验证
