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
