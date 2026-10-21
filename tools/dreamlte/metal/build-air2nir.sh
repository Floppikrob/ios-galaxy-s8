#!/bin/bash
# Build air2nir (host): LLVM 18 + a Mesa 25.2.8 meson build configured with
# -Dtools=panfrost (see ../build-mali-blit.sh for the Mesa libraries).
#   build-air2nir.sh MESA_SRC MESA_BUILD OUT
set -euo pipefail
src=$(realpath "$1"); bld=$(realpath "$2"); out=$(realpath -m "$3")
here=$(dirname "$(realpath "$0")")
ninja -C "$bld" src/panfrost/lib/libpanfrost_lib.a src/panfrost/lib/libpan-arch-v6.a \
	src/panfrost/compiler/libpanfrost_bifrost.a src/compiler/nir/libnir.a src/util/libmesa_util.a >/dev/null
# Mesa's own flags for its v6 objects (defines, HAVE_* probes, includes).
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
            if a == '-MD' or a.endswith('.c') or a.startswith('-std=') or a.startswith('-W'):
                continue
            if a.startswith('-I') and not os.path.isabs(a[2:]):
                a = '-I' + os.path.normpath(os.path.join(bld, a[2:]))
            out.append(a)
        print(' '.join(shlex.quote(a) for a in out))
        break
PY
)
libs=$(cd "$bld" && find src -name '*.a' | grep -E 'panfrost|pan-|libnir|mesa_util|blake3|compiler|c11|libpankmod|midgard|valhall' | sed "s#^#$bld/#")
eval g++ -std=c++20 -O1 -g $flags -I"$src/src/panfrost/lib" -I"$src/src/panfrost/util" \
	$(llvm-config-18 --cxxflags | sed "s/-std=c++17//; s/-fno-exceptions//") -fexceptions \
	-Wno-deprecated-declarations -Wno-unused-parameter -fpermissive \
	'"$here/air2nir.cpp"' -o '"$out"' -Wl,--start-group $libs -Wl,--end-group \
	$(llvm-config-18 --ldflags --libs core bitreader passes ipo) \
	-lm -lpthread -lz -ldl -lexpat -ldrm -fopenmp
echo "$out"
