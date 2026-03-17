#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
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
echo ${HUGEPAGES_512M} > /sys/kernel/mm/hugepages/hugepages-524288kB/nr_hugepages
rm -f /dev/hugepages/rtemap_* 2>/dev/null || true
"

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
