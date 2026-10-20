#!/bin/bash
# Pull the previous iOS boot's logs from a Galaxy S8 in TWRP (bash counterpart
# of pull-sweetlog.ps1): the kernel console ring (last 1025 blocks of the
# partition) decoded with tools/sweet-log.py, and ramoops pmsg.
#   pull-sweetlog.sh NAME [BLOCKDEV]      (BLOCKDEV default: USERDATA by name)
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
name=$1
dev=${2:-$(adb shell readlink -f /dev/block/platform/11120000.ufs/by-name/USERDATA | tr -d '\r')}
mkdir -p "$repo/logs"
size=$(adb shell blockdev --getsize64 "$dev" | tr -d '\r')    # TWRP's sh is 32-bit: count here
adb shell "dd if=$dev of=/tmp/slog.bin bs=4096 skip=$((size / 4096 - 1025)) count=1025 2>/dev/null"
adb pull /tmp/slog.bin "$repo/logs/sweetlog-$name.bin" >/dev/null
adb pull /sys/fs/pstore/pmsg-ramoops-0 "$repo/logs/pmsg-$name.txt" >/dev/null 2>&1 || echo "no pmsg"
python3 "$repo/tools/sweet-log.py" "$repo/logs/sweetlog-$name.bin" "$repo/logs/sweetlog-$name.txt"
echo "kernel log: logs/sweetlog-$name.txt"
