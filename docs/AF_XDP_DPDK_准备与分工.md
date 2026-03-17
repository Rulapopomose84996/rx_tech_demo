# AF_XDP / DPDK 准备与分工

本文档把 `rx_tech_demo` 进入 `AF_XDP` / `DPDK` 实验前的工作拆成两类：

- 我可以直接代做的工作
- 你必须亲自完成或拍板的工作

## 我已经能代做的工作

### 1. 服务器只读探查

我已经完成或可以继续完成：

- 探查内核、编译器、CMake、NUMA、网卡、驱动、IOMMU、VFIO、hugetlbfs 状态
- 探查 AF_XDP 所需的 BPF/XDP 内核能力
- 探查 DPDK 所需的 VFIO / hugepage / 驱动绑定前提
- 把实探结果写回设计文档

### 2. 仓库内工程准备

我已经完成或可以继续完成：

- 建立项目骨架
- 建立服务器共享缓存命名空间说明
- 补充环境适配说明
- 编写环境自检脚本
- 编写后续 AF_XDP / DPDK 最小 PoC 的代码骨架和脚本骨架

当前已实际完成：

- 服务器项目目录已 clone 到 `/home/devuser/WorkSpace/rx_tech_demo`
- 共享缓存命名空间已建立：
  - `/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/archives`
  - `/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64`
- 共享缓存说明已写入 `/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/README.md`
- 自检脚本已入仓
- 最小 BPF / XDP attach / AF_XDP bind 验证脚本已入仓
- 已完成一轮服务器最小 AF_XDP 实测闭环
- AF_XDP backend 已编进正式工程并在服务器上构建通过
- 收包级别 AF_XDP RX PoC 与 benchmark 脚本已入仓并可执行

### 3. 可直接使用的自检脚本

服务器上建议运行：

运行位置：Linux server，目录 `/home/devuser/WorkSpace/rx_tech_demo`
```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/check_af_xdp_env.sh enP1s25f3 0
./scripts/check_dpdk_env.sh 0001:05:00.3
```

最小 AF_XDP 验证入口：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/compile_min_xdp.sh
./scripts/attach_min_xdp.sh enP1s25f3
bpftool net
./scripts/build_af_xdp_bind_probe.sh
./build_af_xdp_probe/af_xdp_bind_probe enP1s25f3 0
./scripts/detach_xdp.sh enP1s25f3
```

收包级别 AF_XDP RX PoC 与 benchmark 入口：

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/build_af_xdp_rx_poc.sh
sudo ./build_af_xdp_probe/af_xdp_rx_poc enP1s25f3 0 2
sudo ./scripts/run_af_xdp_benchmark.sh enP1s25f3 0 2 rx_only results/af_xdp_benchmark_script
```

## 你必须亲自完成或确认的工作

### 1. 已形成的推荐决策

当前推荐方案已经明确：

- AF_XDP 测试口：`enP1s25f3`
- DPDK 解绑测试口：`enP1s25f3`
- AF_XDP 依赖管理：系统安装优先
- DPDK 依赖管理：共享缓存离线化优先
- hugepage：允许启用
- 驱动切换：允许，但仅限专用实验口 `enP1s25f3`

你现在需要做的不是再次决策，而是确认这些推荐是否可以在现场执行。

### 2. 需要你安装或让运维安装的东西

AF_XDP 必须：

- `libbpf` 开发库
- 建议同时准备 `libxdp`

DPDK 必须：

- `libdpdk`
- `dpdk-devbind.py`
- 建议同时准备 `dpdk-testpmd`
- 建议同时准备 `dpdk-hugepages.py`

### 3. 需要你或运维执行的变更类动作

- 分配 hugepage
- 确认 `enP1s25f3` 可作为专用实验口
- 在需要时把 `enP1s25f3` 从内核驱动切到 `vfio-pci`
- 测试完成后把 `enP1s25f3` 安全绑回原驱动

这些都属于会影响系统状态的动作，不应由我在未知影响范围下直接执行。

## 建议推进顺序

1. 先运行自检脚本，确认当前缺口
2. 先补 AF_XDP 依赖和最小 attach 流程
3. 再补 DPDK hugepage 和用户态工具链
4. 最后才做 devbind 和 testpmd

## 当前结论

我已经尽量把“无需改变系统状态”的工作做掉了，当前真正卡住项目推进的主要是：

- AF_XDP 仍缺 `libxdp`、带流量的 RX 验证和 zero-copy/copy 判定
- DPDK 用户态依赖未安装
- hugepage 尚未分配
- `enP1s25f3` 是否可作为专用实验口，仍需你或运维最后确认
- DPDK 端口重绑授权仍需你或运维最终确认

根据 2026-03-17 终端日志，AF_XDP 方向已新增进展：

- `libbpf` 已在服务器上安装完成
- `pkg-config --modversion libbpf` 返回 `0.8.1`
- `ldconfig -p` 已能看到 `libbpf.so`
- `check_af_xdp_env.sh` 已确认 XDP 与 XSKMAP 能力存在
- 最小 `.bpf.o` 已成功生成
- 最小 XDP attach 已成功
- 最小 AF_XDP bind probe 已成功
- 正式脚本已在服务器上完整重跑通过
- 收包级别 AF_XDP RX PoC 已成功运行，但本轮未注入测试流量，结果为 `packets=0`
- `rxbench_xdp` 已通过正式工程入口运行，结果文件已生成

本轮实测还确认了两个环境特征：

- `/usr/local/corex/bin/clang` 不包含可用的 BPF target，不能直接用于生成 `.bpf.o`
- 该 4.19 内核下不应把带 BTF 调试信息的对象直接拿去 attach，因此最小 BPF 编译脚本默认不携带 `-g`
