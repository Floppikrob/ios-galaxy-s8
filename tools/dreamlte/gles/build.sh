#!/bin/bash
set -eu
: "${XNU_CC:?Set XNU_CC to an iOS arm64-capable clang wrapper}"
: "${IOS_SDK:?Set IOS_SDK to your iPhoneOS SDK directory}"
: "${MESA_DYLIB:?Set MESA_DYLIB to the Darwin Mesa 25.0.7 libOSMesa.8.dylib}"
: "${LDID:?Set LDID to the local signing tool}"
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:-libdream.gles.dylib}
"$XNU_CC" -arch arm64 -isysroot "$IOS_SDK" -O2 -DGLES_SILENCE_DEPRECATION -I"$src" \
 -dynamiclib -Wl,-x -install_name /redwood/lib/libdream.gles.dylib \
 -framework CoreFoundation -framework IOKit -framework IOSurface -lobjc \
 "$MESA_DYLIB" -o "$out" "$src/eagl.c" "$src/gl_osmesa.c" \
 "$src/surface_checksum.c" "$src/dream-gles-probe.c"
"$LDID" -S "$out"
echo "Built and locally signed $out"
