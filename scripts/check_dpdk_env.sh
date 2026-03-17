#!/usr/bin/env bash
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
