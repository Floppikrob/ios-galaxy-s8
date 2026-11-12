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
