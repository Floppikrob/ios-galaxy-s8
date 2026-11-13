# Dream2lte GLES trial sources

EAGL adapter and Darwin Mesa transport donor: https://github.com/danstronics/ios-s9plus-exynos.git, commit 2d092398040f98685b0c5a07776df5e9f199b9dc.
The source files retain their original license notices. xxHash retains its origin file and license.
The new dream-gles-probe.c is a hardware triangle/readback gate before EAGL hooks.
Kernel transport is in patch 0168, adapted for the S8+ G71 high RAM bank and existing eight blit contexts.

Only the kernel and two locally signed dylibs have been built; successful UI acceleration is not yet established.
Local build output: /home/rybce/S8Port/gles-port. Mesa source/patches/build configuration are in s9-reference/core/tools/mesa and xnu-deps/build/mesa.
To run the production transport host test after applying 0168:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Wno-format -I/path/to/xnu/iokit/Kernel/arm test-transport.cpp -o test-transport
./test-transport
```

-Wno-format handles the Linux/Darwin difference in uint64_t printf typedefs; it does not alter production code.

2026-11-13: Local b3a8 SDR display bridge added after actual CoreAnimation output attachment was refused. CPU pack/unpack and readback are an initial correctness path; full HDR/gamut and UI acceleration are not claimed. dream_b3a8.h is identical to kernel DreamB3a8.h in patch0174; production extraction tests enforce this. -Wl,-x strips local nonexported symbols to fit existing adapter file data without HFS metadata edits.

2026-11-13: ca-gl-bridge.c narrowly restores donor inject.c context/function-uniform forwarding omitted from the original isolated EAGL build. Runtime guards use UUID and vtable metadata independently resolved for the actual local iOS16 cache; no Apple instructions are distributed or patched. Original support decisions preserved. Kernel0177 retains normal cache sync and optimizes only never-CPU-mapped tiler heap;0178 snapshots immutable caller images after protection failure. All new hardware results pending.

2026-11-13: dream-group-mask.inc preserves the original runtime fragment shader and vertex inputs while compositing a deferred vibrant group. Its local GLSL wrapper consumes the original source alpha; no Apple shader text/binary is embedded in the new source/tests. Own synthetic circle/second-texture/discard fixture is rendered by optional host Mesa EGL via ctypes; ASAN/UBSAN tests extract actual production assembly/group begin/end. Host LIBGL_ALWAYS_SOFTWARE is confined to that test subprocess, not handset settings. Device visual result pending; existing shared BGRA output code remains unchanged.
