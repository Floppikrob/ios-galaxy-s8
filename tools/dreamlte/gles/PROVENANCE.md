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
