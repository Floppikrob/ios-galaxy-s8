#!/bin/bash
# Build S8MaliMetal.bundle (arm64 iOS MH_BUNDLE, ad-hoc signed) into OUTDIR.
#   build.sh OUTDIR
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(realpath -m "${1:-$here/out}")
b=$out/S8MaliMetal.bundle
mkdir -p "$b" "$out/obj"
objs=()
for src in "$here"/s8mtl_*.m; do
	o=$out/obj/$(basename "${src%.m}").o
	clang -target arm64-apple-ios16.0 -O2 -Wall -Wextra -Werror -Wno-objc-root-class \
		-Wno-unused-parameter -fno-stack-protector -fobjc-runtime=ios-16.0 -fno-objc-arc \
		-fno-objc-exceptions -fblocks -nostdlibinc -isystem "$here/../ios/stub" -c "$src" -o "$o"
	objs+=("$o")
done
ld64.lld-18 -arch arm64 -platform_version ios 16.0 16.0 -bundle -adhoc_codesign \
	-o "$b/S8MaliMetal" "${objs[@]}" "$here/../ios/libSystem.tbd" \
	"$here/sdk/libobjc.tbd" "$here/sdk/Metal.tbd" "$here/sdk/Foundation.tbd" \
	"$here/sdk/IOSurface.tbd" "$here/sdk/CoreFoundation.tbd"
cp "$here/Info.plist" "$b/Info.plist"
echo "$b: $(stat -c %s "$b/S8MaliMetal") bytes"
