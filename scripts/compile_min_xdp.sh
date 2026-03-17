#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${ROOT_DIR}/src/backends/af_xdp/bpf/xdp_redirect_kern.bpf.c"
OUT_DIR="${ROOT_DIR}/build_af_xdp_probe"
OUT="${OUT_DIR}/xdp_redirect_kern.bpf.o"

mkdir -p "${OUT_DIR}"

CLANG_BIN="${CLANG_BIN:-/usr/bin/clang}"

"${CLANG_BIN}" -O2 -target bpf -D__TARGET_ARCH_arm64 \
               -I/usr/include/aarch64-linux-gnu \
               -c "${SRC}" -o "${OUT}"

echo "${OUT}"
