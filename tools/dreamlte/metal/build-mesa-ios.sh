#!/bin/bash
# Build the parts of Mesa 25.2.8 the S8 Metal plugin uses at run time
# (panfrost v6 descriptors: framebuffer, textures, blending, formats, job
# chains; plus the util code they need) as an iOS arm64 static library.
#   build-mesa-ios.sh MESA_SRC MESA_BUILD IOS_SDK OUT.a   (also writes OUT.flags)
# MESA_BUILD is the host meson build (generated headers and tables are
# target independent); compile flags are taken from it and stripped of the
# Linux-specific HAVE_* probes.
set -euo pipefail
src=$(realpath "$1"); bld=$(realpath "$2"); sdk=$(realpath "$3"); out=$(realpath -m "$4")
here=$(dirname "$(realpath "$0")")
obj=$(dirname "$out")/mesa-ios-obj
mkdir -p "$obj"
flags=$(python3 - "$bld" <<'PY'
import json, shlex, sys, os
bld = sys.argv[1]
drop = {'-DHAVE_ENDIAN_H', '-DMAJOR_IN_SYSMACROS', '-DHAS_SCHED_H', '-DHAS_SCHED_GETAFFINITY',
        '-DHAVE_LINUX_FUTEX_H', '-DHAVE_SYS_INOTIFY_H', '-DHAVE_CET_H', '-DHAVE_LINUX_UDMABUF_H',
        '-DHAVE_MEMFD_CREATE', '-DHAVE_RANDOM_R', '-DHAVE_SECURE_GETENV', '-DHAVE_GNU_QSORT_R',
        '-DHAVE_PROGRAM_INVOCATION_NAME', '-DHAVE_ISSIGNALING', '-DHAVE_DL_ITERATE_PHDR',
        '-DHAVE_POSIX_FALLOCATE', '-DHAVE_GETRANDOM', '-DUSE_SSE41', '-DUSE_X86_64_ASM',
        '-DHAVE___BUILTIN_IA32_CLFLUSHOPT', '-DHAVE_LIBDRM', '-DHAVE_OPENMP', '-DHAVE_REALLOCARRAY',
        '-DHAVE_PTHREAD_SETAFFINITY', '-DHAVE_SPIRV_TOOLS', '-DALLOW_KCMP', '-DHAVE_MKOSTEMP',
        '-D_GNU_SOURCE', '-DHAVE_SYS_SHM_H', '-DENABLE_SHADER_CACHE', '-DHAVE_FUNC_ATTRIBUTE_ALIAS',
        '-pthread', '-fdiagnostics-color=always', '-DHAVE_ZLIB', '-DHAVE_COMPRESSION', '-DHAVE_STRTOD_L'}
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
            if a == '-MD' or a.endswith('.c') or a.startswith('-mtls') or a.startswith('-W') or a in drop:
                continue
            if a.startswith('-D') and 'PAN_ARCH' in a:
                continue
            if a.startswith('-I') and not os.path.isabs(a[2:]):
                a = '-I' + os.path.normpath(os.path.join(bld, a[2:]))
            out.append(a)
        print(' '.join(shlex.quote(a) for a in out))
        break
PY
)
# Paths are shell-quoted (they may contain spaces): every use goes through eval.
q() { printf '%q' "$1"; }
clang=$(command -v clang-18 || command -v clang)
inc="-isysroot $(q "$sdk") -I$(q "$here/ios-stub") -I$(q "$src/src/util") -I$(q "$src/src/util/format") -I$(q "$bld/src/util/format")"
cc="$(q "$clang") -target arm64-apple-ios16.0 $inc -Wno-everything -fno-stack-protector"
# The same flags compile s8mtl_draw.c (metal/build.sh reads them from here).
echo "$inc -I$(q "$src/src/panfrost/lib") $flags -DPAN_ARCH=6" > "${out%.a}.flags"
objs=()
compile() {     # compile SRC TAG [extra flags]
	local s=$1 tag=$2; shift 2
	local o=$obj/$tag-$(basename "${s%.*}").o
	eval $cc $flags "$@" -c "$(q "$s")" -o "$(q "$o")" || { echo "FAILED $s"; return 0; }
	objs+=("$o")
}
for f in pan_desc pan_texture pan_mod; do
	compile "$src/src/panfrost/lib/$f.c" v6 -DPAN_ARCH=6
done
compile "$src/src/panfrost/lib/pan_format.c" fmt6 -DPAN_ARCH=6
for f in pan_afbc pan_clear pan_earlyzs pan_layout pan_samples pan_scratch pan_tiler pan_util pan_props; do
	compile "$src/src/panfrost/lib/$f.c" lib
done
for f in bitscan half_float ralloc hash_table set u_debug log os_misc simple_mtx format/u_format \
    format/u_format_table format/u_format_pack format/u_format_unpack format/u_format_other \
    format/u_format_zs format/u_format_s3tc format/u_format_rgtc format/u_format_latc \
    format/u_format_etc format/u_format_bptc format/u_format_fxt1 format/u_format_yuv format_srgb \
    format_r11g11b10f softfloat u_math rand_xor rgtc format/u_format_unpack_neon; do
	s="$src/src/util/$f.c"
	[ -f "$s" ] || s="$bld/src/util/$f.c"
	[ -f "$s" ] || { echo "skip $f"; continue; }
	compile "$s" util
done
# C11 call_once and friends (util's format tables use them).
compile "$src/src/c11/impl/threads_posix.c" c11
compile "$src/src/c11/impl/time.c" c11
# Fixed-function blending: the common part of pan_blend.c without its
# NIR blend-shader half (which would pull in the whole compiler).
glue=$obj/pan_blend_ff.c
{
	cat <<'C'
#include "genxml/gen_macros.h"
#include "util/blend.h"
#include "util/bitscan.h"
#include "util/macros.h"
#include "util/compiler.h"
#include <strings.h>
struct pan_blend_equation {
   unsigned blend_enable                  : 1;
   enum pipe_blend_func rgb_func          : 3;
   enum pipe_blendfactor rgb_src_factor   : 5;
   enum pipe_blendfactor rgb_dst_factor   : 5;
   enum pipe_blend_func alpha_func        : 3;
   enum pipe_blendfactor alpha_src_factor : 5;
   enum pipe_blendfactor alpha_dst_factor : 5;
   unsigned color_mask                    : 4;
   unsigned padding                       : 1;
};
static inline float
pan_blend_get_constant(unsigned mask, const float *constants)
{
   return mask ? constants[ffs(mask) - 1] : 0.0;
}
C
	awk '/^\/\* Fixed function blending \*\//{on=1} /^enum mali_register_file_format/{on=0} on' \
	    "$src/src/panfrost/lib/pan_blend.c"
} > "$glue"
compile "$glue" ff -DPAN_ARCH=6
rm -f "$out"
llvm-ar-18 rcs "$out" "${objs[@]}"
echo "$out: ${#objs[@]} objects"
