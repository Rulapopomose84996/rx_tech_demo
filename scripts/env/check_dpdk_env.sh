#!/usr/bin/env bash
# ============================================
# DPDK 环境检查脚本
# ============================================
# 用途：在绑定或运行接收器之前检查 Linux 服务器上的 DPDK 先决条件
#
# 功能说明：
#   - 检查目标网卡设备的状态
#   - 检查 IOMMU 是否启用
#   - 检查 VFIO 模块是否加载
#   - 检查 DPDK 相关库和工具是否安装
#   - 检查 hugepage 内存配置
#   - 验证 vfio-pci 驱动是否可用
#
# 使用方法:
#   ./check_dpdk_env.sh [PCI BDF地址]
#
# 参数说明:
#   PCI BDF地址 - 目标网卡的PCI总线设备功能地址
#                 默认值: 0001:05:00.0
#
# 示例:
#   # 检查默认设备 0001:05:00.0
#   ./check_dpdk_env.sh
#
#   # 检查指定设备
#   ./check_dpdk_env.sh 0000:81:00.0
#
# 检查项目:
#   - IOMMU 是否启用
#   - VFIO 模块是否加载
#   - hugepage 内存是否分配
#   - DPDK 库和工具是否可用
#   - 目标网卡的当前驱动状态
#   - VFIO 设备节点是否存在
#
# 输出说明:
#   脚本会输出各个检查项目的当前状态，
#   并在最后给出运行 DPDK 应用前必须满足的条件清单
# ============================================

set -euo pipefail

TARGET_BDF="${1:-0001:05:00.0}"

echo "[target]"
echo "bdf=${TARGET_BDF}"
echo

echo "[kernel]"
uname -a
echo

echo "[iommu]"
dmesg | grep -i iommu | tail -n 20 || true
echo

echo "[vfio modules]"
lsmod | egrep 'vfio|uio' || true
echo

echo "[driver candidates]"
modinfo vfio-pci 2>/dev/null | head -n 10 || echo "vfio-pci:not-found"
echo
modinfo uio_pci_generic 2>/dev/null | head -n 10 || echo "uio_pci_generic:not-found"
echo

echo "[current pci binding]"
lspci -nnk -s "${TARGET_BDF}" || true
echo

echo "[iommu group]"
readlink -f "/sys/bus/pci/devices/${TARGET_BDF}/iommu_group" || true
echo

echo "[hugetlbfs]"
mount | grep -i hugetlbfs || echo "hugetlbfs:not-mounted"
echo

echo "[hugepages]"
grep -H . /sys/kernel/mm/hugepages/hugepages-524288kB/* 2>/dev/null || true
echo

echo "[libraries and tools]"
pkg-config --modversion libdpdk 2>/dev/null || echo "libdpdk:not-found"
command -v dpdk-devbind.py || echo "dpdk-devbind.py:not-found"
command -v dpdk-testpmd || echo "dpdk-testpmd:not-found"
command -v dpdk-hugepages.py || echo "dpdk-hugepages.py:not-found"
echo

echo "[vfio device node]"
ls -l /dev/vfio 2>/dev/null || true
echo

echo "[verdict]"
echo "- MUST: hugepages allocated (>0)"
echo "- MUST: libdpdk available"
echo "- MUST: dpdk-devbind.py available"
echo "- MUST: target port is approved for driver rebind"
echo "- MUST: bind and rollback commands are prepared before testing"
