#!/bin/sh
# 导出 EdgeFocus 日志和数据库
set -eu

DATE=$(date +%Y%m%d_%H%M%S)
OUT_ARCHIVE="/tmp/edgefocus_logs_${DATE}.tar.gz"

echo "Collecting logs and database to ${OUT_ARCHIVE}..."

tar -czf "${OUT_ARCHIVE}" -C / \
    var/log/edgefocus.log \
    var/log/edgefocus.err \
    opt/edgefocus/edgefocus.db 2>/dev/null || true

echo "Done! You can copy it out via SCP:"
echo "scp root@<board-ip>:${OUT_ARCHIVE} ."
