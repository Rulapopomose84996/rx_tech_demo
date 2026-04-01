# Findings

## Replay Sender Direction

- `data/cpi_0002_complete/cpi_0002_replay_manifest.json` 已包含顺序化重放信息。
- manifest 中每个条目至少有：
  - `sequence`
  - `kind`
  - `file`
  - `offset_bytes`
  - `length_bytes`
  - 对数据包还带 `cpi / prt / channel / channel_name / packet_index`
- 当前样本目录中真正参与重放的文件是：
  - `cpi_0002_control_table.bin`
  - `cpi_0002_data_payloads.bin`
- manifest 是现成的重放事实源，因此 sender 不需要猜测协议切片规则。
- sender 当前已经具备两层可复用能力：
  - `load_replay_manifest(path)`
  - `build_replay_plan(unit_dir, manifest)`
- 下一步只需要补 Linux UDP 发送薄壳，即可形成最小 `replay_sender` 可执行。
- 样本协议与当前仓库旧 parser 使用的 `TPDX` 头并不一致：
  - 控制表包头前 4 字节为 `00 ff aa 55`，即小端 `0x55AAFF00`
  - 数据包头前 4 字节为 `03 ff aa 55`，即小端 `0x55AAFF03`
  - 数据包采用 16 字节信息头：`magic/cpi/channel/prt/packet_index/tail`
- 这意味着当前闭环要优先支持的是“样本协议轻量解析”，而不是继续沿用历史 `TPDX` 风格字段模型。

## Receiver Baseline

- 当前 `src/receiver` 已有：
  - `ingress/dpdk`
  - `protocol/parser`
  - `protocol/packet_validator`
  - `core/owner_loop`
  - 一些更重的 CPI 生命周期雏形
- 用户最新目标明确要求：
  - receiver 只保留 `DPDK ingress + PacketParser + PacketValidator + 轻量统计`
  - 当前不以完整 CPI 生命周期闭环为成功标准

## Platform Constraint

- 用户明确指出项目是 Linux-only，不能继续引入 Windows 平台代码。
- `docs/设计方案/平台与环境适配说明.md` 也明确目标平台是 `Kylin Linux Advanced Server V10 (GFB), aarch64`。

## Validation Workflow

- 最终权威验证仍应通过 `ssh kds` 在 `/home/devuser/WorkSpace/rx_tech_demo` 上完成。
- 服务器构建命令继续使用仓库已有脚本：
  - `bash ./scripts/build_server_shared_cache.sh`
- 服务器测试命令继续使用：
  - `cd /home/devuser/WorkSpace/rx_tech_demo/build && ctest --output-on-failure`
