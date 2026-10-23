#!/bin/bash
# Why air2nir fails on some functions: for each one, the NIR intrinsics and
# texture ops in its final NIR (before the Bifrost back end) and the error.
#   diag-shaders.sh METALLIB AIR2NIR FUNC [FUNC...]
# The varying table is the one build-shaders.py uses for the whole library.
set -uo pipefail
lib=$1 air2nir=$2
shift 2
here=$(cd "$(dirname "$0")" && pwd)
table=$(python3 -c "
import sys, tempfile
sys.path.insert(0, '$here')
import importlib.util
spec = importlib.util.spec_from_file_location('bs', '$here/build-shaders.py')
bs = importlib.util.module_from_spec(spec); spec.loader.exec_module(bs)
import metallib
funcs = [f for f in metallib.parse(open('$lib', 'rb').read()) if f['type'] in ('vertex', 'fragment')]
with tempfile.TemporaryDirectory() as tmp:
    slots = bs.colour([bs.varyings_of(f['bitcode'], tmp, f['name']) for f in funcs])
print(','.join(f'{n}={s}' for n, s in sorted(slots.items(), key=lambda x: x[1])))
")
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for f in "$@"; do
	"$air2nir" "$lib" "$f" "$tmp/$f.s8sh" -D -v "$table" -d > "$tmp/$f.log" 2>&1
	rc=$?
	echo "=== $f (exit $rc)"
	# NIR as printed before compiling (the last "shader:" block of -d output)
	awk '/^shader: /{buf=""} {buf=buf $0 "\n"} END{printf "%s", buf}' "$tmp/$f.log" > "$tmp/$f.nir"
	echo "intrinsics: $(grep -oE '@[a-z0-9_]+' "$tmp/$f.nir" | sort | uniq -c | sort -rn | awk '{printf "%s%s(%s)", sep, $2, $1; sep=" "}')"
	echo "tex ops: $(grep -oE '\b(tex|txb|txl|txd|txf|txf_ms|txs|lod|tg4|query_levels|texture_samples|samples_identical|fragment_fetch_amd|txf_ms_fb)\b [^ ]* ' "$tmp/$f.nir" | awk '{print $1}' | sort | uniq -c | awk '{printf "%s(%s) ", $2, $1}')"
	grep -nE 'Assertion|should.ve been|unsupported|error|air2nir:' "$tmp/$f.log" | tail -5
	# the 12 NIR lines that use the 64-bit or unusual operations near the failure
	grep -nE '@(load_global|load_ubo|store|image|load_sample|load_frag|load_helper|ddx|ddy|load_barycentric|load_interpolated)|i64|u64' "$tmp/$f.nir" | head -12
done
