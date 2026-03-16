#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build_wsl_cross_dev"

cmake -S "${ROOT_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/aarch64-linux-gnu.cmake" \
      -DCMAKE_BUILD_TYPE=Debug

cmake --build "${BUILD_DIR}" -j
