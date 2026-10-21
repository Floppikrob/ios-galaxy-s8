#!/bin/bash
# Build the iOS programs of the S8 GPU work: arm64 Mach-O, ad-hoc signed,
# linked against libSystem from the phone's shared cache (libSystem.tbd).
#   build.sh OUTDIR
set -euo pipefail
cd "$(dirname "$0")"
out=${1:-out}
mkdir -p "$out"
CFLAGS="-target arm64-apple-ios16.0 -O2 -Wall -Wextra -Werror -fno-stack-protector -ffreestanding -nostdlibinc -isystem stub"
for p in s8gputest; do
	clang $CFLAGS -c $p.c -o "$out/$p.o"
	ld64.lld-18 -arch arm64 -platform_version ios 16.0 16.0 -adhoc_codesign \
		-o "$out/$p" "$out/$p.o" libSystem.tbd
	echo "$out/$p: $(stat -c %s "$out/$p") bytes"
done
