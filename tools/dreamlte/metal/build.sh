#!/bin/bash
# Build S8MaliMetal.bundle (arm64 iOS MH_BUNDLE, ad-hoc signed) into OUTDIR.
#   build.sh OUTDIR
# The draw path (s8mtl_draw.c) needs Mesa: set S8_MESA_LIB to the
# libs8mesa.a from build-mesa-ios.sh (its .flags file next to it) and
# S8_IOS_SDK to an iPhoneOS SDK (for libSystem). Without them the bundle is
# built without the draw path (stage 2: draws are skipped).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(realpath -m "${1:-$here/out}")
clang=$(command -v clang-18 || command -v clang)
b=$out/S8MaliMetal.bundle
mkdir -p "$b" "$out/obj"
objs=()
for src in "$here"/s8mtl_*.m; do
	o=$out/obj/$(basename "${src%.m}").o
	"$clang" -target arm64-apple-ios16.0 -O2 -Wall -Wextra -Werror -Wno-objc-root-class \
		-Wno-unused-parameter -fno-stack-protector -fobjc-runtime=ios-16.0 -fno-objc-arc \
		-fno-objc-exceptions -fblocks -nostdlibinc -isystem "$here/../ios/stub" -c "$src" -o "$o"
	objs+=("$o")
done
libs=("$here/../ios/libSystem.tbd")
if [[ -n ${S8_MESA_LIB:-} ]]; then
	sdk=$(realpath "${S8_IOS_SDK:?S8_IOS_SDK must name an iPhoneOS SDK}")
	o=$out/obj/s8mtl_draw.o
	eval "$(printf '%q' "$clang")" -target arm64-apple-ios16.0 "$(cat "${S8_MESA_LIB%.a}.flags")" -std=gnu11 -O2 \
		-Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
		-Wno-macro-redefined -Wno-ignored-qualifiers -fno-stack-protector -DNDEBUG \
		-c "$(printf '%q' "$here/s8mtl_draw.c")" -o "$(printf '%q' "$o")"
	objs+=("$o" "$S8_MESA_LIB")
	libs=("$sdk/usr/lib/libSystem.tbd")
else
	# no draw path: answer the draw interface with "unavailable"
	o=$out/obj/s8mtl_nodraw.o
	"$clang" -target arm64-apple-ios16.0 -O2 -Wall -Werror -nostdlibinc -isystem "$here/../ios/stub" \
		-DS8_NO_DRAW -x c -c "$here/s8mtl_nodraw.c" -o "$o"
	objs+=("$o")
fi
ld64.lld-18 -arch arm64 -platform_version ios 16.0 16.0 -bundle -adhoc_codesign \
	-o "$b/S8MaliMetal" "${objs[@]}" "${libs[@]}" \
	"$here/sdk/libobjc.tbd" "$here/sdk/Metal.tbd" "$here/sdk/Foundation.tbd" \
	"$here/sdk/IOSurface.tbd" "$here/sdk/CoreFoundation.tbd"
cp "$here/Info.plist" "$b/Info.plist"
echo "$b: $(stat -c %s "$b/S8MaliMetal") bytes${S8_MESA_LIB:+ (draw path)}"
