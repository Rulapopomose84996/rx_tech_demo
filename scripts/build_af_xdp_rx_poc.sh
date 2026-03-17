#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build_af_xdp_probe"
OUT="${OUT_DIR}/af_xdp_rx_poc"

mkdir -p "${OUT_DIR}"

gcc -O2 -Wall -Wextra -std=gnu11 \
    "${ROOT_DIR}/tools/af_xdp_rx_poc.c" \
    $(pkg-config --cflags --libs libbpf) \
    -o "${OUT}"

echo "${OUT}"
