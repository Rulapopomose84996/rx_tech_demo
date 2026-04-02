# rx_tech_demo

`rx_tech_demo` 是一个 Linux-only 的雷达接收端演示工程。当前主线实现位于 `src/receiver`，主入口是基于 DPDK 的轻量接收链路，而不是旧的 AF_XDP benchmark 叙述。

## 当前定位

- 当前目标是把真实网卡收包、样本协议解析、轻量统计和旁路落盘串成一条稳定接收链。
- 当前主线后端是 DPDK。
- `src/legacy` 下的 AF_XDP 代码与脚本仅作为兼容/历史参考。
- 当前实现已经支持：
  - DPDK 收包
  - IPv4/UDP payload 提取与 IP 分片重组
  - 样本协议解析和校验
  - 实时落盘到 `capture_packets.bin` / `capture_index.csv`
  - 终端状态输出和中文汇总

## 当前源码结构

```text
src/
  receiver/
    app/
    core/
    ingress/dpdk/
    protocol/
    runtime/
    sidecar/
  legacy/
    af_xdp/
```

说明：

- `src/receiver` 是当前产品化主线。
- `src/receiver/ingress/dpdk` 是当前唯一主接收实现。
- `src/receiver/core/owner_loop` 负责把收包、解析、统计、落盘串起来。
- `src/receiver/protocol` 负责 UDP payload 组装、样本头解析、校验和协议序列解释。
- `src/receiver/runtime` 负责配置加载、运行器和输出路径准备。

## 当前接收链路

当前代码里的主线大致是：

1. `rx_receiver_dpdk` / `rxbench_dpdk` 启动
2. 解析 CLI，加载并合并配置
3. 初始化 DPDK backend
4. 从网卡批量取包，必要时自动应答 ARP
5. 从以太网帧中提取 IPv4/UDP payload，并按 IP 分片做重组
6. 解析当前样本协议头：
   - 控制表包 magic：`0x55AAFF00`
   - 数据包 magic：`0x55AAFF03`
7. 做轻量协议校验和顺序解释
8. 把通过当前链路的 UDP payload 立即写入：
   - `capture_packets.bin`
   - `capture_index.csv`
9. 输出单行统计与人类可读汇总

## 输出文件

默认输出目录是 `results`，也可以通过配置文件或 `--output` 覆盖。

当前主线会写出：

- `capture_packets.bin`
- `capture_index.csv`

这两个文件是运行中实时写入的，不是“按暂停后一次性导出”。

## 当前配置

当前仓库里仍保留几份配置文件：

- `configs/dpdk_receiver0_replay.conf`
- `configs/dpdk_single_face.conf`
- `configs/af_xdp_receiver0.conf`
- `configs/af_xdp_single_face.conf`

其中应优先以 DPDK 配置作为当前主线参考。AF_XDP 配置只代表历史路径，不应再默认当作当前推荐入口。

## 服务器构建

Linux server，执行目录：`/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/build_server_shared_cache.sh
```

## 服务器测试

Linux server，执行目录：`/home/devuser/WorkSpace/rx_tech_demo/build`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/build
ctest --output-on-failure
```

## 当前可执行入口

当前主入口：

- `rx_receiver_dpdk`
- `rxbench_dpdk`

Legacy 入口仍可能存在于 `src/legacy`，但不作为当前主线说明和验证依据。

## 验证边界

- Windows 侧只用于读代码、改代码、改文档。
- 权威构建、测试和联调验证必须在 Linux 服务器上完成。
- 如果没有经过服务器验证，不应把本地观察、IDE 分析或文档推断写成“已验证事实”。
