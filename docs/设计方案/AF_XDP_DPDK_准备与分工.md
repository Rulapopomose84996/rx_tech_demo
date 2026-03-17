# AF_XDP / DPDK 已完成配置总结

本文档自 `2026-03-17` 起作为 `rx_tech_demo` 在 `AF_XDP` / `DPDK` 方向上的已完成配置总结文档。

文档定位：

- 作为本项目 `AF_XDP` / `DPDK` 基线文档
- 作为后续环境核对、压测执行、回归复查的真实源
- 如与 `README.md`、`docs/设计方案/平台与环境适配说明.md` 等文档出现表述冲突，以本文件为准

## 1. 已确认管理结论

截至 `2026-03-17`，以下事项已明确确认：

- `enP1s25f3` 已确认可长期作为专用实验口
- 已确认进入正式 `DPDK` 压测窗口
- 已确认允许继续对 `enP1s25f3` 执行驱动重绑
- 驱动重绑仅允许在 `enP1s25f3` 上执行，不扩展到其他 X710 端口
- `AF_XDP` 依赖策略已固化为：`libbpf` 系统安装 + `libxdp` 共享缓存前缀
- `DPDK` 依赖策略已固化为：共享缓存离线化 + 项目自管版本

## 2. 已完成环境与依赖基线

### 2.1 工作区与共享缓存

- 服务器工作区：`/home/devuser/WorkSpace/rx_tech_demo`
- 共享缓存根目录：`/home/devuser/WorkSpace/ThirdPartyCache`
- 项目缓存命名空间：`/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo`
- 当前已建立目录：
  - `/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/archives`
  - `/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64`

### 2.2 AF_XDP 依赖基线

- `libbpf`：系统安装，版本 `0.8.1`
- `libxdp`：共享缓存前缀安装，版本 `1.2.9`
- `libxdp` 前缀目录：`/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix`
- `libxdp` 来源：`xdp-tools 1.2.9`
- `libxdp` 构建工具链：
  - `CLANG=/usr/bin/clang-10`
  - `LLC=/usr/bin/llc`
- 说明：
  - 默认系统路径下 `pkg-config --modversion libxdp` 仍不会返回版本
  - 使用 `libxdp` 相关自检、构建或验证时，必须先导出前缀环境

推荐环境变量：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
```

### 2.3 DPDK 依赖基线

- `libdpdk`：`19.11.14`
- `dpdk-devbind.py`：已安装到 `/usr/local/bin/dpdk-devbind.py`
- `dpdk-testpmd`：已安装到 `/usr/local/bin/dpdk-testpmd`
- `dpdk-hugepages.py`：当前仍未提供，不阻断现阶段闭环

### 2.4 平台与工具链基线

- 操作系统：`Kylin Linux Advanced Server V10 (GFB)`
- 内核：`4.19.90-52.23.v2207.gfb08.ky10.aarch64`
- GCC：`7.3.0`
- CMake：`3.16.5`
- `/usr/bin/clang-10`：可用于最小 `.bpf.o` 构建
- `/usr/bin/llc`：`10.0.1`，已确认支持 `bpf` target
- `/usr/local/corex/bin/clang`：不用于 `BPF` 目标构建
- `/usr/local/corex/bin/llc`：不用于 `libxdp` 构建

## 3. 已完成端口与资源基线

### 3.1 实验口基线

- `AF_XDP` 测试口：`enP1s25f3`
- `DPDK` 解绑测试口：`enP1s25f3`
- 对应 `BDF`：`0001:05:00.3`
- 网卡：Intel X710
- 内核驱动：`i40e`
- 当前队列能力：`Combined 32`
- 当前 NUMA 节点：`1`
- 本地 CPU 范围：`16-31`

### 3.2 DPDK 资源基线

- hugepage 已配置为 `2 x 512MB`
- 当前 hugepage 位于 `NUMA node 1`
- `hugetlbfs` 已挂载到 `/dev/hugepages`
- `enP1s25f3` 已完成一轮 `i40e -> vfio-pci -> dpdk-testpmd -> i40e` 最小闭环回退

## 4. AF_XDP 已完成基线

当前已确认完成：

- `check_af_xdp_env.sh` 已确认 `program_type xdp` 与 `map_type xskmap` 可用
- 最小 `.bpf.o` 已成功生成
- 最小 XDP attach 已成功
- `bpftool net` 已看到 `enP1s25f3 driver id`
- 最小 AF_XDP bind probe 已成功绑定 `enP1s25f3 queue 0`
- `AF_XDP RX PoC` 已成功运行
- 当前 `xdp_attach_mode=driver`
- 当前 `xsk_mode=copy`
- 强制 `zerocopy` 时 `xsk_socket__create failed`
- 因此当前平台结论已明确为：`copy` 路径可用，`zerocopy` 不可用

建议自检入口：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
./scripts/check_af_xdp_env.sh enP1s25f3 0
./scripts/compile_min_xdp.sh
./scripts/attach_min_xdp.sh enP1s25f3
bpftool net
./scripts/build_af_xdp_bind_probe.sh
./build_af_xdp_probe/af_xdp_bind_probe enP1s25f3 0
./scripts/detach_xdp.sh enP1s25f3
```

## 5. DPDK 已完成基线

当前已确认完成：

- hugepage 已分配并可用
- `vfio` / `vfio-pci` / `uio_pci_generic` 已存在
- `libdpdk 19.11.14` 已可用
- `dpdk-devbind.py` 已可用
- `dpdk-testpmd` 已可用
- 已完成 `vfio-pci -> dpdk-testpmd -> i40e` 最小闭环
- 已验证测试结束后可将 `enP1s25f3` 安全绑回 `i40e` 并恢复 `UP`

建议自检入口：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/check_dpdk_env.sh 0001:05:00.3
```

正式驱动重绑与压测执行时，继续沿用已验证的回退路径：

- 绑定到 `vfio-pci`
- 运行 `dpdk-testpmd` 或正式压测程序
- 结束后重新绑定回 `i40e`
- 执行 `ip link set dev enP1s25f3 up`

## 6. 当前剩余关注项

当前不再属于缺失配置，而属于后续验证重点的事项如下：

- 真实受控流量尚未稳定进入 `enP1s25f3 queue 0`
- `AF_XDP` 当前仅确认 `copy` 路径，不再继续以 `zerocopy` 为当前平台目标
- `dpdk-hugepages.py` 仍未提供，但不阻断当前正式压测窗口
- 使用 `libxdp` 相关脚本时，需保持共享缓存前缀环境变量一致

## 7. 标准执行基线

### 7.1 AF_XDP 标准环境

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
```

### 7.2 DPDK 标准环境

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/check_dpdk_env.sh 0001:05:00.3
```

## 8. 文档闭环要求

从 `2026-03-17` 起，后续若发生以下变化，必须先更新本文件，再同步其他文档：

- 实验口变更
- 驱动重绑策略变更
- 正式压测窗口状态变更
- `libxdp` / `libdpdk` / hugepage 基线变更
- AF_XDP 或 DPDK 验收结论变更

当前闭环结论：

- `enP1s25f3` 长期专用实验口：已确认
- 正式 `DPDK` 压测窗口：已确认
- 继续对 `enP1s25f3` 执行驱动重绑：已确认
- 本文件：已转为完成态基线文档，并作为真实源使用
