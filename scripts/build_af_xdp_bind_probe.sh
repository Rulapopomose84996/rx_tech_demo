#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build_af_xdp_probe"
OUT="${OUT_DIR}/af_xdp_bind_probe"

mkdir -p "${OUT_DIR}"

gcc -O2 -Wall -Wextra -std=c11 \
    "${ROOT_DIR}/tools/af_xdp_bind_probe.c" \
    -o "${OUT}"

echo "${OUT}"
