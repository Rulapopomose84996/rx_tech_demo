#!/usr/bin/env bash
set -euo pipefail

RX_NIC="${1:-enP1s25f3}"
TX_IFACE="${2:-enP1s25f0}"
QUEUE_ID="${3:-0}"
DURATION="${4:-10}"
PPS="${5:-1000}"
PAYLOAD_SIZE="${6:-512}"
OUTPUT_DIR="${7:-results/af_xdp_controlled}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

RX_MAC="$(cat /sys/class/net/${RX_NIC}/address)"
TX_MAC="$(cat /sys/class/net/${TX_IFACE}/address)"

cleanup() {
  sudo ip link set dev "${RX_NIC}" xdp off >/dev/null 2>&1 || true
  sudo ip addr del 192.168.1.102/24 dev "${RX_NIC}" >/dev/null 2>&1 || true
}

trap cleanup EXIT

mkdir -p "${ROOT_DIR}/${OUTPUT_DIR}"
sudo ip addr add 192.168.1.102/24 dev "${RX_NIC}" >/dev/null 2>&1 || true

bash "${ROOT_DIR}/scripts/compile_min_xdp.sh" >/dev/null
bash "${ROOT_DIR}/scripts/build_af_xdp_rx_poc.sh" >/dev/null

sudo "${ROOT_DIR}/build_af_xdp_probe/af_xdp_rx_poc" "${RX_NIC}" "${QUEUE_ID}" "${DURATION}" "${ROOT_DIR}/build_af_xdp_probe/xdp_redirect_kern.bpf.o" \
  > "${ROOT_DIR}/${OUTPUT_DIR}/rxpoc.log" 2>&1 &
RX_PID=$!

sleep 1

python3 "${ROOT_DIR}/tools/raw_eth_sender.py" \
  --iface "${TX_IFACE}" \
  --dst-mac "${RX_MAC}" \
  --src-mac "${TX_MAC}" \
  --duration "${DURATION}" \
  --pps "${PPS}" \
  --payload-size "${PAYLOAD_SIZE}" \
  > "${ROOT_DIR}/${OUTPUT_DIR}/sender.log" 2>&1

wait "${RX_PID}"

echo "--- sender.log ---"
cat "${ROOT_DIR}/${OUTPUT_DIR}/sender.log"
echo "--- rxpoc.log ---"
cat "${ROOT_DIR}/${OUTPUT_DIR}/rxpoc.log"
