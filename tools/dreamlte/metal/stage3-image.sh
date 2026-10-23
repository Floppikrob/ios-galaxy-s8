#!/bin/bash
# Build everything for a Metal stage 3 boot (run in WSL/Linux as root):
# air2nir, the QuartzCore shader cache, the host draw test, the iOS Mesa
# library, the boot overlay (plugin with draw path + cache) and the BOOT
# image. Writes nothing to the phone.
#   stage3-image.sh WORK KERNEL STOCK_BOOT.img OUT.img [boot-args...]
# WORK holds mesa/src, mesa/build (Mesa 25.2.8, -Dtools=panfrost), sdk/
# (iPhoneOS16.5.sdk) and metal/default.metallib (QuartzCore's, from the
# iOS root; never committed).
set -euo pipefail
work=$(realpath "$1"); kernel=$(realpath "$2"); stock=$(realpath "$3"); out=$(realpath -m "$4")
shift 4
here=$(cd "$(dirname "$0")" && pwd)
m=$work/metal
mkdir -p "$m"
step() { echo; echo "### $*"; }

step air2nir
bash "$here/build-air2nir.sh" "$work/mesa/src" "$work/mesa/build" "$m/air2nir" > "$m/air2nir.log" 2>&1 \
	|| { tail -30 "$m/air2nir.log"; exit 1; }

step "host draw test"
bash "$here/test/build-draw-test.sh" "$work/mesa/src" "$work/mesa/build" "$m/air2nir" "$m/dt" > "$m/dt.log" 2>&1 \
	|| { tail -30 "$m/dt.log"; exit 1; }
grep '====' "$m/dt.log"

step "shader cache"
python3 "$here/build-shaders.py" "$m/default.metallib" "$m/air2nir" "$m/quartzcore.s8sc" > "$m/shaders.log" 2>&1 || true
grep -E 'FAIL' "$m/shaders.log" || true
tail -1 "$m/shaders.log"
[[ -s $m/quartzcore.s8sc ]] || exit 1

step "iOS Mesa"
bash "$here/build-mesa-ios.sh" "$work/mesa/src" "$work/mesa/build" "$work/sdk/iPhoneOS16.5.sdk" "$m/ios/libs8mesa.a" \
	> "$m/mesa-ios.log" 2>&1 || { tail -30 "$m/mesa-ios.log"; exit 1; }
grep FAILED "$m/mesa-ios.log" && exit 1
tail -1 "$m/mesa-ios.log"

step overlay
S8_MESA_LIB=$m/ios/libs8mesa.a S8_IOS_SDK=$work/sdk/iPhoneOS16.5.sdk S8_SHADER_CACHE=$m/quartzcore.s8sc \
	bash "$here/../ios/mkoverlay.sh" "$m/stage3.ovl"

step "BOOT image"
S8_OVERLAY=$m/stage3.ovl bash "$here/../make-boot.sh" "$kernel" "$stock" "$out" "$@"
ls -l "$out"
sha256sum "$out" "$kernel" "$m/quartzcore.s8sc"
