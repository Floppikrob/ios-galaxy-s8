#!/bin/bash
# Write an image to a partition from TWRP over adb in verified chunks
# (bash counterpart of flash-chunks.ps1). Rerun after a USB drop: chunks whose
# hash already matches on the phone are skipped.
#   flash-chunks.sh IMAGE BLOCKDEV [CHUNK_MIB]      e.g. ios16-root.img /dev/block/sda29
set -euo pipefail
img=$1 dev=$2 mib=${3:-32}
size=$(stat -c %s "$img")
n=$(( (size + mib * 1048576 - 1) / (mib * 1048576) ))
psize=$(adb shell blockdev --getsize64 "$dev" | tr -d '\r')
(( size <= psize )) || { echo "image $size > partition $psize" >&2; exit 1; }
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
for ((i = 0; i < n; i++)); do
	dd if="$img" of="$tmp" bs=1M skip=$((i * mib)) count=$mib status=none
	want=$(sha256sum "$tmp" | cut -d' ' -f1)
	len=$(stat -c %s "$tmp")
	have=$(adb shell "dd if=$dev bs=1M skip=$((i * mib)) count=$mib 2>/dev/null | head -c $len | sha256sum" | cut -d' ' -f1)
	if [[ $have == "$want" ]]; then
		printf '\r%d/%d same ' $((i + 1)) $n
		continue
	fi
	adb push "$tmp" /tmp/chunk >/dev/null
	adb shell "dd if=/tmp/chunk of=$dev bs=1M seek=$((i * mib)) conv=notrunc,fsync 2>/dev/null; rm /tmp/chunk"
	have=$(adb shell "dd if=$dev bs=1M skip=$((i * mib)) count=$mib 2>/dev/null | head -c $len | sha256sum" | cut -d' ' -f1)
	[[ $have == "$want" ]] || { echo; echo "chunk $i: readback mismatch" >&2; exit 1; }
	printf '\r%d/%d written ' $((i + 1)) $n
done
adb shell sync
echo; echo "done: $n chunks of $mib MiB verified on $dev"
