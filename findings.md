# Findings

## Current Mainline

- 当前产品化主线位于 `src/receiver`，主后端是 DPDK。
- `src/legacy/af_xdp` 仍保留为兼容/历史参考路径，不再作为当前主线叙述。
- 当前 Phase 3 已落地的核心能力是：配置重构、协议参数化、sidecar 简化、热路径骨架接入。

## Phase 3 State

- Phase B 已完成：CLI 只保留 `--config`、`--dry-run`、`--help`，运行参数通过 section 化配置文件提供。
- Phase A 已完成：capture index 简化为语义字段，sidecar 指标从端口/分片细节收敛到协议包统计。
- Phase C 已完成：协议侧已引入 `ProtocolSpec`，解析/校验/序列解释统一基于当前协议参数工作。
- Phase D 已完成：`storage`、`admit`、`output`、`finalize` 骨架已接入 owner loop。

## Runtime Facts

- 启动入口：`rx_receiver_dpdk` 和 `rxbench_dpdk`。
- 当前主循环处理顺序是：收包 -> UDP payload 提取 -> 解析 -> 校验 -> 准入 -> slot 写入 -> 进度推进 -> finalizer -> 统计/落盘。
- 当前 capture index 表头已经简化为：`cpi,channel,prt,packet_index,packet_kind,payload_len,valid`。
- 当前配置里的协议默认值仍是：`udp_packet_size=2048`、`channels_per_prt=3`、`packets_per_channel=9`。
- 第 9 个数据包仍要求只有前 `476 * 4` 字节是有效 IQ 数据，后续补零必须全为 0。

## Sender And Validation Tools

- `tools/raw_eth_sender.py` 只适合做 ingress/过滤层面的原始 UDP 烟雾测试，不保证通过当前协议解析链路。
- 新增 `tools/rxtech_protocol_sender.py`，按当前 Phase 3 协议格式发送控制表包和数据包，可直接用于真实接收解析联调。

## Validation Boundary

- Windows 仅用于阅读、编辑、提交代码和文档。
- 权威构建与测试结果必须来自 Linux 服务器。
- 本轮权威验证在 `ssh kds` 的隔离目录 `/home/devuser/WorkSpace/rx_tech_demo_phase3_infra_validation` 完成。

## Verified Results

- 服务器重新构建通过，`RXTECH_THIRD_PARTY_CACHE` 未使用告警已消失。
- legacy AF_XDP 目标的 libbpf deprecation 告警已局部压制，不影响主线 DPDK 目标。
- 服务器单元测试通过：11/11。
- 服务器集成测试通过：1/1。
- 新增 `tools/rxtech_protocol_sender.py` 已在服务器通过 `python3 -m py_compile` 语法检查。

## Notes

- `test_arp_responder` 之前在 Release 构建下被 `assert` 掩盖了真实失败，本轮已修正为真实检查，并修复了测试中的 IP 端序错误。
- `CpiContextPool` 仍保持堆分配，这一点已经在之前的服务器联调中证明是必要的，不能回退到栈上分配。
