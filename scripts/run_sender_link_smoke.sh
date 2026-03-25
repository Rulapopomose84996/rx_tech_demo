#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_APPS_DIR="${ROOT_DIR}/build_production/src/apps"
if [[ ! -x "${BUILD_APPS_DIR}/rxbench_socket" ]]; then
  BUILD_APPS_DIR="${ROOT_DIR}/build_wsl_task1/src/apps"
fi

"${BUILD_APPS_DIR}/rxbench_socket" \
  --config "${ROOT_DIR}/configs/rx_sender_link.conf" \
  --scenario "${ROOT_DIR}/scenarios/sender_link_smoke.yaml"
