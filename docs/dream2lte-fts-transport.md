# Dream2lte FTS transport diagnostic, 2026-11-12

Observed on Galaxy S8+: the working fts-fixed image reaches the iOS UI but user reports no touch response. Fresh raw-event image retains its boot args (debug=0xc serial=3 cpus=2 -noprogress, a5fb_noconsole=1, sweetfb_native=1, s8touch=2). Both CPUs initialized, SGI1 verified; SpringBoard executes.

The raw log prints 80 identical packets: `00 36 70 01 00 26 23 00`, matching the chip-ID response. No down/up events or I2C failures appear. This suggests stale data or command/transaction handling, but does not prove the cause. Do not shift event offsets based on this packet.

Patch 0125 is an experimental isolation test: reset the touch HSI2C controller before each complete FTS operation and trace the first 16 transaction FIFO/state snapshots. It preserves repeated START, event parsing and FTS initialization commands. It does not reset the touchscreen or write firmware, calibration or NVM. The battery bus and S6SY761 path are unchanged.

Validation: tooling source matches patch 0124; patch 0125 applies to that source with the XNU prefix; FTS snippet passes clang syntax with warnings treated as errors. Hardware result pending.

Next: build XNU, package using the exact working boot arguments, preserve BOOT, flash BOOT only, then collect events after physical taps. If the image fails to boot, restore the saved working BOOT. Current device is in TWRP at this handoff.

Local logs, images and checkpoint details are kept in the Codex chat outputs, outside git. Preserve the repository's pre-existing modifications.


## FTS8CD56 handshake test (2026-11-12)

Physical test confirmed the local LLVM-built kernel boots iOS with 2/2 CPUs and SGI1 verified. STOP-separated reads still return 00 36 70 01 00 26 23 00, so neither reset-only HSI2C nor STOP separation fixed touch.

Native recovery sysfs identifies the controller as fts8cd56. Compared Samsung driver source at https://github.com/jesec/android_kernel_samsung_universal8895/blob/tw-7.0/drivers/input/touchscreen/stm/fts_ts.c (fts_systemreset, fts_wait_for_ready, fts_reinit): volatile reset B6 00 28 80, wait for ready event 0x10, flush A1, sense on 93, optional pressure on 9C, controller interrupt enable B6 00 2C 48. Coordinate packing agrees with current event parser. 9C was mislabeled as factory/interrupt and is now named pressure sensing.

Patch 0127 adds optional s8fts_reinit=1, with bounded ready polling and explicit failure logging. No flash, calibration, NVM or firmware-update commands. Default init behavior remains available. Ready timeout stops the touch init path but does not prevent iOS startup.

Locally built and validated (no undefined symbols, no code LSE). Flashed dream2lte-local-fts-reinit.img with cpus=2, s8touch=2, s8fts_reinit=1, default repeated START. SHA256 0a78ff32e9381f7d1a5e780586efeaece4086b619a4e93a01381f543389a635a. BOOT readback matched, reboot issued. Physical ready/touch result pending. Backup boot-before-fts-reinit.img preserved in Codex outputs. New changes remain local.


## TX FIFO prefill test (2026-11-12)

Reinit runtime log fts-reinit-test.txt confirms ready event `10 00 00 00 00 26 0d 01`, then chip ID read succeeds, sensing and interrupt setup finish. Subsequent READ_EVENT packets again repeat the chip-ID bytes. Two CPUs initialized. This confirms reset/ready communication but not that the short polling commands are transmitted correctly.

Patch 0128 adds optional s8fts_prefill=1. For the FTS address 0x49 only and writes up to 64 bytes, it fills TX FIFO before AUTO_RUN, verifies queued count equals command length, and records the first 16 queue snapshots. Legacy ordering remains default; other I2C devices are unchanged. No change to event offsets, calibration, firmware or NVM.

Full local build and checks passed; no undefined symbols or LSE code instructions. Current flashed image dream2lte-local-fts-prefill.img SHA256 e72e3ece9ccc968b426c65f84ee4d0d8ca4f66f86f765c515933ca0cc285a979, bytes 16130064; BOOT readback matches; reboot issued. Args retain cpus=2 with s8fts_reinit=1 s8fts_prefill=1, default repeated START. Physical touch/queue-count result pending. Backup boot-before-fts-prefill.img is in Codex outputs.


## GitHub research and board-specific comparison (2026-11-12)

User requested a GitHub-based investigation after TX prefill still failed. No new image flashed during this research turn; device remains in TWRP with the prefill image in BOOT.

Sources actually read:
1. Samsung Exynos8895 touchscreen implementation: https://github.com/jesec/android_kernel_samsung_universal8895/blob/tw-7.0/drivers/input/touchscreen/stm/fts_ts.c and fts_ts.h. Native recovery reports fts8cd56. Event packing matches our parser; full initial init includes FORCECALIBRATION A2 before interrupt enable. Current experimental port deliberately omitted calibration; that is a remaining init difference, not proof of cause.
2. Detailed same-controller port report, DIFFERENT DEVICE SM-T385/T380: https://github.com/akprof2000/samsung-t385-postmarketos/blob/main/docs/RAZBOR.md, section Touchscreen, and kernel/0001-stmfts-samsung-fts8-init.patch. Confirms Samsung reset/ready/IRQ behavior and IRQ enable after wake. Its board, GPIO, supplies and timings must not be copied as S8+ facts.
3. Board controller source: https://github.com/jesec/android_kernel_samsung_universal8895/blob/tw-7.0/drivers/i2c/busses/i2c-exynos5.c. TX completion checks TRANSFER_DONE plus TX_FIFO_EMPTY; when scl_clk_stretch is enabled it additionally waits on TRANS_STATUS: low state nibble zero for a final STOP, allowing MASTER_BUSY for a non-final message. Timeout is bounded.

Direct phone evidence: /sys/class/i2c-adapter/i2c-5/device/of_node resolves to hsi2c@108E0000 and has samsung,scl-clk-stretching, samsung,check-transdone-int, samsung,reset-before-trans. No samsung,stop-after-trans property is present. This confirms the correct bus and makes the Samsung completion check relevant to this physical board.

Latest fts-prefill-test.txt: TX queued matches lengths4,1,3; reset returns ready0x10; ID36 70; subsequent events still repeat ID. One successful IRQ-enable write ends with TRANS_STATUS=0x0002000d, i.e. master busy/non-idle state. Therefore failure to enqueue TX is not supported, and an interrupt done flag alone can precede bus idle in this trace.

Next grounded change: wait for final STOP completion/state idle before clearing CTL channels or resetting HSI2C for the next FTS operation. Current legacy path exits on INT_STATUS and clears CTL immediately; only optional split-read helper waits idle. This is a hypothesis supported by a board/source discrepancy, not a demonstrated root cause. Keep calibration as a separate later variable. Preserve cpus=2 and boot backups.


## Final STOP completion test (2026-11-12)

Patch 0129 implements the board-specific completion check requested after the GitHub comparison. Optional s8fts_stopwait=1, FTS address0x49 only: after final TRANSFER_DONE and before CTL channel disable, poll TRANS_STATUS low nibble until zero and require TX_FIFO_EMPTY. Wait is bounded to100ms; the first16 completions and rate-limited failures record initial/final state, FIFO and wait time. Timeout fails the transaction and uses the existing recovery path. No initialization/calibration/event-format change.

Local build and checks passed: ARM64 Mach-O, no undefined symbols, no LSE code instructions. Kernel SHA2563cc374901e9f2808eac2b95f1cdd70d2cbefd12501d227920a64c2fa951896fa. Flashed dream2lte-local-fts-stopwait.img SHA2562ec3ad5ecc01542f9842d301e8d740700acee9b4779578526d13aa7cd42609e6 (16130064 bytes). BOOT prefix readback matched; reboot issued. Prior full BOOT saved as boot-before-fts-stopwait.img. Test keeps cpus=2, s8fts_reinit=1, s8fts_prefill=1, adds only s8fts_stopwait=1. Hardware result pending. Changes are local, not pushed.


## Runtime calibration test (2026-11-12)

User confirms touchscreen works in TWRP. Native dmesg contains real press/release records. The STOP-wait iOS log reports complete=1, state0, empty TX for all traced operations, but READ_EVENT still repeats chip-ID bytes. STOP waiting has not resolved the touch problem.

Patch 0130 adds independent optional s8fts_calibrate=1: normal FORCECALIBRATION command A2 after sensing has settled, followed by1000ms delay before controller IRQ enable, as supported by the Samsung init/same-controller port sources. No flash unlock/burn/erase or SAVE_FWCONFIG/SAVE_CX_TUNING commands are sent. Default remains off. Do not describe touch as fixed before a physical test.

Build validated locally (NOUNDEFS, no LSE code). Flashed dream2lte-local-fts-calibrate.img SHA256b517f69c02adb73327a629ca2b26d4e4033087fc0c006d3accbc79a95505f759,16130064bytes. Prefix readback matches. Reboot issued; hardware result pending. Keeps cpus=2, reinit/prefill/stopwait enabled; adds only calibration. Backup boot-before-fts-calibrate.img preserved in outputs. No external push.


## ID probe ordering isolation (2026-11-12)

Calibration test still has no physical touch. Log confirms A2 issued and settling finished, but event reads still repeat ID bytes. Across reinit/prefill/STOP/calibration tests, the ready event is valid before the ID register read and ID bytes repeat afterwards. Correlation does not establish cause.

Patch0131 adds optional s8fts_probe_first=1: read/validate ID first, then perform existing software reset and ready handshake. No ID register read occurs after reset before polling. All other settings retained, including A2. This isolates ID-read ordering; default order unchanged.

Locally built and validated. Flashed dream2lte-local-fts-probe-first.img SHA256e4389d4f89cb9fc04188e57fe478d860a883f2e80fb6150bd58d7d246b010dbc (16130064bytes). Header validated, BOOT readback matched, reboot issued. cpus=2 retained. Hardware result pending. Previous BOOT is boot-before-fts-probe-first.img. Local-only changes.


## Counted FIFO event path (2026-11-12)

Probe-first test changes the repeated polling response from chip-ID bytes to repeated ready packet10 00 00 00 00 26 0d 01. Physical touch still absent. This supports a stale/undrained response problem but does not identify its cause.

Samsung FTS8CD56 interrupt handler uses count register B6 00 23, reads2bytes, computes count=byte1/2, clamps to32, then reads count*8bytes using86. Patch0132 adds optional s8fts_bulk=1 using that path in a polling worker (IRQ not routed in XNU). The85 helper remains only for startup-ready and legacy mode. Count/IRQ logs are rate limited; raw86 logs are capped/throttled; all event buffers bounded256bytes. Existing event parser/HID path unchanged.

Full local build/checks pass. Flashed dream2lte-local-fts-bulk.img SHA256bb3c46813e155be7157a67b02eb112499779aa11f3365c448812745755f15d21,16130064bytes. BOOT readback matches; reboot issued. Prior BOOT saved in boot-before-fts-bulk.img. cpus=2 and previous init settings retained; only adds s8fts_bulk=1. Physical result pending. No external push.


## MILESTONE: physical touchscreen working; 4-CPU image prepared (2026-11-12)

User explicitly reported touch works after booting dream2lte-local-fts-bulk.img. This establishes a working complete configuration with cpus=2 and counted bulk FIFO mode, not that all earlier experimental flags are necessary. Preserve this image: SHA256bb3c46813e155be7157a67b02eb112499779aa11f3365c448812745755f15d21. Swipes/edges/release validation has not yet been reported separately.

User requested4CPUs. Prepared dream2lte-fts-working-4cpu.img from the exact working BOOT, changing only cpus=2 to cpus=4 and the Android header SHA-1. Kernel, loader, ADT and touch settings are byte-identical. SHA2560fbe7e57398b081a77d6d246649f5379095de4fedf2f5929ba2cc9c0b1dcbe7a,16130064bytes. Not flashed: phone not visible in ADB, likely still running iOS. Wait for user to enter TWRP; then preserve the actual working full BOOT checkpoint, flash4CPU image, verify prefix readback and reboot. Physical4CPU result pending; later verify requested=4 selected=4 and4/4 initialized in fresh log.


## Four-CPU trial flashed (2026-11-12)

Preserved actual working2CPU full BOOT as boot-working-touch-2cpu.img,41943040bytes, SHA256eab6d071cbae6e75aad611cb1e9edb069040ac174d3fb65c454665818ef972ec. Device full-partition checksum matches; prefix matches exactly the confirmed working bulk image. Fresh working-touch-2cpu-log.txt confirms2/2 CPUs and numerous FTS down/up events (including multiple finger IDs).

Flashed dream2lte-fts-working-4cpu.img SHA2560fbe7e57398b081a77d6d246649f5379095de4fedf2f5929ba2cc9c0b1dcbe7a,16130064bytes. BOOT prefix readback matches. Reboot issued. Only cpus=2 to4 and header checksum differ from the confirmed working image. Physical boot/touch and4/4 CPU log confirmation pending. Roll back to preserved working BOOT if needed; no USERDATA writes were performed by flashing.


## Four CPUs verified; eight-CPU trial flashed (2026-11-12)

Fresh4cpu-test.txt proves requested=4 selected=4, CPU0..3 MPIDR0x100..0x103,4/4initialized and SGI1verified. Physical user report: iOS and touch work. Log contains FTS down/up events. Working4CPU full BOOT saved as boot-working-touch-4cpu.img (41943040bytes), SHA2562d7ba612f0131850c0946e864514165df41575ee8e82ef7a3d88db354ae42210. Device checksum matches and prefix matches prepared4CPU image.

Reviewed existing code: MAX8, topology maps logical4..7 to Mongoose MPIDR0..3, CPU_ON/PMU sequencing already implemented; GIC target masks learned per CPU; generic secondary entry handles EL1/EL2. This is source-level readiness, not evidence that the second cluster works. No new kernel code was needed for a first runtime trial.

Prepared8CPU image by changing only cpus=4 to8 and header SHA-1. Embedded ADT parsed and verified8nodes [0x100,0x101,0x102,0x103,0,1,2,3]. Kernel, loader, ADT and touch config otherwise byte-identical to working4CPU image. Flashed dream2lte-fts-working-8cpu.img SHA256e45f3a9eca0df59db0dff689c485bc9ebe8c50251cbc7372cd22fc097c325780,16130064bytes; BOOT prefix readback matches. Reboot issued. Physical boot/touch and fresh8/8CPU/SGI confirmation pending. Preserve4CPU checkpoint for rollback.
Prepared8CPU image by changing only cpus=4 to8 and header SHA-1. Embedded ADT parsed and verified8nodes [0x100,0x101,0x102,0x103,0,1,2,3]. Kernel, loader, ADT and touch config otherwise byte-identical to working4CPU image. Flashed dream2lte-fts-working-8cpu.img SHA256e45f3a9eca0df59db0dff689c485bc9ebe8c50251cbc7372cd22fc097c325780,16130064bytes; BOOT prefix readback matches. Reboot issued. Physical boot/touch and fresh8/8CPU/SGI confirmation pending. Preserve4CPU checkpoint for rollback.

2026-11-12: Eight-core image panicked before CPU_ON: cpu4-7 IOKit registration timeout (photo). Confirmed KernelConfigTables.cpp matched only arm,cortex-a53 while ADT M2 nodes advertise samsung,mongoose-m2. Added incremental patch0133 to match both compatibles. Local build passed, no undefined symbols or executable LSE instructions. Flashed dream2lte-8cpu-mongoose-match.img (16130064 bytes), SHA256 8eeb4d347cb4c7af4240e63e531cf01755edf587df47efb22437b319fb7cce97; readback matched. Reboot sent; hardware result pending. Current preflash BOOT backup boot-before-mongoose-match.img SHA256 9cbb8a8199d7a7996f48c552d60de758b533c6ced419731e7e01156cbe77ed69. Known working four-core backup preserved. Touch flags unchanged. This fixes driver matching; M2 startup remains unverified.

2026-11-12: Mongoose matching fix eliminated registration timeout. Photo now shows cpu0 and cpu1 SGI1 OK, then panic on CPU0 while starting cpu2 after low-trampoline message: ticket lock fffffff007b28880 unexpectedly owned @lock_ticket.c143. Symbol resolves inside pset0 (base fffffff007b28780), likely sched_lock; exact caller not yet known, no normal backtrace due early handshake. No M2 CPU_ON reached. Added patch0134: A5-only backtrace(12 frames) included directly in owned-lock panic; lock semantics unchanged. Build and checksum passed; image dream2lte-8cpu-lock-diagnostic.img SHA256 16cfb4e649f7b4ef9715f5b3c10f36f009885eb8c83299691a011e68b7ff3142 size16130064 flashed/readback verified and rebooted. Preflash full BOOT outputs/boot-before-lock-diagnostic.img SHA256 66e578b0a63ab704c85a75d7d716f992500fa66a06128fe9237c7551bbc46e6a. Await photo with bt= chain; symbolize using exact current local kernel. Not a proven fix, diagnostic only.

2026-11-12: Diagnostic photo frames abfb74,72c92d8,72e524c,72e490c symbolize to owned-panic, lck_ticket_contended, thread_select, thread_block_reason. Disassembly return72e524c is pset_lock at sched_prim.c after SCHED(steal_thread). Root cause: sched_clutch_steal_thread stub returns NULL WITHOUT unlocking; generic sched_steal_thread_enabled sees two psets from ADT, so single-cluster Clutch stub is incorrectly called on dream2lte two-cluster topology. Added patch0135 DREAMLTE-only SCHED dispatch selection to built-in sched_traditional_dispatch. It has actual multi-pset steal implementation and drops lock on all return paths. NO_ECORE symmetric model retained, physical topology unchanged; M2 power-up still unverified. Build passed, undefined0/LSEcode0, reverse patch dry-run passes. Flashed outputs/dream2lte-8cpu-traditional.img size16130064 SHA256272000e2b7c0d3e2ffdcbb646d02170d9c0b260f93050db108603f57f3af0294; prefix readback matches; reboot sent. Full preflash outputs/boot-before-two-cluster-scheduler.img SHA2564e485148b670a67ac5bccec7e91566525561f0f01346994060f1c08a311313a6. Pending hardware result. Touch settings unchanged. Patch0134 diagnostic retained.
