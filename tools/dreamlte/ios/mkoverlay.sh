#!/bin/bash
# Build the iOS programs and pack them into the boot overlay.
#   mkoverlay.sh OUT.ovl     (then: S8_OVERLAY=OUT.ovl make-boot.sh ... launchdsuffix=s8)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(realpath -m "$1")
bin=$(mktemp -d)
trap 'rm -rf "$bin"' EXIT
bash "$here/build.sh" "$bin"
python3 "$here/../mkoverlay.py" "$out" \
	dir:/usr/appleinternal:0755:0:0 \
	dir:/usr/appleinternal/sbin:0755:0:0 \
	"file:/usr/appleinternal/sbin/launchd.s8:0755:0:0:$bin/s8gputest"
