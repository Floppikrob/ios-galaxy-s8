#!/bin/bash
# Build tools/dreamlte/mali-blit-gen against a Mesa 25.2.8 source tree and a
# meson build directory configured with -Dtools=panfrost (libraries:
# libpanfrost_lib, libpan-arch-v6, libpan-format-v6, libpanfrost_bifrost,
# libnir, libmesa_util), then generate the kernel header.
# Usage: build-mali-blit.sh MESA_SRC MESA_BUILD OUT.h [BASE_PA]
set -euo pipefail
src=$(realpath "$1"); bld=$(realpath "$2"); out=$(realpath -m "$3"); base=${4:-0x8a0000000}
here=$(dirname "$(realpath "$0")")
ninja -C "$bld" src/panfrost/lib/libpanfrost_lib.a src/panfrost/lib/libpan-arch-v6.a \
	src/panfrost/lib/libpan-format-v6.a src/panfrost/compiler/libpanfrost_bifrost.a \
	src/compiler/nir/libnir.a src/util/libmesa_util.a >/dev/null
# Same flags as Mesa's own v6 objects (endianness, HAVE_* probes, includes),
# from compile_commands.json; relative include paths are made absolute.
flags=$(python3 - "$bld" <<'PY'
import json, shlex, sys, os
bld = sys.argv[1]
for e in json.load(open(os.path.join(bld, 'compile_commands.json'))):
    if e['file'].endswith('panfrost/lib/pan_desc.c') and 'arch-v6' in e['command']:
        out, skip = [], False
        for a in shlex.split(e['command'])[1:]:
            if skip:
                skip = False
                continue
            if a in ('-o', '-MF', '-MQ', '-c'):
                skip = True
                continue
            if a == '-MD' or a.endswith('.c'):
                continue
            if a.startswith('-I') and not os.path.isabs(a[2:]):
                a = '-I' + os.path.normpath(os.path.join(bld, a[2:]))
            out.append(a)
        print(' '.join(shlex.quote(a) for a in out))
        break
PY
)
libs=$(cd "$bld" && find src -name '*.a' | grep -E 'panfrost|pan-|libnir|mesa_util|blake3|compiler|c11|libpankmod|midgard|valhall' | sed "s#^#$bld/#")
eval cc $flags -I"$src/src/panfrost/lib" '"$here/mali-blit-gen.c"' -Wl,--start-group $libs -Wl,--end-group \
	-lm -lpthread -lz -ldl -lexpat -ldrm -fopenmp -o '"$bld/mali-blit-gen"'
"$bld/mali-blit-gen" "$base" "$out"
