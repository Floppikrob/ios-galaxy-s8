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

2026-11-13: dream-mask-trace.inc adds bounded observations of runtime shape programs, their original source association, vertex/uniform/texture inputs and five destination pixels before/after actual draws. No Apple source is embedded or committed; original runtime shader hex stays in private diagnostic logs. Own ES3 synthetic circle/half-alpha/VBO/EBO fixture exercises the real helper under ASAN/UBSAN and host Mesa with exact image/state preservation. Linux test uses self process_vm_readv; iOS uses self vm_read_overwrite. No mask correction or handset runtime result is claimed from the host test.

2026-11-13: diagnostic capture is now default-off and requires explicit DREAM_MASK_TRACE=1 at build. Hardware trial showed a display identity-transition failure before the first diagnostic draw; causal link not isolated. Four complete captures produced real pure-circle corner-coverage evidence, not a confirmed derivative rootcause. Adapter restored to a41a... with fast copy; physical recovery boot pending. Default build has only original draw forwarding; enabled diagnostic and disabled-forwarding host tests pass.

2026-11-13: dream-mask-derivative.inc is an owned narrow source transformation and GLSL shim. A default-zero active uniform gives scalar circle/supercircle derivatives a signed use, choosing the quad-bounded path in the pinned first-generation Bifrost compiler; Mesa source itself is unchanged. The zero-neighbor explanation is a hardware-evidence hypothesis, not a proven ISA fault. Owned ES3 circle/supercircle fixtures compare95360pixels/allRGBA with maxdelta0 on host Mesa, with active zero-initialized guard; no captured Apple shader source is committed. Finite-input equivalence only; actual handset activation and visual result pending. Output-copy code unchanged, tracing default-off.

2026-11-14: poster-preview.m is an owned exact-process/UUID/type/IMP-guarded compatibility hook for one PosterBoard still-preview method when Metal has no device. Public UIKit bitmap APIs/normal CALayer rendering preserve the existing GLES screen compositor. Bounded runtime catalog diagnostics resolve normal system APIs dynamically. No Apple instructions, source or images are embedded. Host tests cover actual exec insertion helper and geometry limits; UIKit rendering and catalog behavior require the handset and are pending.

2026-11-14: Kernel0186 extends private never-CPU-mapped BO ownership tracking from tiler heaps to delayed-mapping render targets. Inspired by the reference cache-mode distinction, verified against pinned Mesa PAN_BO_DELAY_MMAP; S8 stays default-cached. Actual production helper tests cover CPU exposure/surface/fault/reuse rules; real-device speed result pending. Adapter/output/renderer sources unchanged.

2026-11-14:0187 adapts reference bounded hybrid waiting to synchronous G71, retaining exclusion/failure behavior. Userland vector4 packing preserves scalar/kernel byte contract; old Clang source also auto-vectorized. Host equality/state tests and native wait/pack markers passed, but heavy UI frames still158-385ms; stable30FPS not achieved. All effects must remain enabled.

2026-11-14: Large-image shared staging/GPU blit replaces CPU upload into persistent texture when eligible. Names/parameters/effects preserved, CPU fallback retained. Host model/state/failure tests pass and hardware staged1 is observed. User reports slight gain, but sampled heavy frames~104ms: stable30FPS not achieved.
