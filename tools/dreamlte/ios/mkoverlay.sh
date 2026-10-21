#!/bin/bash
# Build the iOS programs and the Metal plugin and pack them into the boot
# overlay (written to the root once launchd made it writable).
#   mkoverlay.sh OUT.ovl     (then: S8_OVERLAY=OUT.ovl make-boot.sh ... launchdsuffix=s8)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(realpath -m "$1")
bin=$(mktemp -d)
trap 'rm -rf "$bin"' EXIT
bash "$here/build.sh" "$bin"
bash "$here/../metal/build.sh" "$bin" >/dev/null
ext=/System/Library/Extensions/S8MaliMetal.bundle
python3 "$here/../mkoverlay.py" "$out" \
	dir:/usr/appleinternal:0755:0:0 \
	dir:/usr/appleinternal/sbin:0755:0:0 \
	"file:/usr/appleinternal/sbin/launchd.s8:0755:0:0:$bin/s8gputest" \
	"dir:$ext:0755:0:0" \
	"file:$ext/Info.plist:0644:0:0:$bin/S8MaliMetal.bundle/Info.plist" \
	"file:$ext/S8MaliMetal:0755:0:0:$bin/S8MaliMetal.bundle/S8MaliMetal"
