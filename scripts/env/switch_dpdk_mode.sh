#!/usr/bin/env bash
# ============================================
# DPDK 模式切换脚本
# ============================================
# 用途：在普通 Linux 内核网卡模式和 DPDK vfio-pci 模式之间切换指定的网卡端口
#
# 使用方法:
#   ./switch_dpdk_mode.sh <目标模式> <网络接口名>
#
# 参数说明:
#   目标模式      - linux 或 dpdk
#                 - linux: 切换到普通 Linux 内核模式 (使用 i40e 驱动)
#                 - dpdk:  切换到 DPDK 模式 (使用 vfio-pci 驱动)
#
#   网络接口名    - 当前仅支持：receiver0
#
# 使用示例:
#   # 将 receiver0 切换到 DPDK 模式
#   ./switch_dpdk_mode.sh dpdk receiver0
#
#   # 将 receiver0 切换回 Linux 模式
#   ./switch_dpdk_mode.sh linux receiver0
#
# 运行环境:
#   - 必须在 Linux 服务器上运行
#   - 需要安装 dpdk-devbind.py 工具
#   - 需要 sudo 权限
#
# 注意事项:
#   - 切换到 DPDK 模式后，网卡将从 Linux 网络栈中消失 (这是正常现象)
#   - 切换回 Linux 模式后，网卡会重新出现在网络栈中
#   - 建议在切换到 DPDK 模式前先设置好巨型帧 MTU(如需要)
#     例如：sudo ip link set receiver0 mtu 9000
# ============================================

set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <linux|dpdk> <receiver0>" >&2
  exit 1
fi

TARGET_MODE="$1"
TARGET_IFACE="$2"

case "${TARGET_MODE}" in
  linux|dpdk)
    ;;
  *)
    echo "invalid mode: ${TARGET_MODE} (expected linux or dpdk)" >&2
    exit 1
    ;;
esac

case "${TARGET_IFACE}" in
  receiver0)
    TARGET_BDF="0001:05:00.0"
    TARGET_KERNEL_DRIVER="i40e"
    ;;
  *)
    echo "unsupported interface: ${TARGET_IFACE}" >&2
    echo "currently allowed interfaces: receiver0" >&2
    exit 1
    ;;
esac

DPDK_DEVBIND="$(command -v dpdk-devbind.py || true)"
if [ -z "${DPDK_DEVBIND}" ]; then
  echo "dpdk-devbind.py not found in PATH" >&2
  exit 1
fi

if [ ! -e "/sys/bus/pci/devices/${TARGET_BDF}" ]; then
  echo "target BDF not present: ${TARGET_BDF}" >&2
  exit 1
fi

current_driver() {
  if [ -L "/sys/bus/pci/devices/${TARGET_BDF}/driver" ]; then
    basename "$(readlink -f "/sys/bus/pci/devices/${TARGET_BDF}/driver")"
  else
    echo "unbound"
  fi
}

current_mode() {
  local driver
  driver="$(current_driver)"
  if [ "${driver}" = "vfio-pci" ]; then
    echo "dpdk"
  elif [ "${driver}" = "${TARGET_KERNEL_DRIVER}" ]; then
    echo "linux"
  else
    echo "other:${driver}"
  fi
}

print_status() {
  echo "[target]"
  echo "iface=${TARGET_IFACE}"
  echo "bdf=${TARGET_BDF}"
  echo "kernel_driver=${TARGET_KERNEL_DRIVER}"
  echo "current_driver=$(current_driver)"
  echo "current_mode=$(current_mode)"
  echo

  echo "[ip link]"
  ip -br link | egrep "receiver0|receiver1|receiver2|receiver3" || true
  echo

  echo "[lspci]"
  lspci -nnk -s "${TARGET_BDF}" || true
  echo

  echo "[dpdk-devbind]"
  "${DPDK_DEVBIND}" --status 2>/dev/null | sed -n '1,220p' || true
  echo
}

run_sudo() {
  sudo "$@"
}

echo "[pre-check]"
print_status

CURRENT_MODE="$(current_mode)"
if [ "${CURRENT_MODE}" = "${TARGET_MODE}" ]; then
  echo "already in target mode: ${TARGET_MODE}"
  exit 0
fi

if [ "${TARGET_MODE}" = "dpdk" ]; then
  echo "[action]"
  echo "switching ${TARGET_IFACE} (${TARGET_BDF}) to DPDK mode (vfio-pci)"
  echo "warning: ${TARGET_IFACE} will disappear from Linux networking after rebind"
  run_sudo modprobe vfio-pci
  run_sudo ip link set dev "${TARGET_IFACE}" down || true
  run_sudo "${DPDK_DEVBIND}" -b vfio-pci "${TARGET_BDF}"
else
  echo "[action]"
  echo "switching ${TARGET_IFACE} (${TARGET_BDF}) to Linux mode (${TARGET_KERNEL_DRIVER})"
  run_sudo modprobe "${TARGET_KERNEL_DRIVER}"
  run_sudo "${DPDK_DEVBIND}" -b "${TARGET_KERNEL_DRIVER}" "${TARGET_BDF}"
  sleep 2
  run_sudo ip link set dev "${TARGET_IFACE}" up || true
fi

echo
echo "[post-check]"
print_status
