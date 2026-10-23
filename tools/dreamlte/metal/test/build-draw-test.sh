#!/bin/bash
# Build and run the host test of the draw path (test/draw-test.c) against a
# Mesa 25.2.8 build tree (-Dtools=panfrost) and air2nir.
#   build-draw-test.sh MESA_SRC MESA_BUILD AIR2NIR OUTDIR
set -euo pipefail
src=$(realpath "$1"); bld=$(realpath "$2"); air2nir=$(realpath "$3"); out=$(realpath -m "$4")
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
# the test cache: the three built-in shaders
for f in @preload @test_vs @test_fs; do
	"$air2nir" none "$f" "$out/$f.s8sh" -D 2>/dev/null
done
python3 - "$out" <<'PY'
import struct, sys, os
out = sys.argv[1]
names = ['@preload', '@test_vs', '@test_fs']
blobs = [open(os.path.join(out, n + '.s8sh'), 'rb').read() for n in names]
head = 12 + 72 * len(names)
index, data, off = b'', b'', head
for n, b in zip(names, blobs):
    index += n.encode().ljust(64, b'\0') + struct.pack('<II', off, len(b))
    data += b
    off += len(b)
open(os.path.join(out, 'test.s8sc'), 'wb').write(b'S8SC' + struct.pack('<II', 1, len(names)) + index + data)
PY
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
            if a == '-MD' or a.endswith('.c') or a.startswith('-W'):
                continue
            if a.startswith('-I') and not os.path.isabs(a[2:]):
                a = '-I' + os.path.normpath(os.path.join(bld, a[2:]))
            out.append(a)
        print(' '.join(shlex.quote(a) for a in out))
        break
PY
)
libs=$(cd "$bld" && find src -name '*.a' | grep -E 'panfrost|pan-|pandecode|libnir|mesa_util|blake3|compiler|c11|libpankmod|midgard|valhall' | sed "s#^#$bld/#")
eval cc $flags -DS8_TEST -I"$src/src/panfrost/lib" -I"$here/.." -idirafter "$here/../../ios/stub" -Wall -Wno-unused-parameter -g \
	'"$here/draw-test.c"' '"$here/../s8mtl_draw.c"' -Wl,--start-group $libs -Wl,--end-group \
	-lm -lpthread -lz -ldl -lexpat -ldrm -fopenmp -o '"$out/draw-test"'
"$out/draw-test" "$out/test.s8sc" > "$out/draw-test.log" 2>&1 && rc=0 || rc=$?
grep -E '^(====|S8 MALI)' "$out/draw-test.log"
echo "decode log: $out/draw-test.log (exit $rc)"
exit $rc
