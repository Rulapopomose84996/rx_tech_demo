#!/usr/bin/env bash
# ============================================
# DPDK 环境安装与配置脚本
# ============================================
# 用途：在 Linux 服务器上安装和配置共享缓存的 DPDK 工具链
#
# 功能说明：
#   - 下载并编译指定版本的 DPDK 库
#   - 安装 meson 构建工具
#   - 配置 hugepage 内存（默认 512MB 页面 2 个）
#   - 安装 DPDK 运行时库和工具
#   - 配置系统动态链接库路径
#
# 使用方法:
#   ./setup_dpdk.sh
#
# 环境要求:
#   - 必须在 Linux 服务器上运行
#   - 需要网络访问权限下载 DPDK 源码
#   - 需要 sudo 权限进行系统配置
#
# 配置变量:
#   CACHE_ROOT      - 缓存根目录 (默认: /home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo)
#   DPDK_VERSION    - DPDK 版本号 (默认: 19.11.14)
#   HUGEPAGES_512M  - 512MB hugepage 数量 (默认: 2)
#
# 运行后效果:
#   - DPDK 库文件安装到 /usr/local/lib64
#   - pkg-config 配置文件链接到系统路径
#   - hugepage 内存配置生效
#   - 可使用 dpdk-devbind.py, dpdk-testpmd 等工具
# ============================================

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CACHE_ROOT="${CACHE_ROOT:-/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo}"
ARCHIVES_DIR="${CACHE_ROOT}/archives"
BUILD_ROOT="${CACHE_ROOT}/build/native-aarch64"
DPDK_VERSION="${DPDK_VERSION:-19.11.14}"
DPDK_ARCHIVE="${ARCHIVES_DIR}/dpdk-${DPDK_VERSION}.tar.xz"
DPDK_URL="${DPDK_URL:-https://fast.dpdk.org/rel/dpdk-${DPDK_VERSION}.tar.xz}"
DPDK_SOURCE_DIR="${BUILD_ROOT}/dpdk-stable-${DPDK_VERSION}"
DPDK_BUILD_DIR="${BUILD_ROOT}/dpdk-${DPDK_VERSION}-build"
MESON_VERSION="${MESON_VERSION:-0.57.2}"
HUGEPAGES_512M="${HUGEPAGES_512M:-2}"
HUGEPAGE_NODE="${HUGEPAGE_NODE:-1}"
USER_SITE="$(python3 - <<'PY'
import site
print(site.getusersitepackages())
PY
)"

if [ "$(id -u)" -eq 0 ]; then
  PRIV_CMD=()
else
  PRIV_CMD=(sudo)
fi

export PATH="$HOME/.local/bin:${PATH}"

mkdir -p "${ARCHIVES_DIR}" "${BUILD_ROOT}"

python3 -m pip install --user "meson==${MESON_VERSION}"

if [ ! -f "${DPDK_ARCHIVE}" ]; then
  wget -O "${DPDK_ARCHIVE}" "${DPDK_URL}"
fi

rm -rf "${DPDK_SOURCE_DIR}" "${DPDK_BUILD_DIR}"
tar -xf "${DPDK_ARCHIVE}" -C "${BUILD_ROOT}"

meson setup "${DPDK_BUILD_DIR}" "${DPDK_SOURCE_DIR}" \
  -Dexamples= \
  -Dtests=false \
  -Ddefault_library=shared

ninja -C "${DPDK_BUILD_DIR}"

"${PRIV_CMD[@]}" env \
  PYTHONPATH="${USER_SITE}" \
  PATH="$HOME/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
  ninja -C "${DPDK_BUILD_DIR}" install

"${PRIV_CMD[@]}" bash -lc "
printf '%s\n' /usr/local/lib64 > /etc/ld.so.conf.d/rxtech_dpdk.conf
ln -sf /usr/local/lib64/pkgconfig/libdpdk.pc /usr/lib64/pkgconfig/libdpdk.pc
ldconfig
"

HUGEPAGES_512M="${HUGEPAGES_512M}" HUGEPAGE_NODE="${HUGEPAGE_NODE}" \
  "${SCRIPT_DIR}/install_dpdk_hugepages_service.sh"

echo "[cache]"
find "${CACHE_ROOT}" -maxdepth 3 | sort
echo

echo "[dpdk]"
pkg-config --modversion libdpdk
command -v dpdk-devbind.py
command -v dpdk-testpmd
command -v dpdk-hugepages.py || true
echo

echo "[hugepages]"
grep -H . /sys/kernel/mm/hugepages/hugepages-524288kB/*
