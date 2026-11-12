# Local Galaxy S8+ XNU build (WSL Ubuntu)

Configured and tested on 2026-11-12. Existing tree and pre-existing user changes were preserved. Source baseline is the previously downloaded A5XELTE/dreamlte build tree, with the dreamlte touch/battery block synchronized from tools/dreamlte/dreamlte_touch.c on each build.

## Run

In WSL:

    ~/S8Port/linux-xnu/s8-buildtree/build-local.sh

Or in PowerShell:

    wsl -d Ubuntu -- /home/rybce/S8Port/linux-xnu/s8-buildtree/build-local.sh

The script defaults to `build`. Kernel output:

    ~/S8Port/linux-xnu/s8-buildtree/work/build/xnu.obj/kernel.development.a5xelte

Override job count with XNU_LOCAL_JOBS (default 4). The script updates only the embedded touch/battery block from the port repository; other future port patches must also be applied to the local source tree before they can appear in a local kernel.

## Working configuration

* Existing UniXNU Linux toolchain in /opt/unixnu-linux: patched LLVM 24, Mach-O tools, iig, mig. ARM64 target, Cortex-A53, ARMv8.0+crypto.
* Linux host programs use /usr/bin/clang and /usr/bin/ld; target objects use the Darwin cross compiler.
* Plain Mach-O objects, BUILD_LTO=0, matching the installed cctools linker.
* Warning policy reduced to Wall/Wextra for this older XNU under a newer Clang. Real compilation/link errors remain fatal; new Clang pointer compatibility diagnostic is a warning.
* Existing generated CI paths were relocated; originals are in work/local-test/relocation-backup.
* Local compiler compatibility changes in kalloc.h preserve the void-pointer wildcard and XNU type-signature checks without dereferencing void pointers in C++. Corecrypto uses modern vector assembler syntax. Originals are in work/local-test/linux-compat-backup.
* Real libfirehose_kernel.a built from Apple libdispatch-1415.140.2 (cdb61e93e803d0093635f356753a9a30c09836db).
* Real libcc_kext.a profile sources built from llvmorg-14.0.6, whose six-argument internal profile APIs match XNU8796. Newer compiler-rt has a different ABI and was not used. The profile-only source copy guards an unused stdlib include under KERNEL_USE. No replacement runtime stubs were introduced.
* Libraries use explicit local paths for linking. No network access is needed for subsequent builds.

## Validation

Full local make and a subsequent incremental build exited 0. Kernel is ARM64 Mach-O, NOUNDEFS|PIE. Current FTS reset/split markers exist. Undefined symbols: 0. LSE instructions in executable code: 0. A raw disassembly match is the known word 0x78a5636f inside the 256-byte SHA-256 K256 constant table; table boundaries and first/last words were verified before excluding it.

The ready BOOT dream2lte-local-fts-split.img was packaged, header checksum checked, flashed to BOOT only, read back by SHA-256, and rebooted. Physical boot/touch result is pending. Working CI BOOT backups remain in the chat outputs.

## Current FTS evidence

CI commit 25d060c builds and boots. Reset-before-transaction does not fix touch: repeated READ_EVENT responses are still 00 36 70 01 00 26 23 00. Most snapshots show idle/empty queues; one shows master busy/read state after reported completion. This does not establish the cause.

Current local patch 0126 adds optional s8fts_split=1: finish STOP after command, wait for bus idle, delay 100 us, then read response and wait for idle. Default repeated START is unchanged. Event parsing, firmware/calibration/NVM are unchanged. Test image uses cpus=2 and the working boot args plus s8fts_split=1. Source/patch changes are local; only earlier commit 25d060c was published with user approval.
