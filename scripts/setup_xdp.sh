#!/usr/bin/env bash
set -euo pipefail

CACHE_ROOT="${CACHE_ROOT:-/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo}"
ARCHIVES_DIR="${CACHE_ROOT}/archives"
BUILD_ROOT="${CACHE_ROOT}/build/native-aarch64"
XDP_TOOLS_VERSION="${XDP_TOOLS_VERSION:-1.2.9}"
XDP_ARCHIVE="${ARCHIVES_DIR}/xdp-tools-${XDP_TOOLS_VERSION}.tar.gz"
XDP_URL="${XDP_URL:-https://github.com/xdp-project/xdp-tools/archive/refs/tags/v${XDP_TOOLS_VERSION}.tar.gz}"
XDP_SOURCE_DIR="${BUILD_ROOT}/xdp-tools-${XDP_TOOLS_VERSION}"
XDP_PREFIX="${BUILD_ROOT}/xdp-tools-${XDP_TOOLS_VERSION}-prefix"
CLANG_BIN="${CLANG_BIN:-/usr/bin/clang-10}"
LLC_BIN="${LLC_BIN:-/usr/bin/llc}"

mkdir -p "${ARCHIVES_DIR}" "${BUILD_ROOT}"

if ! command -v "${CLANG_BIN}" >/dev/null 2>&1; then
  echo "missing clang binary: ${CLANG_BIN}" >&2
  exit 1
fi

if ! command -v "${LLC_BIN}" >/dev/null 2>&1; then
  echo "missing llc binary: ${LLC_BIN}" >&2
  exit 1
fi

if ! pkg-config --modversion libbpf >/dev/null 2>&1; then
  echo "missing system libbpf; install libbpf-devel first" >&2
  exit 1
fi

if [ ! -f "${XDP_ARCHIVE}" ]; then
  curl -L --fail -o "${XDP_ARCHIVE}" "${XDP_URL}"
fi

rm -rf "${XDP_SOURCE_DIR}" "${XDP_PREFIX}"
tar -xf "${XDP_ARCHIVE}" -C "${BUILD_ROOT}"

pushd "${XDP_SOURCE_DIR}" >/dev/null
make libxdp_install PREFIX="${XDP_PREFIX}" CLANG="${CLANG_BIN}" LLC="${LLC_BIN}"
popd >/dev/null

export PKG_CONFIG_PATH="${XDP_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="${XDP_PREFIX}/lib:${LD_LIBRARY_PATH:-}"

echo "[toolchain]"
"${CLANG_BIN}" --version | head -n 1
"${LLC_BIN}" --version | egrep 'LLVM version|Registered Targets|bpf' || true
echo

echo "[libraries]"
pkg-config --modversion libbpf
pkg-config --modversion libxdp
echo

echo "[prefix]"
echo "PREFIX=${XDP_PREFIX}"
find "${XDP_PREFIX}" -maxdepth 3 | sort
