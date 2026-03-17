#!/usr/bin/env bash
set -euo pipefail

TARGET_NIC="${1:-enP1s25f0}"
TARGET_QUEUE="${2:-0}"

echo "[target]"
echo "nic=${TARGET_NIC}"
echo "queue=${TARGET_QUEUE}"
echo

echo "[kernel]"
uname -a
echo

echo "[required kernel config]"
if [ -r /proc/config.gz ]; then
  zcat /proc/config.gz | egrep 'CONFIG_BPF=|CONFIG_BPF_SYSCALL=|CONFIG_BPF_JIT=|CONFIG_XDP_SOCKETS=' || true
else
  cat "/boot/config-$(uname -r)" 2>/dev/null | egrep 'CONFIG_BPF=|CONFIG_BPF_SYSCALL=|CONFIG_BPF_JIT=|CONFIG_XDP_SOCKETS=' || true
fi
echo

echo "[bpftool]"
bpftool version || true
echo

echo "[bpftool feature]"
sudo -n bpftool feature probe kernel 2>/dev/null | egrep 'program_type xdp|map_type xskmap' || true
echo

echo "[compiler]"
clang -target bpf -dM -E - < /dev/null 2>/dev/null | head -n 5 || true
echo

echo "[libraries]"
pkg-config --modversion libbpf 2>/dev/null || echo "libbpf:not-found"
pkg-config --modversion libxdp 2>/dev/null || echo "libxdp:not-found"
echo

echo "[nic driver]"
ethtool -i "${TARGET_NIC}" || true
echo

echo "[queue capability]"
ethtool -l "${TARGET_NIC}" || true
echo

echo "[offload and rss]"
ethtool -k "${TARGET_NIC}" 2>/dev/null | egrep 'receive-hashing|generic-receive-offload|rx-checksumming|tx-checksumming' || true
echo

echo "[link details]"
ip -details link show dev "${TARGET_NIC}" | sed -n '1,12p' || true
echo

echo "[xdp attach]"
bpftool net 2>/dev/null || true
echo

echo "[sysctl]"
sysctl net.core.busy_poll net.core.busy_read 2>/dev/null || true
echo

echo "[verdict]"
echo "- MUST: libbpf available"
echo "- MUST: XDP program can compile"
echo "- MUST: target NIC has no conflicting XDP attach"
echo "- MUST: queue mapping is documented before test"
echo "- MUST: actual mode must be recorded after PoC (native/generic, zero-copy/copy)"
