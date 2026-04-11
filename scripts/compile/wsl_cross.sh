#!/usr/bin/env bash
# Purpose: local WSL cross-build helper for compile checks when server validation is unavailable.
# Environment: run under WSL in the project root; output goes to build_wsl_cross_dev/.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build_wsl_cross_dev"

cmake -S "${ROOT_DIR}" \
      -B "${BUILD_DIR}" \
      -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/aarch64-linux-gnu.cmake" \
      -DCMAKE_BUILD_TYPE=Debug

cmake --build "${BUILD_DIR}" -j
