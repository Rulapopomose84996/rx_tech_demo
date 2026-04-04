#!/usr/bin/env bash
# 安装并立即执行 DPDK hugepage 持久化服务。
# 目标：在每次开机后为 NUMA node1 分配 2 x 512MB hugepages，并清理旧的 rtemap 文件。

set -euo pipefail

HUGEPAGES_512M="${HUGEPAGES_512M:-2}"
HUGEPAGE_NODE="${HUGEPAGE_NODE:-1}"

if [ "$(id -u)" -eq 0 ]; then
  PRIV_CMD=()
else
  PRIV_CMD=(sudo)
fi

"${PRIV_CMD[@]}" bash -lc "
cat > /usr/local/bin/rxtech-dpdk-hugepages.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

HUGEPAGES_512M=\"\${1:-2}\"
HUGEPAGE_NODE=\"\${2:-1}\"
NODE_DIR=\"/sys/devices/system/node/node\${HUGEPAGE_NODE}/hugepages/hugepages-524288kB\"
GLOBAL_DIR=\"/sys/kernel/mm/hugepages/hugepages-524288kB\"

if [ ! -d \"\${NODE_DIR}\" ]; then
  echo \"missing hugepage node directory: \${NODE_DIR}\" >&2
  exit 1
fi

mkdir -p /dev/hugepages
if ! mountpoint -q /dev/hugepages; then
  mount -t hugetlbfs -o pagesize=512M none /dev/hugepages
fi

echo \"\${HUGEPAGES_512M}\" > \"\${NODE_DIR}/nr_hugepages\"
rm -f /dev/hugepages/rtemap_* 2>/dev/null || true

echo \"[node hugepages]\"
grep -H . \"\${NODE_DIR}\"/* || true
echo
echo \"[global hugepages]\"
grep -H . \"\${GLOBAL_DIR}\"/* || true
EOF
chmod 755 /usr/local/bin/rxtech-dpdk-hugepages.sh

cat > /etc/systemd/system/rxtech-dpdk-hugepages.service <<'EOF'
[Unit]
Description=Allocate DPDK hugepages for rx_tech_demo
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/rxtech-dpdk-hugepages.sh ${HUGEPAGES_512M} ${HUGEPAGE_NODE}
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable rxtech-dpdk-hugepages.service
systemctl restart rxtech-dpdk-hugepages.service
"

echo "[service]"
systemctl status rxtech-dpdk-hugepages.service --no-pager || true
echo
echo "[hugepages]"
grep -H . "/sys/devices/system/node/node${HUGEPAGE_NODE}/hugepages/hugepages-524288kB/"* || true
grep -H . /sys/kernel/mm/hugepages/hugepages-524288kB/* || true
