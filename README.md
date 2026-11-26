# iOS on the Samsung Galaxy S8 and S8+

This branch ports XNU 8796 and the iPhone X iOS 16 userland to Samsung phones with Exynos 8895. The latest hardware tests are on the Galaxy S8+ (SM-G955F, `dream2lte`). The original Galaxy S8 (SM-G950F, `dreamlte`) status and its separate installation guide are below.

## Galaxy S8+ status

| Area | Verified on SM-G955F |
| --- | --- |
| iOS interface | SpringBoard, lock screen and touch input |
| CPU and memory | 8 CPU cores, 2 GiB usable RAM and 4 KiB pages |
| Graphics | Hardware OpenGL ES rendering through Mesa/Panfrost on Mali-G71, with an EAGL bridge and 8 GPU contexts |
| Display | 1125 x 2436 render surface, GPU copy and scaling to the 1440 x 2960 panel, and pipelined scanout through the DPU IOMMU |
| Latest graphics test | Kernel 196 passed display startup. The vector upload build logged 8 successful direct uploads, at least 330 completed flips without DMA or MMU errors, and 10 matching pixel checks |

Stable 30 FPS is still pending. Heavy scenes can stall even without texture uploads. Metal remains outside the verified graphics path.

See the [S8+ bring-up notes](docs/dream2lte-bringup.md), the [latest test results and handoff](docs/CONTINUE_IOS_S8_PLUS.md), and the [driver journal](docs/dream2lte-fts-transport.md). The S8 installation instructions below target SM-G950F. The S8+ uses a different partition layout and requires its own verified images and backups.

## Original Galaxy S8 status

**iOS 16.7 on the Samsung Galaxy S8 (SM-G950F, Exynos 8895, `dreamlte`).**

This project ports Apple's open-source XNU kernel (xnu-8796, the iOS 16 generation) to the Galaxy S8. On top of it runs the **unmodified iOS 16.7.16 (20H392) userland of the iPhone X**: launchd, the system daemons, backboardd and SpringBoard. The phone reaches the **iOS lock screen**.

> ⚠️ Experimental bring-up, not a usable phone OS. It is very slow and much is missing (see [Known bugs](#known-bugs)). Installing it **erases the USERDATA partition** (all Android data). Exynos **SM-G950F** only.

## What works

| Area | State |
| --- | --- |
| Boot | Stock Samsung S-Boot → our loader in the BOOT partition → XNU. Recovery (TWRP), EFS and the bootloaders are not touched |
| CPU | 4 Cortex-A53 cores (MPIDR 0x100–0x103), started through the PMU + PSCI. 4 KiB pages, GIC-400, 26 MHz timer |
| Memory | 512 MiB of the upper bank at `0x880000000` |
| Storage | Own Exynos UFS driver. The iOS root is HFS+ in USERDATA (`sda24`), read/write |
| iOS userland | iPhone X 16.7.16 root filesystem and shared cache; launchd, daemons, backboardd, SpringBoard |
| Display | S-Boot framebuffer, 1440×2960 (DECON in command mode). CoreAnimation renders on the CPU |
| GUI | Apple logo, iOS lock screen (confirmed on the device) |
| Buttons | Power, volume up/down, Bixby (ALIVE GPIOs) → HID events (seen in the log) |
| Clock | Date and time from the S2MPS17 RTC, read by the loader |
| Touch | S6SY761 on `hsi2c_23`: the loader powers its rails (S2MPS17 LDO34/35 over SPEEDY), a polled kernel driver feeds HID touch events; taps and swipes reach SpringBoard |
| Logs | No need to film the screen: loader and kernel log in ramoops (pmsg) plus a 4 MiB ring at the end of USERDATA, read from TWRP |

## Known bugs

- **Very slow and laggy.** All rendering is on the CPU, there is no GPU. About 15 minutes to the lock screen on one core.
- The iPhone X 1125×2436 mode hangs on its first frame; the native 1440×2960 mode is used (`sweetfb_native=1`).
- The Mongoose (big) cluster does not start yet: its L2/cluster power must be enabled.
- No battery / charging readout (no fuel gauge driver).
- No Wi-Fi, Bluetooth, cellular, audio, cameras, sensors, GPS or SEP / Face ID.
- **No GPU.** The Mali-G71 cannot run iOS's Metal / AGX drivers, so hardware graphics is not realistic.
- Security is off: permissive AMFI/sandbox, stub AppleKeyStore. Do not put anything personal on it.

## Roadmap

- [x] S6SY761 touch driver in XNU
- [x] Fix the 4-core stalls (physical timer)
- [ ] Bring up the Mongoose cluster (4 more cores), raise the A53 clock (now ~1050 of 1690 MHz)
- [ ] Faster display path: zero-copy scanout through the DPU input DMA (probe in progress), 1125x2436 presentation (being tested)
- [ ] 1125×2436 mode without the hang
- [x] RTC (date/time)
- [ ] Battery stub or driver (MAX77865 fuel gauge)
- [ ] More RAM (512 MiB of 4 GiB today)
- [ ] Vibration motor, backlight brightness
- [ ] Wi-Fi (BCM4361): a large job

Help is welcome: issues and pull requests.

## Installation

Step-by-step guide: **[docs/s8-install.md](docs/s8-install.md)**.

In short:
1. An unlocked SM-G950F with TWRP and a full partition backup.
2. The XNU kernel is built on GitHub Actions (workflow `xnu`, `MACHINE_CONFIG=A5XELTE`).
3. The iOS root image is built from the iPhone X 16.7.16 IPSW, which you download yourself, and is written to USERDATA.
4. `tools/dreamlte/make-boot.sh` builds the BOOT image (loader + XNU + Apple Device Tree), which is written to `sda7` from TWRP.

## How it boots

```
S-Boot (stock Samsung bootloader, EL1, MMU off)
  -> BOOT partition: shim/dreamlte (our loader instead of a Linux Image)
     -> unmasks the DECON trigger, logs to ramoops, wakes UFS
     -> copies XNU + ADT from the ramdisk to 0x880000000, builds boot_args
        -> XNU (xnu-8796, A5XELTE config + patch 0124-dreamlte-s8)
           -> UFS driver -> HFS+ root in USERDATA
              -> dyld + shared cache -> launchd -> backboardd + SpringBoard
```

## Layout

| Path | What it is |
| --- | --- |
| `shim/dreamlte/` | Loader for S-Boot: `loader.c`, UFS / SMP / touch probes |
| `xnu/patches/` | XNU patches; the S8-specific one is `0124-dreamlte-s8.patch` |
| `tools/dreamlte/apply-dreamlte.py` | Generator of patch 0124 |
| `tools/mkadt_dreamlte.py` | Apple Device Tree for the S8 |
| `tools/dreamlte/make-boot.sh` | Kernel → BOOT image |
| `tools/mkiosroot.sh`, `tools/dreamlte/finish-iosroot.sh` | iOS root image build |
| `tools/dreamlte/flash-chunks.ps1` | Writes an image to USERDATA in verified chunks |
| `tools/dreamlte/pull-sweetlog.ps1` | Pulls the boot logs from TWRP |
| `DREAMLTE_PROGRESS.md` | Port log: hardware facts, every boot |

## Authors

- [@Floppikrob](https://github.com/Floppikrob): Galaxy S8 port
- [@MakrSas](https://github.com/MakrSas): contributor, mainline Linux on the S8 ([LinuxS8](https://github.com/MakrSas/LinuxS8))

## Credits

- [orhunnx/ios-a5xelte](https://github.com/orhunnx/ios-a5xelte): the Galaxy A5 2016 port (4 KiB pages, Exynos, S-Boot) this project is based on
- [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet): the original iOS 16 port to the Redmi Note 10 Pro (its README: [docs/upstream-README.md](docs/upstream-README.md))
- [MakrSas/LinuxS8](https://github.com/MakrSas/LinuxS8): mainline Linux on the Galaxy S8, the reference for touch, buttons and UFS
- Mainline Linux (Exynos 8895, HSI2C and S6SY761 drivers), [uniLoader](https://github.com/ivoszbg/uniLoader)
- Apple's open-source releases (xnu, IOHIDFamily, hfs), [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build), [ipsw](https://github.com/blacktop/ipsw), [apfs-fuse](https://github.com/sgan81/apfs-fuse)

iOS belongs to Apple and is not distributed here: download the IPSW yourself.

## License

The XNU patches are derived from XNU and fall under the [APSL 2.0](https://opensource.apple.com/apsl/). Code written after Linux drivers (for example `shim/dreamlte/touch_probe.c`) is GPL-2.0, see the SPDX headers. The upstream projects ios-sweet and ios-a5xelte did not state an overall license, so the remaining files have no separate license yet.
