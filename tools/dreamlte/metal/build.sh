#!/bin/bash
# Build S8MaliMetal.bundle (arm64 iOS MH_BUNDLE, ad-hoc signed) into OUTDIR.
#   build.sh OUTDIR
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(realpath -m "${1:-$here/out}")
b=$out/S8MaliMetal.bundle
mkdir -p "$b"
clang -target arm64-apple-ios16.0 -O2 -Wall -Wextra -Werror -fno-stack-protector \
	-fobjc-runtime=ios-16.0 -fno-objc-arc -fno-objc-exceptions -nostdlibinc \
	-isystem "$here/../ios/stub" -c "$here/S8MaliMetal.m" -o "$out/S8MaliMetal.o"
ld64.lld-18 -arch arm64 -platform_version ios 16.0 16.0 -bundle -adhoc_codesign \
	-o "$b/S8MaliMetal" "$out/S8MaliMetal.o" \
	"$here/../ios/libSystem.tbd" "$here/sdk/libobjc.tbd" "$here/sdk/Metal.tbd"
cp "$here/Info.plist" "$b/Info.plist"
echo "$b: $(stat -c %s "$b/S8MaliMetal") bytes"
