#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NIC="${1:-receiver3}"
OBJ="${ROOT_DIR}/build_af_xdp_probe/xdp_redirect_kern.bpf.o"

if [ ! -f "${OBJ}" ]; then
  echo "missing object: ${OBJ}" >&2
  exit 1
fi

sudo ip link set dev "${NIC}" xdp obj "${OBJ}" sec xdp
ip -details link show dev "${NIC}" | sed -n '1,20p'
