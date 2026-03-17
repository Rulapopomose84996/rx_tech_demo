#!/usr/bin/env bash
set -euo pipefail

NIC="${1:-enP1s25f3}"
sudo ip link set dev "${NIC}" xdp off
ip -details link show dev "${NIC}" | sed -n '1,12p'
