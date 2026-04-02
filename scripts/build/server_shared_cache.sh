#!/usr/bin/env bash
# Purpose: authoritative Linux server build entrypoint for the current DPDK receiver mainline.
# Environment: run on the Linux server workspace /home/devuser/WorkSpace/rx_tech_demo.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
THIRD_PARTY_CACHE="${THIRD_PARTY_CACHE:-/home/devuser/WorkSpace/ThirdPartyCache}"

echo "Using shared third-party cache: ${THIRD_PARTY_CACHE}"

cmake -S "${ROOT_DIR}" \
      -B "${BUILD_DIR}" \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DRXTECH_THIRD_PARTY_CACHE="${THIRD_PARTY_CACHE}"

cmake --build "${BUILD_DIR}" -j
