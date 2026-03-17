#!/usr/bin/env bash
set -euo pipefail

NIC="${1:-enP1s25f3}"
QUEUE_ID="${2:-0}"
DURATION="${3:-5}"
MODE="${4:-rx_only}"
OUTPUT_DIR="${5:-results/af_xdp_benchmark}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Running rxbench_xdp: nic=${NIC} queue=${QUEUE_ID} duration=${DURATION}s mode=${MODE}"
sudo "${ROOT_DIR}/build/src/apps/rxbench_xdp" "${MODE}" "" "${OUTPUT_DIR}" "${NIC}" "${QUEUE_ID}" "${DURATION}"
