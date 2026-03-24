#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NIC="${1:-receiver3}"
QUEUE_ID="${2:-0}"
DURATION="${3:-3}"

bash "${ROOT_DIR}/scripts/build_af_xdp_rx_poc.sh" >/dev/null
echo "Running AF_XDP RX PoC: nic=${NIC} queue=${QUEUE_ID} duration=${DURATION}s"
sudo "${ROOT_DIR}/build_af_xdp_probe/af_xdp_rx_poc" "${NIC}" "${QUEUE_ID}" "${DURATION}"
