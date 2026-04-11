# DPDK 服务器配置与使用说明

本文面向当前 `Radar` 服务器上的 `rx_tech_demo` 仓库，按当前现场状态固化 DPDK 使用方法。

当前约束：

- 当前 DPDK 运行口按现场状态使用 `receiver0`
- `receiver0` 对应 PCI BDF：`0001:05:00.0`
- hugepage 基线：`NUMA node1` 上 `2 x 512MB`
- 权威执行环境：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

## 当前已配置内容

- `libdpdk` 版本：`19.11.14`
- DPDK 工具：`/usr/local/bin/dpdk-devbind.py`、`/usr/local/bin/dpdk-testpmd`
- hugepage 持久化服务：`rxtech-dpdk-hugepages.service`
- hugepage 分配脚本：`/usr/local/bin/rxtech-dpdk-hugepages.sh`
- 接收配置文件：`configs/dpdk_single_face.conf`

## 1. 检查当前 DPDK 环境

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/env/check_dpdk_env.sh 0001:05:00.0
systemctl status rxtech-dpdk-hugepages.service --no-pager
```

你应重点确认：

- `libdpdk` 已输出 `19.11.14`
- `dpdk-devbind.py` 与 `dpdk-testpmd` 均存在
- `nr_hugepages=2`
- `free_hugepages=2`

## 2. 如果 hugepage 丢失，重新下发持久化配置

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/env/install_dpdk_hugepages_service.sh
```

这个脚本会完成三件事：

- 安装 `/usr/local/bin/rxtech-dpdk-hugepages.sh`
- 安装并启用 `rxtech-dpdk-hugepages.service`
- 立即为 `node1` 分配 `2 x 512MB` hugepages

## 3. 切换 `receiver0` 到 DPDK 模式

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/env/switch_dpdk_mode.sh dpdk receiver0
```

切换成功后：

- `lspci -nnk -s 0001:05:00.0` 应显示 `Kernel driver in use: vfio-pci`
- `ip link show receiver0` 不再存在，这是正常现象

## 4. 做最小 DPDK 自检

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
sudo dpdk-testpmd -l 16-17 -n 4 --in-memory -w 0001:05:00.0 -- --port-topology=chained --rxq=1 --txq=1 --forward-mode=rxonly --auto-start
```

说明：

- 这是最小自检命令，用来确认 `vfio-pci + hugepage + EAL` 闭环成立
- 如果只做冒烟验证，看到端口初始化完成后即可按 `Ctrl+C` 退出

## 5. 运行接收程序

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
sudo ./build/src/receiver/rx_receiver_dpdk --config configs/dpdk_single_face.conf --run-until-stopped --status-interval 1
```

当前配置文件中的关键字段：

- `interface_name = receiver0`
- `pci_addr = 0001:05:00.0`
- `receiver_ipv4 = 172.20.11.100`
- `allowed_source_ipv4 = 172.20.11.222`

注意：

- 当前代码已经修复 DPDK 19.11 EAL 参数兼容问题，设备选择参数使用 `-w`
- 运行前必须保证 `receiver0` 已切换到 `vfio-pci`
- 当前服务器上 `receiver0` 对应的 `/dev/vfio/18` 由 root 管理，因此接收程序按当前基线使用 `sudo` 启动

## 6. 切回 Linux 网络模式

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/env/switch_dpdk_mode.sh linux receiver0
ip -4 addr show receiver0
```

切回成功后：

- `lspci -nnk -s 0001:05:00.0` 应显示 `Kernel driver in use: i40e`
- `receiver0` 会重新回到 Linux 网络栈

## 7. 当前最常见故障与处理

### `rte_eal_init() failed` 且提示 hugepage

先执行：

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
grep -H . /sys/kernel/mm/hugepages/hugepages-524288kB/*
systemctl restart rxtech-dpdk-hugepages.service
```

### `receiver0` 不存在

这通常表示它当前已经绑定到了 `vfio-pci`。这不是故障，而是 DPDK 模式下的正常现象。

### 程序仍报端口不可用

先执行：

执行位置：Linux 服务器 `/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
lspci -nnk -s 0001:05:00.0
bash ./scripts/env/check_dpdk_env.sh 0001:05:00.0
```
