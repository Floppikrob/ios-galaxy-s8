#!/usr/bin/env python3
"""Apply the Galaxy S8 (dreamlte, Exynos 8895) hardware layer to an
A5XELTE-patched xnu tree (tools-s8/prep-xnu-tree.sh output).

The A5XELTE machine config and its guards are reused unchanged. What
differs on the S8 is expressed as A5_* macros in A5XELTE.h, with the A5
values kept for a non-DREAMLTE build. Every replacement asserts its
expected match count. Usage: apply-dreamlte.py XNU_TREE
"""
import sys
from pathlib import Path

tree = Path(sys.argv[1])


def edit(rel, pairs):
    path = tree / rel
    text = path.read_text()
    for old, new, count in pairs:
        found = text.count(old)
        if found != count:
            raise SystemExit(f'{rel}: expected {count}x {old!r}, found {found}')
        text = text.replace(old, new)
    path.write_text(text)
    print(f'edited {rel}')


edit('pexpert/pexpert/arm64/A5XELTE.h', [(
    '#define A5XELTE_VTIMER_INTID         27\n',
    '#define A5XELTE_VTIMER_INTID         27\n'
    '\n'
    '/*\n'
    ' * Samsung Galaxy S8 (SM-G950F, dreamlte, Exynos 8895) build of this port.\n'
    ' * Same Samsung S-Boot handoff, GIC-400, 26 MHz timer and Cortex-A53 boot\n'
    ' * cluster as the A5; the board facts that differ are below.\n'
    ' */\n'
    '#define DREAMLTE                      1\n'
    '#if defined(DREAMLTE)\n'
    '#define A5_FB_PHYS                    0xcc000000  /* s3cfb.bootloaderfb */\n'
    '#define A5_FB_WIDTH                   1440\n'
    '#define A5_FB_HEIGHT                  2960\n'
    '#define A5_FB_STRIDE                  5760        /* bytes per row */\n'
    '#define A5_FB_2M_BLOCKS               9           /* 1440*2960*4 bytes */\n'
    '#define A5_ENTRY_MARKER_PHYS          0x920ff000  /* slot in the loader\'s pstore text */\n'
    '#define A5_BOOT_MPIDR                 0x100       /* first Cortex-A53 */\n'
    '#define A5_KEYS_DEFAULT               1           /* S8 keys: GPA0/GPA2 */\n'
    '#define A5_TOUCH_DEFAULT              0           /* A5 ZT7538 path absent */\n'
    '#define A5_KLOG_PHYS                  0x92180000  /* TWRP ramoops pmsg zone */\n'
    '#define A5_KLOG_SIZE                  0x80000\n'
    '/* Samsung\'s S8 kernel DT: arch timer "use-physical-timer". The EL1\n'
    ' * virtual timer is not usable on the secondary cores (boot 22/24: 4\n'
    ' * CPUs up, then sleeping threads and workqueue threads never ran).\n'
    ' * Use the non-secure physical timer, PPI 14 = INTID 30. */\n'
    '#define A5_USE_PHYS_TIMER             1\n'
    '#undef A5XELTE_VTIMER_INTID\n'
    '#define A5XELTE_VTIMER_INTID         30\n'
    '#else\n'
    '#define A5_FB_PHYS                    0x67000000\n'
    '#define A5_FB_WIDTH                   1080\n'
    '#define A5_FB_HEIGHT                  1920\n'
    '#define A5_FB_STRIDE                  4320\n'
    '#define A5_FB_2M_BLOCKS               5\n'
    '#define A5_ENTRY_MARKER_PHYS          0x469ff100\n'
    '#define A5_BOOT_MPIDR                 0\n'
    '#define A5_KEYS_DEFAULT               1\n'
    '#define A5_TOUCH_DEFAULT              1\n'
    '#endif\n', 1)])

# Early assembly: entry/MMU/stage blocks are 36x21 px at x=240, rows 88..216.
stage_rows = {'0x6705d0c0': 88, '0x6707ecc0': 120, '0x670a08c0': 152,
              '0x670c24c0': 184, '0x670e40c0': 216}
pairs = [('MOV64\tx10, 0x469ff100', 'MOV64\tx10, A5_ENTRY_MARKER_PHYS', 1)]
for addr, row in stage_rows.items():
    pairs.append((f'MOV64\tx10, {addr}',
                  f'MOV64\tx10, (A5_FB_PHYS + {row} * A5_FB_STRIDE + 240 * 4)', 1))
pairs += [
    ('mov\t\tx15, #4320', 'mov\t\tx15, #A5_FB_STRIDE', 2),
    ('mov\tx15, #4320', 'mov\tx15, #A5_FB_STRIDE', 3),
    ('MOV64\tx0, 0x67000000\n\tmov\t\tx4, x0\n\tmov\t\tx5, #5\n',
     'MOV64\tx0, A5_FB_PHYS\n\tmov\t\tx4, x0\n\tmov\t\tx5, #A5_FB_2M_BLOCKS\n', 1),
    ('MOV64\tx10, 0x67000000', 'MOV64\tx10, A5_FB_PHYS', 1),
    ('mov\t\tx13, #5\nLa5_fb_attr:', 'mov\t\tx13, #A5_FB_2M_BLOCKS\nLa5_fb_attr:', 1),
    ('MOV64\tx4, 0x67000000\n\tmov\tx5, #4320',
     'MOV64\tx4, A5_FB_PHYS\n\tmov\tx5, #A5_FB_STRIDE', 1),
    ('mov\tx13, #8640', 'mov\tx13, #(2 * A5_FB_STRIDE)', 1),
    ('/* Five 2 MiB blocks map the 1080x1920 scanout buffer VA=PA.',
     '/* A5_FB_2M_BLOCKS 2 MiB blocks map the scanout buffer VA=PA.', 1),
]
edit('osfmk/arm64/start.s', pairs)

edit('osfmk/arm64/sweet_early.c', [
    ('#define A5_FB ((volatile uint32_t *)(uintptr_t)0x67000000)\n#define A5_STRIDE 1080u\n',
     '#define A5_FB ((volatile uint32_t *)(uintptr_t)A5_FB_PHYS)\n#define A5_STRIDE (A5_FB_STRIDE / 4u)\n', 1),
    ('#define A5_COLS (1080u / A5_COL_WIDTH)', '#define A5_COLS (A5_FB_WIDTH / A5_COL_WIDTH)', 1),
    ('(1920u / A5_ROW_HEIGHT)', '(A5_FB_HEIGHT / A5_ROW_HEIGHT)', 1),
    ('for (unsigned int x = 0; x < A5_STRIDE; x++) {', 'for (unsigned int x = 0; x < A5_FB_WIDTH; x++) {', 1),
])

edit('iokit/Kernel/arm/SweetFramebuffer.cpp', [
    ('#include <pexpert/pexpert.h>\n',
     '#include <pexpert/pexpert.h>\n#include <pexpert/arm64/board_config.h>\n', 1),
    ('\tfPhys = 0x67000000ULL;\n\tfWidth = 1080;\n\tfHeight = 1920;\n\tfRowBytes = 4320;\n',
     '\tfPhys = A5_FB_PHYS;\n\tfWidth = A5_FB_WIDTH;\n\tfHeight = A5_FB_HEIGHT;\n\tfRowBytes = A5_FB_STRIDE;\n', 1),
])

edit('iokit/Kernel/arm/SweetCPU.cpp', [
    ('\tuint32_t expected_mpidr = cpu->cpu_id < 4 ? cpu->cpu_id :\n\t    0x100U + cpu->cpu_id - 4;\n',
     '#if defined(DREAMLTE)\n'
     '\t/* Exynos 8895: logical 0..3 = A53 MPIDR 0x100..0x103 (boot cluster);\n'
     '\t * logical 4..7 = Mongoose M2 MPIDR 0..3. */\n'
     '\tuint32_t expected_mpidr = cpu->cpu_id < 4 ? 0x100U + cpu->cpu_id :\n'
     '\t    cpu->cpu_id - 4;\n'
     '#else\n'
     '\tuint32_t expected_mpidr = cpu->cpu_id < 4 ? cpu->cpu_id :\n'
     '\t    0x100U + cpu->cpu_id - 4;\n'
     '#endif\n', 1),
    ('\t/* The A5 ADT describes CPU0 as the boot CPU. */\n'
     '\tif (topology->boot_cpu->cpu_id != 0 || topology->boot_cpu->phys_id != 0) {\n'
     '\t\tpanic(CPU_PORT_NAME " CPU: expected boot MPIDR 0 and logical CPU 0");\n',
     '\t/* The ADT describes logical CPU 0 as the boot CPU. */\n'
     '\tif (topology->boot_cpu->cpu_id != 0 || topology->boot_cpu->phys_id != A5_BOOT_MPIDR) {\n'
     '\t\tpanic(CPU_PORT_NAME " CPU: expected boot MPIDR %#x and logical CPU 0", A5_BOOT_MPIDR);\n', 1),
])

edit('pexpert/arm/pe_serial.c', [
    ('static uint32_t fbcon_cols, fbcon_rows, fbcon_col, fbcon_row;\n',
     'static uint32_t fbcon_cols, fbcon_rows, fbcon_col, fbcon_row;\n'
     '\n'
     '#if defined(DREAMLTE)\n'
     '/*\n'
     ' * Copy of the console in TWRP\'s ramoops pmsg zone, in persistent_ram\n'
     ' * format (sig "DBGC", start, size, ring data), appended to the loader\'s\n'
     ' * text there. After a forced reboot TWRP shows loader + kernel log as\n'
     ' * /sys/fs/pstore/pmsg-ramoops-0. (The dump zone survives too but this\n'
     ' * TWRP does not expose it; the console zone header sits on RAM cells that\n'
     ' * lose the same bits on every forced reboot.)\n'
     ' */\n'
     '#define KLOG_CAP        (A5_KLOG_SIZE - 12)\n'
     'static volatile uint32_t *klog_hdr;\n'
     'static volatile uint8_t *klog_data;\n'
     'static uint32_t klog_start, klog_size;\n'
     'static void klog_putc(uint8_t c);\n'
     '\n'
     'static void\n'
     'klog_init(void)\n'
     '{\n'
     '\tklog_hdr = (volatile uint32_t *)ml_io_map(A5_KLOG_PHYS, A5_KLOG_SIZE);\n'
     '\tif (klog_hdr == NULL) {\n'
     '\t\treturn;\n'
     '\t}\n'
     '\tklog_data = (volatile uint8_t *)(klog_hdr + 3);\n'
     '\t/* Continue after the loader\'s text if its header is sane. */\n'
     '\tif (klog_hdr[0] == 0x43474244u && klog_hdr[1] == klog_hdr[2] &&\n'
     '\t    klog_hdr[2] < KLOG_CAP - 0x10000) {\n'
     '\t\tklog_start = klog_size = klog_hdr[2];\n'
     '\t} else {\n'
     '\t\tklog_hdr[0] = 0x43474244u;\n'
     '\t\tklog_start = klog_size = 0;\n'
     '\t}\n'
     '\tconst char *banner = "\\n--- XNU console ---\\n";\n'
     '\tfor (; *banner; banner++) {\n'
     '\t\tklog_putc((uint8_t)*banner);\n'
     '\t}\n'
     '}\n'
     '\n'
     'static void\n'
     'klog_putc(uint8_t c)\n'
     '{\n'
     '\tif (klog_data == NULL) {\n'
     '\t\treturn;\n'
     '\t}\n'
     '\tklog_data[klog_start] = c;\n'
     '\tklog_start = (klog_start + 1) % KLOG_CAP;\n'
     '\tif (klog_size < KLOG_CAP) {\n'
     '\t\tklog_size++;\n'
     '\t}\n'
     '\tklog_hdr[1] = klog_start;\n'
     '\tklog_hdr[2] = klog_size;\n'
     '}\n'
     '#endif\n', 1),
    ('fbcon_transmit_data(uint8_t c)\n{\n',
     'fbcon_transmit_data(uint8_t c)\n{\n'
     '#if defined(DREAMLTE)\n'
     '\tklog_putc(c);\n'
     '#endif\n', 1),
    ('\tfbcon_base = (volatile uint32_t *)ml_io_map_wcomb(reg[0], reg[1]);\n',
     '\tfbcon_base = (volatile uint32_t *)ml_io_map_wcomb(reg[0], reg[1]);\n'
     '#if defined(DREAMLTE)\n'
     '\tklog_init();\n'
     '#endif\n', 1),
])

# Boots 3, 5, 6 and 7 hung right after the kernel version line, before
# pmap_startup's "init/release time"; boots 1, 2, 3b and 5b got past it with
# the same image. The physical aperture slide (1 GiB range, early_random) is
# the boot-to-boot variable there (gPicBase differs on every photo). Pin it
# on the S8 and mark vm_page_bootstrap's steps in the console.
edit('osfmk/arm64/arm_vm_init.c', [
    ('\tuint64_t physmap_slide = early_random() & ARM64_PHYSMAP_SLIDE_MASK & ~((uint64_t)PAGE_MASK);\n',
     '#if defined(DREAMLTE)\n'
     '\tuint64_t physmap_slide = 0;\n'
     '#else\n'
     '\tuint64_t physmap_slide = early_random() & ARM64_PHYSMAP_SLIDE_MASK & ~((uint64_t)PAGE_MASK);\n'
     '#endif\n', 1),
    # Boot 14: a 4 MiB larger kernel (sweet_ufs log ring) moved
    # topOfKernelData and hung pmap_startup again. Only XNU_MONITOR builds
    # round physmap_base to a twig (2 MiB with 4 KiB pages); do it here too.
    # The real cause of the vm_page_bootstrap hangs (boots 3-7, 14, 15):
    # arm_vm_init pre-builds L3 tables only for (2 + 10 * 256 MiB-segments)
    # MiB + the boot_args framebuffer past dynamic_memory_begin. Our loader
    # passes no Video, yet the console maps the 17 MiB S8 framebuffer there
    # first; when pmap_steal_memory then needs a table beyond the budget,
    # pmap_enter cannot allocate one yet. With 4 KiB pages one L3 table spans
    # 2 MiB (32 MiB with 16 KiB), so add room for the framebuffer and 64 MiB.
    ('\tva_l1_end += round_page(args->Video.v_height * args->Video.v_rowBytes);\n',
     '\tva_l1_end += round_page(args->Video.v_height * args->Video.v_rowBytes);\n'
     '#if defined(DREAMLTE)\n'
     '\tva_l1_end += round_page((vm_size_t)A5_FB_STRIDE * A5_FB_HEIGHT) + (64ULL << 20);\n'
     '#endif\n', 1),
    ('\tphysmap_base += physmap_slide;\n',
     '\tphysmap_base += physmap_slide;\n'
     '#if defined(DREAMLTE) && !XNU_MONITOR\n'
     '\tphysmap_base = ROUND_TWIG(physmap_base);\n'
     '#endif\n', 1),
])

edit('osfmk/vm/vm_resident.c', [
    ('\tpmap_startup(&virtual_space_start, &virtual_space_end);\n',
     '#if defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '\tkprintf("A5 vm: vm_page_bootstrap -> pmap_startup\\n");\n'
     '#endif\n'
     '\tpmap_startup(&virtual_space_start, &virtual_space_end);\n', 1),
    ('\tvm_pages = (vm_page_t) pmap_steal_freeable_memory(npages * sizeof *vm_pages);\n',
     '\tvm_pages = (vm_page_t) pmap_steal_freeable_memory(npages * sizeof *vm_pages);\n'
     '#if defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '\tkprintf("A5 vm: npages=%u vm_pages=%p mem_sz=%#llx\\n", npages, vm_pages, (unsigned long long)mem_sz);\n'
     '#endif\n', 1),
    ('\t\tif (!pmap_next_page(&phys_page)) {\n\t\t\tbreak;\n\t\t}\n\n\t\tif (phys_page < max_valid_low_ppnum) {\n',
     '\t\tif (!pmap_next_page(&phys_page)) {\n\t\t\tbreak;\n\t\t}\n'
     '#if defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '\t\tif ((i & 0x7fff) == 0) {\n'
     '\t\t\tkprintf("A5 vm: page %u ppn %#x\\n", i, phys_page);\n'
     '\t\t}\n'
     '#endif\n'
     '\n\t\tif (phys_page < max_valid_low_ppnum) {\n', 1),
    ('\tvm_page_pages = vm_pages_count; /* used to report to user space */\n',
     '\tvm_page_pages = vm_pages_count; /* used to report to user space */\n'
     '#if defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '\tkprintf("A5 vm: %u pages initialized, releasing\\n", vm_pages_count);\n'
     '#endif\n', 1),
])

# ---- Galaxy S8 internal UFS: the SWEET driver with the Exynos 8895 quirks ----
# Proven by the loader probe (shim/dreamlte/ufs_probe.c, S8 boot 13): HCE
# reads 0 while running, UTRD sizes in bytes, 16-byte PRDT entries of at
# most 4 KiB, NEXUS_TYPE bit per SCSI slot, and the device stays NOT READY
# (04/02) until START STOP UNIT (power condition ACTIVE) to W-LUN 0xD0.
edit('bsd/dev/sweet_ufs.c', [
    ('#if defined(ARM64_BOARD_CONFIG_SWEET)\n#include <sys/param.h>\n',
     '#if defined(ARM64_BOARD_CONFIG_SWEET) || defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '#include <sys/param.h>\n', 1),
    ('#define CAP_64AS        (1u << 24)              /* 64-bit DMA addressing */\n',
     '#define CAP_64AS        (1u << 24)              /* 64-bit DMA addressing */\n'
     '\n'
     '#if defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '/*\n'
     ' * Galaxy S8 (Exynos 8895) host, per the Samsung 4.4 driver and the S8\n'
     ' * loader probe (shim/dreamlte/ufs_probe.c): UTRD offsets and lengths are\n'
     ' * in bytes (UFSHCD_QUIRK_BROKEN_DWORD_UTRD), PRDT entries are 16 bytes and\n'
     ' * cover at most 4 KiB (HCI_TXPRDT_ENTRY_SIZE 12), each SCSI slot needs its\n'
     ' * HCI_UTRL_NEXUS_TYPE bit, the standard HCE register reads 0 while the host\n'
     ' * runs (UFSHCD_QUIRK_USE_OF_HCE) and UTRLCLR is inverted\n'
     ' * (UFSHCD_QUIRK_BROKEN_REQ_LIST_CLR). S-Boot leaves the device stopped\n'
     ' * (sense 02/04/02): START STOP UNIT, power condition ACTIVE, to the UFS\n'
     ' * Device well-known LUN wakes it.\n'
     ' */\n'
     '#define EXYNOS_UFS              1\n'
     '#define VS_UTRL_NEXUS_TYPE      0x40\n'
     '#define UFS_WLUN_DEVICE         0xd0\n'
     '#define UFS_ADT_NODE            "/dreamlte-ufs"\n'
     '#define UFS_DEFAULT_PART        "USERDATA"\n'
     '#define PRDT_BYTES(field)       (field)\n'
     '#else\n'
     '#define UFS_ADT_NODE            "/sweet-ufs"\n'
     '#define UFS_DEFAULT_PART        "userdata"\n'
     '#define PRDT_BYTES(field)       ((field) * 16u)\n'
     '#endif\n', 1),
    ('#define PRDT_CHUNK      (256u << 10)            /* PRDT byte count field: 18 bits */\n',
     '#if defined(EXYNOS_UFS)\n'
     '#define PRDT_CHUNK      4096u                   /* HCI_TXPRDT_ENTRY_SIZE 12 */\n'
     '#else\n'
     '#define PRDT_CHUNK      (256u << 10)            /* PRDT byte count field: 18 bits */\n'
     '#endif\n', 1),
    ('\tvm_offset_t regs;\n',
     '\tvm_offset_t regs;\n\tvm_offset_t vs_regs;                    /* Exynos vendor HCI registers */\n', 1),
    ('\tutrd->resp = (512u / 4) << 16 | (512u / 4);\n',
     '#if defined(EXYNOS_UFS)\n'
     '\tutrd->resp = 512u << 16 | 512u;\n'
     '#else\n'
     '\tutrd->resp = (512u / 4) << 16 | (512u / 4);\n'
     '#endif\n', 1),
    ('\t\tutrd->prdt = (1024u / 4) << 16 | n;\n',
     '#if defined(EXYNOS_UFS)\n'
     '\t\tutrd->prdt = 1024u << 16 | (n * (uint32_t)sizeof(ucd->prdt[0]));\n'
     '#else\n'
     '\t\tutrd->prdt = (1024u / 4) << 16 | n;\n'
     '#endif\n', 1),
    ('\t\t\t    (utrd->prdt & 0xffff) * sizeof(ucd->prdt[0]));\n',
     '\t\t\t    PRDT_BYTES(utrd->prdt & 0xffff));\n', 1),
    ('\tufs_wr(REG_UTRLDBR, 1);\n',
     '#if defined(EXYNOS_UFS)\n'
     '\t*(volatile uint32_t *)(su.vs_regs + VS_UTRL_NEXUS_TYPE) |= 1;  /* slot 0: SCSI */\n'
     '#endif\n'
     '\tufs_wr(REG_UTRLDBR, 1);\n', 1),
    ('\t\t\tufs_wr(REG_UTRLCLR, ~1u);\n',
     '#if !defined(EXYNOS_UFS)\n'
     '\t\t\tufs_wr(REG_UTRLCLR, ~1u);\n'
     '#endif\n', 1),
    ('\tif (!(hce & 1) || !(hcs & HCS_DP) || !(hcs & HCS_UTRLRDY)) {\n',
     '#if defined(EXYNOS_UFS)\n'
     '\t(void)hce;\n'
     '\tif (!(hcs & HCS_DP) || !(hcs & HCS_UTRLRDY)) {\n'
     '#else\n'
     '\tif (!(hce & 1) || !(hcs & HCS_DP) || !(hcs & HCS_UTRLRDY)) {\n'
     '#endif\n', 1),
    ('\tchar want[64] = "userdata";\n', '\tchar want[64] = UFS_DEFAULT_PART;\n', 1),
    ('\tif (SecureDTLookupEntry(NULL, "/sweet-ufs", &node) != kSuccess ||\n'
     '\t    SecureDTGetProperty(node, "reg", (void const **)&reg, &size) != kSuccess ||\n'
     '\t    size < 2 * sizeof(uintptr_t)) {\n'
     '\t\tprintf("SWEET UFS: no /sweet-ufs node in the device tree\\n");\n',
     '\tif (SecureDTLookupEntry(NULL, UFS_ADT_NODE, &node) != kSuccess ||\n'
     '\t    SecureDTGetProperty(node, "reg", (void const **)&reg, &size) != kSuccess ||\n'
     '#if defined(EXYNOS_UFS)\n'
     '\t    size < 4 * sizeof(uintptr_t)) {\n'
     '#else\n'
     '\t    size < 2 * sizeof(uintptr_t)) {\n'
     '#endif\n'
     '\t\tprintf("SWEET UFS: no " UFS_ADT_NODE " node in the device tree\\n");\n', 1),
    ('\tsu.regs = ml_io_map(reg[0], reg[1]);\n',
     '\tsu.regs = ml_io_map(reg[0], reg[1]);\n'
     '#if defined(EXYNOS_UFS)\n'
     '\tsu.vs_regs = ml_io_map(reg[2], reg[3]);\n'
     '#endif\n', 1),
    ('\terror = ufs_take_over_host();\n',
     '\terror = ufs_take_over_host();\n'
     '#if defined(EXYNOS_UFS)\n'
     '\tif (!error) {\n'
     '\t\tstatic const uint8_t ssu_active[6] = { 0x1b, 0, 0, 0, 0x10, 0 };\n'
     '\t\tsu.lun = UFS_WLUN_DEVICE;\n'
     '\t\terror = ufs_scsi(ssu_active, sizeof(ssu_active), 0, false);\n'
     '\t\tsu.lun = 0;\n'
     '\t\tprintf("SWEET UFS: START STOP UNIT (ACTIVE) to the device W-LUN: %d\\n", error);\n'
     '\t}\n'
     '#endif\n', 1),
    ('\tsweet_sample_start();\n\t/* Input drivers (HID services in iokit/Kernel/arm/hid/SweetHID.cpp). */\n'
     '\tsweet_keys_init();\n\tsweet_touch_init();\n\tsweet_usb_probe();\n',
     '\tsweet_sample_start();\n'
     '#if defined(ARM64_BOARD_CONFIG_SWEET)\n'
     '\t/* Input drivers (HID services in iokit/Kernel/arm/hid/SweetHID.cpp). */\n'
     '\tsweet_keys_init();\n\tsweet_touch_init();\n\tsweet_usb_probe();\n'
     '#endif\n', 1),
])

# The SWEET user-stack sampler (bsd/dev/sweet_sample.c; its kernel hook in
# exception.c is already built for A5XELTE): stacks of SpringBoard and
# backboardd every 2 minutes, to see where 4-CPU rendering stalls (boot 22).
edit('bsd/dev/sweet_sample.c', [
    ('#if defined(ARM64_BOARD_CONFIG_SWEET)\n/*\n * SWEET process sampler',
     '#if defined(ARM64_BOARD_CONFIG_SWEET) || defined(ARM64_BOARD_CONFIG_A5XELTE)\n/*\n * SWEET process sampler', 1),
])

# rd=ufsroot0 also on the A5XELTE machine config (the S8 build).
edit('iokit/bsddev/IOKitBSDInit.cpp', [
    ('#if defined(ARM64_BOARD_CONFIG_SWEET)\n//\n//\tSWEET: root on a GPT partition of the internal UFS (bsd/dev/sweet_ufs.c)\n',
     '#if defined(ARM64_BOARD_CONFIG_SWEET) || defined(ARM64_BOARD_CONFIG_A5XELTE)\n//\n'
     '//\tSWEET / Galaxy S8: root on a GPT partition of the internal UFS (bsd/dev/sweet_ufs.c)\n', 1),
])

# Console copy into sweet_ufs.c's ring (written to the end of the root
# partition every ufslogsecs when ufsrw=1): longer than pstore, no bit flips.
edit('osfmk/console/serial_console.c', [
    ('#if defined(ARM64_BOARD_CONFIG_SWEET)\nextern void sweet_log_append',
     '#if defined(ARM64_BOARD_CONFIG_SWEET) || defined(ARM64_BOARD_CONFIG_A5XELTE)\nextern void sweet_log_append', 1),
    ('#if defined(ARM64_BOARD_CONFIG_SWEET)\n\tsweet_log_append(c, size);',
     '#if defined(ARM64_BOARD_CONFIG_SWEET) || defined(ARM64_BOARD_CONFIG_A5XELTE)\n\tsweet_log_append(c, size);', 1),
])

# Physical instead of virtual EL1 timer on the S8 (A5_USE_PHYS_TIMER).
edit('osfmk/arm64/machine_routines.c', [
    ('\t\t__builtin_arm_wsr64("CNTV_TVAL_EL0", (uint64_t)dec_value);\n',
     '#if defined(A5_USE_PHYS_TIMER)\n'
     '\t\t__builtin_arm_wsr64("CNTP_TVAL_EL0", (uint64_t)dec_value);\n'
     '#else\n'
     '\t\t__builtin_arm_wsr64("CNTV_TVAL_EL0", (uint64_t)dec_value);\n'
     '#endif\n', 1),
    ('\t\twide_val = __builtin_arm_rsr64("CNTV_TVAL_EL0");\n',
     '#if defined(A5_USE_PHYS_TIMER)\n'
     '\t\twide_val = __builtin_arm_rsr64("CNTP_TVAL_EL0");\n'
     '#else\n'
     '\t\twide_val = __builtin_arm_rsr64("CNTV_TVAL_EL0");\n'
     '#endif\n', 1),
    ('\tuint64_t cntv_ctl = __builtin_arm_rsr64("CNTV_CTL_EL0");\n',
     '#if defined(A5_USE_PHYS_TIMER)\n'
     '\tuint64_t cntv_ctl = __builtin_arm_rsr64("CNTP_CTL_EL0");\n'
     '#else\n'
     '\tuint64_t cntv_ctl = __builtin_arm_rsr64("CNTV_CTL_EL0");\n'
     '#endif\n', 1),
    ('\t__builtin_arm_wsr64("CNTV_CTL_EL0", cntvctl);\n'
     '\t/* disable the physical timer as a precaution, as its registers reset to architecturally unknown values */\n'
     '\t__builtin_arm_wsr64("CNTP_CTL_EL0", CNTP_CTL_EL0_IMASKED);\n',
     '#if defined(A5_USE_PHYS_TIMER)\n'
     '\t__builtin_arm_wsr64("CNTP_CTL_EL0", cntvctl);   /* same ENABLE bit */\n'
     '\t__builtin_arm_wsr64("CNTV_CTL_EL0", CNTV_CTL_EL0_IMASKED);\n'
     '#else\n'
     '\t__builtin_arm_wsr64("CNTV_CTL_EL0", cntvctl);\n'
     '\t/* disable the physical timer as a precaution, as its registers reset to architecturally unknown values */\n'
     '\t__builtin_arm_wsr64("CNTP_CTL_EL0", CNTP_CTL_EL0_IMASKED);\n'
     '#endif\n', 1),
])

edit('pexpert/arm/pe_fiq.c', [
    ('\t    a5_gic_read32(a5_gicd_base, 0x080) | (1U << 27) | (1U << 1));\n',
     '\t    a5_gic_read32(a5_gicd_base, 0x080) | (1U << A5XELTE_VTIMER_INTID) | (1U << 1));\n', 1),
    ('\ta5_gic_write32(a5_gicd_base, 0x100, (1U << 27) | (1U << 1));\n',
     '\ta5_gic_write32(a5_gicd_base, 0x100, (1U << A5XELTE_VTIMER_INTID) | (1U << 1));\n', 1),
])

# IOKit platform expert / CPU personalities: also match the S8 names.
# Present the S8 panel as the iPhone X screen (1125x2436, centered on the
# 1440x2960 panel), like SWEET does: iOS has no layout data for 1440x2960.
edit('iokit/Kernel/arm/SweetFramebuffer.cpp', [
    ('#if !defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '\tif (!native && fWidth <= SWEET_FB_LOGICAL_W && fHeight <= SWEET_FB_LOGICAL_H) {\n',
     '#if defined(DREAMLTE)\n'
     '\tif (!native) {\n'
     '\t\tfLogicalW = SWEET_FB_LOGICAL_W;\n'
     '\t\tfLogicalH = SWEET_FB_LOGICAL_H;\n'
     '\t}\n'
     '#elif !defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '\tif (!native && fWidth <= SWEET_FB_LOGICAL_W && fHeight <= SWEET_FB_LOGICAL_H) {\n', 1),
])

edit('iokit/KernelConfigTables.cpp', [
    ("    \"     'IONameMatch'     = 'samsung,a5xelte-display';\"\n",
     "    \"     'IONameMatch'     = ('samsung,a5xelte-display', 'samsung,dreamlte-display');\"\n", 1),
    ("    \"     'IONameMatch'     = 'samsung,a5xelte';\"\n",
     "    \"     'IONameMatch'     = ('samsung,a5xelte', 'samsung,dreamlte');\"\n", 1),
])

# A5 touch probe maps Exynos7580 I2C/GPIO blocks: a bus error on the S8.
edit('bsd/dev/a5xelte_touch.c', [
    ('#include <pexpert/pexpert.h>\n',
     '#include <pexpert/pexpert.h>\n#include <pexpert/arm64/board_config.h>\n', 1),
    ('a5xelte_touch_probe(void)\n{\n\tint enabled = 1;\n',
     'a5xelte_touch_probe(void)\n{\n\tint enabled = A5_TOUCH_DEFAULT; /* A5 touch: off on the S8 */\n', 1),
])
# No platform clock on the S8 (iOS showed 31 Dec 1969). The loader reads the
# S2MPS17 RTC and passes s8rtc = UTC seconds at system-counter zero.
edit('iokit/Kernel/IOPlatformExpert.cpp', [
    ('\tif (gIOPlatform) {\n\t\tgIOPlatform->getUTCTimeOfDay(secs, &nsecs);\n\t}\n\n\tassert(nsecs < NSEC_PER_SEC);\n',
     '\tif (gIOPlatform) {\n\t\tgIOPlatform->getUTCTimeOfDay(secs, &nsecs);\n\t}\n'
     '#if defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '\tuint64_t s8rtc = 0;\n'
     '\tif (*secs == 0 && PE_parse_boot_argn("s8rtc", &s8rtc, sizeof(s8rtc)) && s8rtc) {\n'
     '\t\tuint64_t ns;\n'
     '\t\tabsolutetime_to_nanoseconds(mach_absolute_time(), &ns);\n'
     '\t\t*secs = (clock_sec_t)(s8rtc + ns / NSEC_PER_SEC);\n'
     '\t\tnsecs = (clock_nsec_t)(ns % NSEC_PER_SEC);\n'
     '\t}\n'
     '#endif\n\n\tassert(nsecs < NSEC_PER_SEC);\n', 1),
    ('#include <pexpert/pexpert.h>\n#include <uuid/uuid.h>\n',
     '#include <pexpert/pexpert.h>\n#include <uuid/uuid.h>\n#include <kern/clock.h>\n'
     '#include <mach/mach_time.h>\n', 1),
])

# Present at SwapEnd. Boot 29 (sweetfb_hz=30) showed heavy artifacts: the
# vsync timer copied the latest swapped surface whenever it fired, while
# CoreAnimation (SwapWait is a no-op) could already be drawing into it.
# During the SwapEnd call the surface is stable, so copy there, at most once
# per 1/hz; the timer only flushes a swap that was skipped. One lock
# serializes the two copy paths.
edit('iokit/Kernel/arm/SweetFramebuffer.cpp', [
    ('\tuint32_t      fFadeStep;      /* fade-out steps done while off */\n',
     '\tuint32_t      fFadeStep;      /* fade-out steps done while off */\n'
     '\tIOLock       *fPresentLock;   /* one panel copy at a time */\n'
     '\tuint64_t      fLastPresent;   /* mach_absolute_time of the last copy */\n'
     '\tuint64_t      fMinGap;        /* 1/hz in timebase ticks */\n', 1),
    ('\tfFlushTicks = (hz == 0 || hz >= 60) ? 1 : 60 / hz;\n',
     '\tfFlushTicks = (hz == 0 || hz >= 60) ? 1 : 60 / hz;\n'
     '\tfMinGap = 26000000ULL / (hz == 0 ? 60 : hz);     /* 26 MHz timebase */\n'
     '\tfPresentLock = IOLockAlloc();\n'
     '\tif (fPresentLock == NULL) {\n'
     '\t\treturn false;\n'
     '\t}\n', 1),
    ('\tmemcpy(fPending, rec, SWEET_FB_SWAP_REC_SIZE);\n\tfDirty = true;\n\tIOLockUnlock(fLock);\n',
     '\tmemcpy(fPending, rec, SWEET_FB_SWAP_REC_SIZE);\n\tfDirty = true;\n'
     '\tuint64_t now = mach_absolute_time();\n'
     '\tbool copyNow = !fPowerOff && now - fLastPresent >= fMinGap;\n'
     '\tif (copyNow) {\n'
     '\t\tfDirty = false;\n'
     '\t\tfLastPresent = now;\n'
     '\t}\n'
     '\tIOLockUnlock(fLock);\n'
     '\tif (copyNow) {\n'
     '\t\tpresent(rec);\n'
     '\t}\n', 1),
    ('\tuint32_t mask = get32(rec, 0x104);\n',
     '\tuint32_t mask = get32(rec, 0x104);\n\tIOLockLock(fPresentLock);\n'
     '#if defined(DREAMLTE)\n\tuint64_t t0 = mach_absolute_time();\n'
     '\tsweet_fb_wait_frame_done();\n\tsweet_fb_trigger(false);\n'
     '\tuint64_t t1 = mach_absolute_time();\n#endif\n', 1),
    ('static uint32_t sweet_fb_swaps_logged;\n',
     '#if defined(DREAMLTE)\n'
     '/*\n'
     ' * Boot 31 video: torn frames (top half of one screen, bottom of the\n'
     ' * next). DECON_F fetches the panel memory at 60 Hz (boot 30 probe: 30\n'
     ' * frame-done events per 0.5 s) while we copy into it. Start each copy\n'
     ' * right after a frame-done (INTERRUPT_PENDING 0x4c bit 13, source\n'
     ' * enabled in INTERRUPT_ENABLE 0x40; the SPI is not routed), so the copy\n'
     ' * runs ahead of the next fetch.\n'
     ' */\n'
     'extern "C" vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);\n'
     'static vm_offset_t sweet_fb_decon;\n'
     '\n'
     'static void\n'
     'sweet_fb_wait_frame_done(void)\n'
     '{\n'
     '\tif (sweet_fb_decon == 0) {\n'
     '\t\tsweet_fb_decon = ml_io_map(0x12860000, 0x1000);\n'
     '\t\tif (sweet_fb_decon == 0) {\n'
     '\t\t\treturn;\n'
     '\t\t}\n'
     '\t\t*(volatile uint32_t *)(sweet_fb_decon + 0x40) |= (1u << 13) | 1u;\n'
     '\t}\n'
     '\tvolatile uint32_t *pend = (volatile uint32_t *)(sweet_fb_decon + 0x4c);\n'
     '\t*pend = 1u << 13;\n'
     '\tfor (int i = 0; i < 200 && !(*pend & (1u << 13)); i++) {\n'
     '\t\tIODelay(100);\n'
     '\t}\n'
     '}\n'
     '\n'
     '/*\n'
     ' * Boot 32: frames still tore. The S8 panel runs in command mode: on\n'
     ' * every TE the DECON sends the whole buffer to the panel\'s own frame\n'
     ' * memory, also while we copy into it. Samsung\'s DECON driver masks the\n'
     ' * hardware trigger around an update (decon_reg_set_trigger:\n'
     ' * HW_SW_TRIG_CONTROL 0x70, HW_TRIG_EN bit 0, HW_TRIG_MASK_DECON bit 4).\n'
     ' * Mask it right after a frame-done (no transfer in flight), copy, unmask:\n'
     ' * the panel keeps showing the previous frame and then gets a whole one.\n'
     ' */\n'
     'static void\n'
     'sweet_fb_trigger(bool enable)\n'
     '{\n'
     '\tif (sweet_fb_decon == 0) {\n'
     '\t\treturn;\n'
     '\t}\n'
     '\tvolatile uint32_t *trig = (volatile uint32_t *)(sweet_fb_decon + 0x70);\n'
     '\tuint32_t v = *trig & ~((1u << 0) | (1u << 4));\n'
     '\t*trig = v | (enable ? (1u << 0) : (1u << 4));\n'
     '}\n'
     '#endif\n'
     '\n'
     'static uint32_t sweet_fb_swaps_logged;\n', 1),
    ('\t\ts->release();\n\t}\n}\n',
     '\t\ts->release();\n\t}\n'
     '#if defined(DREAMLTE)\n\tsweet_fb_trigger(true);\n'
     '\tsweet_fb_stat(t0, t1, mach_absolute_time());\n#endif\n'
     '\tIOLockUnlock(fPresentLock);\n}\n', 1),
    # Frame statistics: how many frames iOS draws (SwapEnd) and the panel
    # shows (copies) per 10 s, and where a copy's time goes (waiting for
    # the DECON frame-done vs the memcpy), to steer the 30-60 fps work.
    ('static uint32_t sweet_fb_swaps_logged;\n',
     '#if defined(DREAMLTE)\n'
     'static uint64_t sweet_fb_stat_start, sweet_fb_stat_swaps, sweet_fb_stat_copies;\n'
     'static uint64_t sweet_fb_stat_wait, sweet_fb_stat_copy, sweet_fb_stat_copy_max;\n'
     '\n'
     '/* Under fPresentLock; timebase 26 MHz, 26 ticks = 1 us. */\n'
     'static void\n'
     'sweet_fb_stat(uint64_t t0, uint64_t t1, uint64_t t2)\n'
     '{\n'
     '\tsweet_fb_stat_copies++;\n'
     '\tsweet_fb_stat_wait += t1 - t0;\n'
     '\tsweet_fb_stat_copy += t2 - t1;\n'
     '\tif (t2 - t1 > sweet_fb_stat_copy_max) {\n'
     '\t\tsweet_fb_stat_copy_max = t2 - t1;\n'
     '\t}\n'
     '\tif (sweet_fb_stat_start == 0) {\n'
     '\t\tsweet_fb_stat_start = t2;\n'
     '\t} else if (t2 - sweet_fb_stat_start >= 10ULL * 26000000ULL) {\n'
     '\t\tuint64_t n = sweet_fb_stat_copies;\n'
     '\t\tIOLog("SWEET FB: 10s: %llu swaps, %llu shown; per frame wait %llu us, copy %llu us (max %llu)\\n",\n'
     '\t\t    __atomic_load_n(&sweet_fb_stat_swaps, __ATOMIC_RELAXED), n,\n'
     '\t\t    sweet_fb_stat_wait / n / 26, sweet_fb_stat_copy / n / 26, sweet_fb_stat_copy_max / 26);\n'
     '\t\tsweet_fb_stat_start = t2;\n'
     '\t\t__atomic_store_n(&sweet_fb_stat_swaps, 0, __ATOMIC_RELAXED);\n'
     '\t\tsweet_fb_stat_copies = sweet_fb_stat_wait = sweet_fb_stat_copy = sweet_fb_stat_copy_max = 0;\n'
     '\t}\n'
     '}\n'
     '#endif\n'
     '\n'
     'static uint32_t sweet_fb_swaps_logged;\n', 1),
    ('\tif ((++sweet_fb_swap_count % 1000) == 0) {\n',
     '#if defined(DREAMLTE)\n'
     '\t__atomic_add_fetch(&sweet_fb_stat_swaps, 1, __ATOMIC_RELAXED);\n'
     '#endif\n'
     '\tif ((++sweet_fb_swap_count % 1000) == 0) {\n', 1),
])

# Boot 35: TWRP did not keep the pmsg zone, so the loader's probe results
# (display flip CRC, Mongoose rail/cluster, Mali ID) were lost although the
# kernel's own log on USERDATA was fine. Copy the loader's text from its
# DISPLAY probe up to JUMP TO XNU into the kernel log once, at AppleCLCD
# start (IOLog reaches the USERDATA ring by then).
edit('iokit/Kernel/arm/SweetFramebuffer.cpp', [
    ('bool\nAppleCLCD::start(IOService *provider)\n{\n',
     '#if defined(DREAMLTE)\n'
     'extern "C" vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);\n'
     '\n'
     '/* The kernel has no strstr. */\n'
     'static bool\n'
     'sweet_fb_has(const char *s, const char *w)\n'
     '{\n'
     '\tsize_t n = strlen(w);\n'
     '\tfor (; *s; s++) {\n'
     '\t\tif (strncmp(s, w, n) == 0) {\n'
     '\t\t\treturn true;\n'
     '\t\t}\n'
     '\t}\n'
     '\treturn false;\n'
     '}\n'
     '\n'
     'static void\n'
     'sweet_fb_echo_loader(void)\n'
     '{\n'
     '\tconst volatile uint32_t *hdr = (const volatile uint32_t *)ml_io_map(A5_KLOG_PHYS, A5_KLOG_SIZE);\n'
     '\tif (hdr == NULL || hdr[0] != 0x43474244u) {\n'
     '\t\treturn;\n'
     '\t}\n'
     '\tconst volatile char *t = (const volatile char *)(hdr + 3);\n'
     '\tuint32_t n = A5_KLOG_SIZE - 12, i = 0, start = UINT32_MAX;\n'
     '\tchar line[160];\n'
     '\tfor (uint32_t lines = 0; i < n && lines < 120; ) {\n'
     '\t\tuint32_t len = 0;\n'
     '\t\twhile (i < n && t[i] != \'\\n\' && t[i] != 0) {\n'
     '\t\t\tif (len < sizeof(line) - 1) {\n'
     '\t\t\t\tchar c = t[i];\n'
     '\t\t\t\tline[len++] = (c >= 0x20 && c < 0x7f) ? c : \'?\';\n'
     '\t\t\t}\n'
     '\t\t\ti++;\n'
     '\t\t}\n'
     '\t\tline[len] = 0;\n'
     '\t\tif (i < n && t[i] == 0) {\n'
     '\t\t\tbreak;\n'
     '\t\t}\n'
     '\t\ti++;\n'
     '\t\t/* pmsg cells lose bits: accept any of the probes\' first lines. */\n'
     '\t\tif (start == UINT32_MAX && (sweet_fb_has(line, "DISPLAY") || sweet_fb_has(line, "rail") ||\n'
     '\t\t    sweet_fb_has(line, "GPU") || sweet_fb_has(line, "MNGS"))) {\n'
     '\t\t\tstart = i;\n'
     '\t\t}\n'
     '\t\tif (start != UINT32_MAX) {\n'
     '\t\t\tIOLog("S8 LOADER| %s\\n", line);\n'
     '\t\t\tlines++;\n'
     '\t\t\tif (strncmp(line, "JUMP TO XNU", 11) == 0) {\n'
     '\t\t\t\tbreak;\n'
     '\t\t\t}\n'
     '\t\t}\n'
     '\t}\n'
     '}\n'
     '#endif\n'
     '\n'
     'bool\nAppleCLCD::start(IOService *provider)\n{\n'
     '#if defined(DREAMLTE)\n'
     '\tsweet_fb_echo_loader();\n'
     '#endif\n', 1),
])

# Frame rate (boot 34 statistics: iOS draws 1-10 frames/s, CoreAnimation's
# CPU renderer is the limit, the copy takes ~8 ms). sweetfb_scale=1: iOS
# renders the iPhone X canvas (1125x2436, 36 % fewer pixels than the panel,
# a layout iOS has data for) and each copy stretches it over the whole
# 1440x2960 panel (nearest pixel, no borders). A panel row is built once in
# a cached row buffer and copied to every panel row that maps to the same
# canvas row.
edit('iokit/Kernel/arm/SweetFramebuffer.cpp', [
    ('#define SWEET_FB_FORMAT_BGRA            0x42475241\n',
     '#define SWEET_FB_FORMAT_BGRA            0x42475241\n'
     '#define SWEET_FB_ROW_MAX                1440u   /* widest panel row we stretch to */\n', 1),
    ('\tuint64_t      fMinGap;        /* 1/hz in timebase ticks */\n',
     '\tuint64_t      fMinGap;        /* 1/hz in timebase ticks */\n'
     '\tbool          fScale;         /* sweetfb_scale: canvas stretched to the panel */\n', 1),
    ('\tfOffY = (int32_t)(fLogicalH - fHeight) / 2;\n',
     '\tfOffY = (int32_t)(fLogicalH - fHeight) / 2;\n'
     '#if defined(DREAMLTE)\n'
     '\tint scale = 0;\n'
     '\tPE_parse_boot_argn("sweetfb_scale", &scale, sizeof(scale));\n'
     '\tfScale = scale != 0 && !native && fWidth <= SWEET_FB_ROW_MAX;\n'
     '\tif (fScale) {\n'
     '\t\tfOffX = fOffY = 0;\n'
     '\t}\n'
     '#endif\n', 1),
    ('\t\tuint32_t bpe = s->bytesPerElement ? s->bytesPerElement : 4;\n',
     '\t\tuint32_t bpe = s->bytesPerElement ? s->bytesPerElement : 4;\n'
     '#if defined(DREAMLTE)\n'
     '\t\tif (fScale) {\n'
     '\t\t\tif (w <= 0 || h <= 0) {\n'
     '\t\t\t\tw = (int32_t)s->width;\n'
     '\t\t\t\th = (int32_t)s->height;\n'
     '\t\t\t}\n'
     '\t\t\tif (sx < 0 || sy < 0) {\n'
     '\t\t\t\tsx = sy = 0;\n'
     '\t\t\t}\n'
     '\t\t\tif (dx < 0 || dy < 0) {\n'
     '\t\t\t\tdx = dy = 0;\n'
     '\t\t\t}\n'
     '\t\t\tif (w > (int32_t)s->width - sx) {\n'
     '\t\t\t\tw = (int32_t)s->width - sx;\n'
     '\t\t\t}\n'
     '\t\t\tif (h > (int32_t)s->height - sy) {\n'
     '\t\t\t\th = (int32_t)s->height - sy;\n'
     '\t\t\t}\n'
     '\t\t\tif (w > (int32_t)fLogicalW - dx) {\n'
     '\t\t\t\tw = (int32_t)fLogicalW - dx;\n'
     '\t\t\t}\n'
     '\t\t\tif (h > (int32_t)fLogicalH - dy) {\n'
     '\t\t\t\th = (int32_t)fLogicalH - dy;\n'
     '\t\t\t}\n'
     '\t\t\tuint64_t end = (uint64_t)s->offset + (uint64_t)(sy + h - 1) * s->bytesPerRow +\n'
     '\t\t\t    (uint64_t)(sx + w) * 4;\n'
     '\t\t\tif (src != 0 && bpe == 4 && w > 0 && h > 0 && s->elementWidth == 1 &&\n'
     '\t\t\t    s->elementHeight == 1 && s->memory != NULL && end <= s->memory->getLength()) {\n'
     '\t\t\t\tsweet_fb_scaled_copy(fPixels, fRowBytes, fWidth, fHeight, fLogicalW, fLogicalH,\n'
     '\t\t\t\t    src + s->offset + (uint64_t)sy * s->bytesPerRow + (uint64_t)sx * 4,\n'
     '\t\t\t\t    s->bytesPerRow, dx, dy, w, h);\n'
     '\t\t\t}\n'
     '\t\t\ts->release();\n'
     '\t\t\tcontinue;\n'
     '\t\t}\n'
     '#endif\n', 1),
    ('static uint32_t sweet_fb_swaps_logged;\n',
     '#if defined(DREAMLTE)\n'
     'static uint32_t sweet_fb_row[SWEET_FB_ROW_MAX];   /* under fPresentLock */\n'
     'static uint16_t sweet_fb_xmap[SWEET_FB_ROW_MAX];\n'
     '\n'
     '/*\n'
     ' * Stretch the canvas rectangle (dx, dy, w, h) of a layer, whose first\n'
     ' * pixel is at src with stride bpr, over the panel: panel pixel (px, py)\n'
     ' * shows canvas pixel (px * lw / pw, py * lh / ph).\n'
     ' */\n'
     'static void\n'
     'sweet_fb_scaled_copy(uintptr_t fb, uint32_t fbBpr, uint32_t pw, uint32_t ph,\n'
     '    uint32_t lw, uint32_t lh, uintptr_t src, uint32_t bpr,\n'
     '    int32_t dx, int32_t dy, int32_t w, int32_t h)\n'
     '{\n'
     '\tuint32_t px0 = (uint32_t)dx * pw / lw, px1 = (uint32_t)(dx + w) * pw / lw;\n'
     '\tuint32_t py0 = (uint32_t)dy * ph / lh, py1 = (uint32_t)(dy + h) * ph / lh;\n'
     '\tif (px1 > pw) {\n'
     '\t\tpx1 = pw;\n'
     '\t}\n'
     '\tif (py1 > ph) {\n'
     '\t\tpy1 = ph;\n'
     '\t}\n'
     '\tif (px1 <= px0 || py1 <= py0) {\n'
     '\t\treturn;\n'
     '\t}\n'
     '\tuint32_t n = px1 - px0;\n'
     '\tfor (uint32_t i = 0; i < n; i++) {\n'
     '\t\tuint32_t lx = (px0 + i) * lw / pw;\n'
     '\t\tint32_t cx = (int32_t)lx - dx;\n'
     '\t\tsweet_fb_xmap[i] = (uint16_t)(cx < 0 ? 0 : (cx >= w ? w - 1 : cx));\n'
     '\t}\n'
     '\tint32_t built = -1;\n'
     '\tfor (uint32_t py = py0; py < py1; py++) {\n'
     '\t\tint32_t cy = (int32_t)(py * lh / ph) - dy;\n'
     '\t\tcy = cy < 0 ? 0 : (cy >= h ? h - 1 : cy);\n'
     '\t\tif (cy != built) {\n'
     '\t\t\tconst uint32_t *row = (const uint32_t *)(src + (uint64_t)cy * bpr);\n'
     '\t\t\tfor (uint32_t i = 0; i < n; i++) {\n'
     '\t\t\t\tsweet_fb_row[i] = row[sweet_fb_xmap[i]];\n'
     '\t\t\t}\n'
     '\t\t\tbuilt = cy;\n'
     '\t\t}\n'
     '\t\tmemcpy((void *)(fb + (uint64_t)py * fbBpr + (uint64_t)px0 * 4), sweet_fb_row, (size_t)n * 4);\n'
     '\t}\n'
     '}\n'
     '#endif\n'
     '\n'
     'static uint32_t sweet_fb_swaps_logged;\n', 1),
])

# GPU present (sweetfb_gpu=1): the Mali-G71 stretches each full-screen iOS
# frame over the panel (tools/dreamlte/s8_gpu_present.inc, descriptor blob
# from tools/dreamlte/mali-blit-gen.c). Full-screen IOSurfaces are allocated
# physically contiguous then, so the GPU (IDENTITY address space) can read
# them; if that allocation fails, the CPU copy stays.
edit('iokit/Kernel/arm/SweetFramebuffer.cpp', [
    ('static uint32_t sweet_fb_swaps_logged;\n',
     '#if defined(DREAMLTE)\n' +
     (Path(__file__).parent / 's8_mali_blit.h').read_text() + '\n' +
     (Path(__file__).parent / 's8_gpu_present.inc').read_text() +
     '#endif\n\nstatic uint32_t sweet_fb_swaps_logged;\n', 1),
    ('bool\nAppleCLCD::start(IOService *provider)\n{\n#if defined(DREAMLTE)\n\tsweet_fb_echo_loader();\n#endif\n',
     'static void s8gpu_init(void);\n\n'
     'bool\nAppleCLCD::start(IOService *provider)\n{\n#if defined(DREAMLTE)\n\tsweet_fb_echo_loader();\n#endif\n', 1),
    ('\tsetProperty(kIOUserClientClassKey, "IOMobileFramebufferUserClient");\n',
     '#if defined(DREAMLTE)\n\ts8gpu_init();\n#endif\n'
     '\tsetProperty(kIOUserClientClassKey, "IOMobileFramebufferUserClient");\n', 1),
    ('\t\t\t\tsweet_fb_scaled_copy(fPixels, fRowBytes, fWidth, fHeight, fLogicalW, fLogicalH,\n',
     '\t\t\t\tIOByteCount seg = 0;\n'
     '\t\t\t\tuint64_t need = (uint64_t)(h - 1) * s->bytesPerRow + (uint64_t)w * 4;\n'
     '\t\t\t\tif (s8gpu_ready && sx == 0 && sy == 0 && dx == 0 && dy == 0 &&\n'
     '\t\t\t\t    w == (int32_t)fLogicalW && h == (int32_t)fLogicalH &&\n'
     '\t\t\t\t    s->pixelFormat == SWEET_FB_FORMAT_BGRA) {\n'
     '\t\t\t\t\taddr64_t pa = s->memory->getPhysicalSegment(s->offset, &seg, kIOMemoryMapperNone);\n'
     '\t\t\t\t\tif (pa != 0 && seg >= need) {\n'
     '\t\t\t\t\t\t/* CoreAnimation drew through the CPU caches. */\n'
     '\t\t\t\t\t\tclean_dcache((vm_offset_t)(src + s->offset), (unsigned)need, 0);\n'
     '\t\t\t\t\t\tif (s8gpu_present(pa, s->bytesPerRow, (uint32_t)w, (uint32_t)h)) {\n'
     '\t\t\t\t\t\t\ts->release();\n'
     '\t\t\t\t\t\t\tcontinue;\n'
     '\t\t\t\t\t\t}\n'
     '\t\t\t\t\t}\n'
     '\t\t\t\t}\n'
     '\t\t\t\tsweet_fb_scaled_copy(fPixels, fRowBytes, fWidth, fHeight, fLogicalW, fLogicalH,\n', 1),
    # Boot 39: with the 1125x2436 canvas SpringBoard aborted in a loop
    # (BoundingPathBitmap-1125x2436-375x812-3.00x not in the asset manager),
    # so the GPU also presents the native 1440x2960 canvas, 1:1.
    ('\t\tif (src != 0 && bpe == 4) {\n\t\t\tfor (int32_t y = 0; y < h; y++) {\n',
     '#if defined(DREAMLTE)\n'
     '\t\tif (src != 0 && bpe == 4 && s8gpu_ready && sx == 0 && sy == 0 && dx == 0 && dy == 0 &&\n'
     '\t\t    w == (int32_t)fWidth && h == (int32_t)fHeight && s->pixelFormat == SWEET_FB_FORMAT_BGRA) {\n'
     '\t\t\tIOByteCount seg = 0;\n'
     '\t\t\tuint64_t need = (uint64_t)(h - 1) * s->bytesPerRow + (uint64_t)w * 4;\n'
     '\t\t\taddr64_t pa = s->memory->getPhysicalSegment(s->offset, &seg, kIOMemoryMapperNone);\n'
     '\t\t\tif (pa != 0 && seg >= need) {\n'
     '\t\t\t\tclean_dcache((vm_offset_t)(src + s->offset), (unsigned)need, 0);\n'
     '\t\t\t\tif (s8gpu_present(pa, s->bytesPerRow, (uint32_t)w, (uint32_t)h)) {\n'
     '\t\t\t\t\ts->release();\n'
     '\t\t\t\t\tcontinue;\n'
     '\t\t\t\t}\n'
     '\t\t\t}\n'
     '\t\t}\n'
     '#endif\n'
     '\t\tif (src != 0 && bpe == 4) {\n\t\t\tfor (int32_t y = 0; y < h; y++) {\n', 1),
])
edit('iokit/Kernel/arm/SweetSurface.cpp', [
    ('\t\t\tmem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,\n'
     '\t\t\t    kIODirectionInOut | kIOMemoryKernelUserShared, s->allocSize, page_size);\n',
     '#if defined(DREAMLTE)\n'
     '\t\t\t/* sweetfb_gpu: full-screen surfaces physically contiguous, so the\n'
     '\t\t\t * Mali (IDENTITY address space) can read them as a texture. */\n'
     '\t\t\tstatic int gpu = -1;\n'
     '\t\t\tif (gpu < 0) {\n'
     '\t\t\t\tgpu = 0;\n'
     '\t\t\t\tPE_parse_boot_argn("sweetfb_gpu", &gpu, sizeof(gpu));\n'
     '\t\t\t}\n'
     '\t\t\tif (gpu && s->allocSize >= (4u << 20)) {\n'
     '\t\t\t\tmem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,\n'
     '\t\t\t\t    kIODirectionInOut | kIOMemoryKernelUserShared | kIOMemoryPhysicallyContiguous,\n'
     '\t\t\t\t    s->allocSize, 0x0000000ffffff000ULL);\n'
     '\t\t\t}\n'
     '\t\t\tif (mem == NULL)\n'
     '#endif\n'
     '\t\t\tmem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,\n'
     '\t\t\t    kIODirectionInOut | kIOMemoryKernelUserShared, s->allocSize, page_size);\n', 1),
    ('#include "SweetSurface.h"\n',
     '#include "SweetSurface.h"\n#include <pexpert/arm64/board_config.h>\n#include <pexpert/pexpert.h>\n', 1),
])

# Battery: the A5XELTE config had no power source, so iOS showed a wrong
# battery. On the S8, SweetBattery reads the MAX77865 fuel gauge and charger
# through dreamlte_battery_read() (dreamlte_touch.c). S8 battery: 3000 mAh.
edit('iokit/Kernel/arm/SweetBattery.cpp', [
    ('#if defined(ARM64_BOARD_CONFIG_SWEET)\n#include <device/device_types.h>',
     '#if defined(ARM64_BOARD_CONFIG_SWEET) || defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '#include <device/device_types.h>', 1),
    ('/* bsd/dev/sweet_spmi.c */\n'
     'extern "C" int sweet_spmi_init(void);\n'
     'extern "C" int sweet_spmi_read(unsigned sid, unsigned addr, uint8_t *out);\n',
     '#if defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '/* Galaxy S8: MAX77865 fuel gauge (bsd/dev/a5xelte_touch.c). */\n'
     'extern "C" int dreamlte_battery_read(unsigned *percent, unsigned *mv, int *ma,\n'
     '    int *external, int *charging, int *full);\n'
     'static inline int sweet_spmi_init(void) { return -1; }\n'
     'static inline int sweet_spmi_read(unsigned, unsigned, uint8_t *) { return -1; }\n'
     '#define SWEET_BAT_MAH 3000u\n'
     '#else\n'
     '/* bsd/dev/sweet_spmi.c */\n'
     'extern "C" int sweet_spmi_init(void);\n'
     'extern "C" int sweet_spmi_read(unsigned sid, unsigned addr, uint8_t *out);\n'
     '#define SWEET_BAT_MAH 5020u\n'
     '#endif\n', 1),
    ('5020u * percent', 'SWEET_BAT_MAH * percent', 1),
    ('setMaxCapacity(5020)', 'setMaxCapacity(SWEET_BAT_MAH)', 1),
    ('"AppleRawMaxCapacity", 5020)', '"AppleRawMaxCapacity", SWEET_BAT_MAH)', 1),
    ('"DesignCapacity", 5020)', '"DesignCapacity", SWEET_BAT_MAH)', 1),
    ('{capacity, 5020, mv', '{capacity, SWEET_BAT_MAH, mv', 1),
    ('void\nSweetBattery::poll(void)\n{\n',
     'void\nSweetBattery::poll(void)\n{\n'
     '#if defined(ARM64_BOARD_CONFIG_A5XELTE)\n'
     '\tunsigned pct = 0, s8mv = 0;\n'
     '\tint s8ma = 0, ext = 0, chg = 0, full = 0;\n'
     '\tif (fReal && dreamlte_battery_read(&pct, &s8mv, &s8ma, &ext, &chg, &full) == 0) {\n'
     '\t\tpublish(full ? 100 : pct, s8mv, s8ma, ext != 0, ext != 0, chg != 0, full != 0, true, true);\n'
     '\t} else {\n'
     '\t\tpublish(100, 4200, 0, true, true, false, true, false, false);\n'
     '\t}\n'
     '#else\n', 1),
    ('\t    charging, full, true, currentValid);\n}\n',
     '\t    charging, full, true, currentValid);\n#endif\n}\n', 1),
    ('\tint real = 0;\n\t(void)PE_parse_boot_argn("sweetbattery"',
     '#if defined(ARM64_BOARD_CONFIG_A5XELTE)\n\tint real = 1;\n#else\n\tint real = 0;\n#endif\n'
     '\t(void)PE_parse_boot_argn("sweetbattery"', 1),
    ('\tfReal = real == 1 && sweet_spmi_init() == 0;\n',
     '#if defined(ARM64_BOARD_CONFIG_A5XELTE)\n\tfReal = real == 1;\n#else\n'
     '\tfReal = real == 1 && sweet_spmi_init() == 0;\n#endif\n', 1),
    ('measured ? "PM6150 / estimated SOC" : "static fallback"',
     'measured ? "fuel gauge" : "static fallback"', 1),
])
edit('iokit/KernelConfigTables.cpp', [
    ('#if !defined(ARM64_BOARD_CONFIG_A5XELTE)\n    "   {"\n    "     \'IOClass\'         = SweetBattery;"',
     '#if !defined(ARM64_BOARD_CONFIG_A5XELTE) || 1  /* S8: MAX77865 */\n    "   {"\n'
     '    "     \'IOClass\'         = SweetBattery;"', 1),
])

# S8 S6SY761 driver (dreamlte_touch.c next to this script), started from
# the same IOKitBSDInit hook in place of the A5 probe.
edit('bsd/dev/a5xelte_touch.c', [
    ('void\na5xelte_touch_probe(void)\n{\n',
     (Path(__file__).parent / 'dreamlte_touch.c').read_text() +
     'void\na5xelte_touch_probe(void)\n{\n'
     '#if defined(DREAMLTE)\n\tdreamlte_touch_start();   /* the A5 path below stays off */\n#endif\n', 1),
])

# Secondary CPUs. Loader probe (boot 19): PSCI CPU_ON alone never starts an
# S8 core; after the PMU power-up that Samsung's kernel does itself
# (drivers/soc/samsung/exynos-pmu.c: CPUn_CONFIGURATION |= LOCAL_PWR_CFG 0xF)
# the A53 at MPIDR 0x101 started. Mongoose cores (MPIDR 0-3, logical 4-7)
# also need Samsung's cluster0_on and their DDn bit (pmucal_cpu_exynos8895.h),
# as the loader's mngs_probe does; the loader caps cpus= at 4 + the number
# of Mongoose cores its probe started, so cpus=8 does not panic here.
edit('iokit/Kernel/arm/SweetCPU.cpp', [
    ('#include <pexpert/pexpert.h>\n',
     '#include <pexpert/pexpert.h>\n'
     '#if defined(DREAMLTE)\n'
     'extern "C" vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);\n'
     '#endif\n', 1),
    ('\tif (start_paddr != 0 || arg_paddr != 0 || cpu->phys_id == 0 ||\n',
     '\tif (start_paddr != 0 || arg_paddr != 0 || cpu->phys_id == A5_BOOT_MPIDR ||\n', 1),
    ('\t__builtin_arm_dsb(DSB_SY);\n\tint32_t rc = sweet_psci_cpu_on(cpu->phys_id, entry, getCPUNumber());\n',
     '#if defined(DREAMLTE)\n'
     '\t{\n'
     '\t\t/*\n'
     '\t\t * The entry is in the high RAM bank (0x88xxxxxxx); the loader probe\n'
     '\t\t * that started a core used a low entry. Exynos firmware keeps the\n'
     '\t\t * CPU_ON entry in a 32-bit PMU INFORM register, so give it a\n'
     '\t\t * trampoline in the unused low bank: ldr x1, 8; br x1; .quad entry.\n'
     '\t\t */\n'
     '\t\tstatic vm_offset_t tramp;\n'
     '\t\tconst uint64_t tramp_pa = 0x8f000000ULL;\n'
     '\t\tif (tramp == 0) {\n'
     '\t\t\ttramp = ml_io_map(tramp_pa, 0x1000);\n'
     '\t\t}\n'
     '\t\tvolatile uint32_t *t = (volatile uint32_t *)tramp;\n'
     '\t\tt[0] = 0x58000041;              /* ldr x1, #8 */\n'
     '\t\tt[1] = 0xd61f0020;              /* br x1 */\n'
     '\t\tt[2] = (uint32_t)entry;\n'
     '\t\tt[3] = (uint32_t)((uint64_t)entry >> 32);\n'
     '\t\t__builtin_arm_dsb(DSB_SY);\n'
     '\t\tIOLog(CPU_PORT_NAME " CPU: cpu=%u entry 0x%llx via low trampoline 0x%llx\\n",\n'
     '\t\t    getCPUNumber(), (uint64_t)entry, tramp_pa);\n'
     '\t\tentry = (vm_offset_t)tramp_pa;\n'
     '\t\tstatic vm_offset_t pmu;\n'
     '\t\tif (pmu == 0) {\n'
     '\t\t\tpmu = ml_io_map(0x16480000, 0x10000);\n'
     '\t\t}\n'
     '\t\tuint32_t off = 0x2000 + (uint32_t)(((((cpu->phys_id >> 8) & 0xf) << 2) |\n'
     '\t\t    (cpu->phys_id & 0xf)) * 0x80);\n'
     '\t\tvolatile uint32_t *cfg = (volatile uint32_t *)(pmu + off);\n'
     '\t\t/*\n'
     '\t\t * Mongoose M2 (MPIDR Aff1 0): Samsung\'s cluster0_on once, and\n'
     '\t\t * core0n_off/core0n_on clear/set DDn_DD_DA_INPUT_0 (0x16840000 +\n'
     '\t\t * n * 0x400) around the power edge. The loader probe turned its\n'
     '\t\t * cores off again (CPU_OFF), so the cluster steps are redone here.\n'
     '\t\t */\n'
     '\t\tconst bool mngs = ((cpu->phys_id >> 8) & 0xf) == 0;\n'
     '\t\tstatic vm_offset_t dd;\n'
     '\t\tstatic bool mngs_cluster_on;\n'
     '\t\tvolatile uint32_t *ddn = NULL;\n'
     '\t\tif (mngs) {\n'
     '\t\t\tvolatile uint32_t *p = (volatile uint32_t *)pmu;\n'
     '\t\t\tif (dd == 0) {\n'
     '\t\t\t\tdd = ml_io_map(0x16840000, 0x2000);\n'
     '\t\t\t}\n'
     '\t\t\tddn = (volatile uint32_t *)(dd + (cpu->phys_id & 3) * 0x400);\n'
     '\t\t\tif (!mngs_cluster_on) {\n'
     '\t\t\t\tp[0x3c20 / 4] |= 1;            /* CPU_QCH_EMBEDDED_CMU_CPUCL0 */\n'
     '\t\t\t\tfor (int i = 0; i < 50000 && !(p[0x3c24 / 4] & 1); i++) {\n'
     '\t\t\t\t\tIODelay(1);\n'
     '\t\t\t\t}\n'
     '\t\t\t\tp[0x45c0 / 4] |= 1;            /* CLKSTOP_CMU_EMBEDDED_CMU_CPUCL0 */\n'
     '\t\t\t\tfor (int i = 0; i < 50000 && !(p[0x45c4 / 4] & 1); i++) {\n'
     '\t\t\t\t\tIODelay(1);\n'
     '\t\t\t\t}\n'
     '\t\t\t\t*(volatile uint32_t *)(dd + 0x1000) |= 1;     /* DD4 */\n'
     '\t\t\t\tp[0x2488 / 4] &= ~1u;          /* CLUSTER0_CPUSEQUENCER_OPTION */\n'
     '\t\t\t\tmngs_cluster_on = true;\n'
     '\t\t\t\tIOLog(CPU_PORT_NAME " CPU: Mongoose cluster on: qch=0x%x clkstop=0x%x seq=0x%x\\n",\n'
     '\t\t\t\t    p[0x3c24 / 4], p[0x45c4 / 4], p[0x2484 / 4]);\n'
     '\t\t\t}\n'
     '\t\t\t*ddn &= ~1u;\n'
     '\t\t}\n'
     '\t\t/*\n'
     '\t\t * The sequence that started a core in the loader probe: PSCI\n'
     '\t\t * CPU_ON, then a PMU power-up (LOCAL_PWR_CFG 0 -> 0xF), then CPU_ON\n'
     '\t\t * again (below). Boot 20 skipped the 0 -> 0xF edge (cfg was left\n'
     '\t\t * at 0xF) and the core never came alive, so power it down first.\n'
     '\t\t */\n'
     '\t\tcfg[0] &= ~0xfu;\n'
     '\t\tfor (int i = 0; i < 10000 && (cfg[1] & 0xf) != 0; i++) {\n'
     '\t\t\tIODelay(1);\n'
     '\t\t}\n'
     '\t\t__builtin_arm_dsb(DSB_SY);\n'
     '\t\tint32_t rc0 = sweet_psci_cpu_on(cpu->phys_id, entry, getCPUNumber());\n'
     '\t\tcfg[0] |= 0xf;                  /* CPUn_CONFIGURATION: LOCAL_PWR_CFG */\n'
     '\t\tfor (int i = 0; i < 10000 && (cfg[1] & 0xf) != 0xf; i++) {\n'
     '\t\t\tIODelay(1);\n'
     '\t\t}\n'
     '\t\tif (ddn != NULL) {\n'
     '\t\t\t*ddn |= 1;\n'
     '\t\t\t/*\n'
     '\t\t\t * Boot 33: the cluster\'s NONCPU (0x2404) and L2 (0x2604) status\n'
     '\t\t\t * stayed off after the core power-up. Power them through their\n'
     '\t\t\t * CONFIGURATION registers (0x2400 = 0xF, 0x2600 = 0x7), as the\n'
     '\t\t\t * loader probe does. The loader also enables the BUCK2 rail.\n'
     '\t\t\t */\n'
     '\t\t\tvolatile uint32_t *pm = (volatile uint32_t *)pmu;\n'
     '\t\t\tif ((pm[0x2404 / 4] & 0xf) != 0xf || (pm[0x2604 / 4] & 7) != 7) {\n'
     '\t\t\t\tpm[0x2400 / 4] |= 0xf;\n'
     '\t\t\t\tfor (int i = 0; i < 10000 && (pm[0x2404 / 4] & 0xf) != 0xf; i++) {\n'
     '\t\t\t\t\tIODelay(1);\n'
     '\t\t\t\t}\n'
     '\t\t\t\tpm[0x2600 / 4] |= 7;\n'
     '\t\t\t\tfor (int i = 0; i < 10000 && (pm[0x2604 / 4] & 7) != 7; i++) {\n'
     '\t\t\t\t\tIODelay(1);\n'
     '\t\t\t\t}\n'
     '\t\t\t}\n'
     '\t\t\tIOLog(CPU_PORT_NAME " CPU: cpu=%u Mongoose cluster noncpu=0x%x l2=0x%x\\n",\n'
     '\t\t\t    getCPUNumber(), pm[0x2404 / 4], pm[0x2604 / 4]);\n'
     '\t\t\t/*\n'
     '\t\t\t * Boot 32: the Mongoose PLL (CMU_CPUCL0 PLL_CON0) was enabled\n'
     '\t\t\t * at 1066 MHz but not STABLE (bit 29). Re-lock it as Samsung\'s\n'
     '\t\t\t * cal-if ra.c does: mux (bit 4) to FIN, wait busy (bit 7),\n'
     '\t\t\t * disable, LOCKTIME = P * 150, enable, wait STABLE, mux back.\n'
     '\t\t\t */\n'
     '\t\t\tstatic vm_offset_t cmu;\n'
     '\t\t\tif (cmu == 0) {\n'
     '\t\t\t\tcmu = ml_io_map(0x16800000, 0x1000);\n'
     '\t\t\t}\n'
     '\t\t\tvolatile uint32_t *con = (volatile uint32_t *)(cmu + 0x120);\n'
     '\t\t\tfor (int i = 0; i < 10000 && !(*con & (1u << 29)); i++) {\n'
     '\t\t\t\tIODelay(1);\n'
     '\t\t\t}\n'
     '\t\t\tif (!(*con & (1u << 29))) {\n'
     '\t\t\t\tuint32_t v = *con;\n'
     '\t\t\t\t*con = v & ~(1u << 4);\n'
     '\t\t\t\tfor (int i = 0; i < 1000 && (*con & (1u << 7)); i++) {\n'
     '\t\t\t\t\tIODelay(1);\n'
     '\t\t\t\t}\n'
     '\t\t\t\t*con &= ~(1u << 31);\n'
     '\t\t\t\t*(volatile uint32_t *)cmu = ((v >> 8) & 0x3f) * 150;\n'
     '\t\t\t\t*con |= 1u << 31;\n'
     '\t\t\t\tfor (int i = 0; i < 10000 && !(*con & (1u << 29)); i++) {\n'
     '\t\t\t\t\tIODelay(1);\n'
     '\t\t\t\t}\n'
     '\t\t\t\tif (*con & (1u << 29)) {\n'
     '\t\t\t\t\t*con |= 1u << 4;\n'
     '\t\t\t\t}\n'
     '\t\t\t}\n'
     '\t\t\tIOLog(CPU_PORT_NAME " CPU: cpu=%u Mongoose PLL con0=0x%x\\n", getCPUNumber(), *con);\n'
     '\t\t}\n'
     '\t\tIOLog(CPU_PORT_NAME " CPU: cpu=%u first CPU_ON rc=%d, PMU power-up cfg=0x%x status=0x%x\\n",\n'
     '\t\t    getCPUNumber(), rc0, cfg[0], cfg[1]);\n'
     '\t}\n'
     '#endif\n'
     '\t__builtin_arm_dsb(DSB_SY);\n\tint32_t rc = sweet_psci_cpu_on(cpu->phys_id, entry, getCPUNumber());\n', 1),
])

# One scheduler cluster for all eight cores. A5XELTE derives the logical
# cluster from MPIDR Aff1, so cpus>4 would create a second processor set;
# the A5's eight-core trial panicked on CPU0 ("Ticket lock ... unexpectedly
# owned") right after that, while cpus=4 (one set) runs. XNU schedules the
# clusters symmetrically anyway (cluster-type SMP); the ADT keeps the real
# die-cluster-id / l2-cache-id. Only MPIDR-indexed PPL data would need the
# physical split, and this build has no XNU_MONITOR.
edit('osfmk/arm64/machine_routines.c', [
    ('#if HAS_CLUSTER || defined(A5XELTE)\n\t\tuint32_t phys_cluster_id = MPIDR_CLUSTER_ID(cpu->phys_id);\n',
     '#if defined(DREAMLTE)\n'
     '\t\tuint32_t phys_cluster_id = 0;\n'
     '#elif HAS_CLUSTER || defined(A5XELTE)\n'
     '\t\tuint32_t phys_cluster_id = MPIDR_CLUSTER_ID(cpu->phys_id);\n', 1),
])

# IOKit CPU names: the S8 has two core types.
edit('iokit/Kernel/arm/SweetCPU.cpp', [
    ('#if defined(A5XELTE)\n\tcpuName = OSSymbol::withCStringNoCopy("Cortex-A53");\n',
     '#if defined(DREAMLTE)\n'
     '\tcpuName = OSSymbol::withCStringNoCopy(((cpu->phys_id >> 8) & 0xf) == 0 ?\n'
     '\t    "Mongoose M2" : "Cortex-A53");\n'
     '#elif defined(A5XELTE)\n\tcpuName = OSSymbol::withCStringNoCopy("Cortex-A53");\n', 1),
])

edit('bsd/dev/a5xelte_keys.c', [
    ('#include <pexpert/pexpert.h>\n',
     '#include <pexpert/pexpert.h>\n#include <pexpert/arm64/board_config.h>\n', 1),
    ('#define A5_ALIVE_PA 0x139b0000u\n',
     '#if defined(DREAMLTE)\n'
     '#define A5_ALIVE_PA 0x164b0000u            /* Exynos 8895 pinctrl ALIVE */\n'
     '#else\n'
     '#define A5_ALIVE_PA 0x139b0000u\n'
     '#endif\n', 1),
    ('static void\na5_keys_wait(void)\n',
     '#if defined(DREAMLTE)\n'
     '/*\n'
     ' * Galaxy S8 (mainline exynos8895-dreamlte.dts gpio-keys, all active low):\n'
     ' * Power GPA2-4, Volume Up GPA0-3, Volume Down GPA0-4, Bixby GPA0-6 (used\n'
     ' * as Home). Bank offsets per mainline pinctrl-exynos-arm64.c\n'
     ' * (exynos8895_pin_banks0): GPA0 0x020, GPA2 0x060; DAT at +4. Boot 22/23\n'
     ' * read 0x044 (GPA1 DAT) and saw nothing. Sample = GPA0 | GPA2 << 8.\n'
     ' */\n'
     'typedef uint16_t keys_t;\n'
     '#define KEYS_SAMPLE() ((keys_t)((a5_keys_read(0x24) & 0xff) | (a5_keys_read(0x64) & 0xff) << 8))\n'
     '#define KEYS_PINS { 12, 3, 4, 6 }\n'
     '#else\n'
     'typedef uint8_t keys_t;\n'
     '#define KEYS_SAMPLE() ((keys_t)a5_keys_read(A5_GPA2_DAT))\n'
     '#define KEYS_PINS { 7, 0, 1, 5 }\n'
     '#endif\n'
     '\n'
     'static void\na5_keys_wait(void)\n', 1),
    ('\tstatic const uint8_t pins[4] = {7, 0, 1, 5};\n'
     '\tuint8_t stable = (uint8_t)a5_keys_read(A5_GPA2_DAT);\n'
     '\tuint8_t previous = stable;\n',
     '\tstatic const uint8_t pins[4] = KEYS_PINS;\n'
     '\tkeys_t stable = KEYS_SAMPLE();\n'
     '\tkeys_t previous = stable;\n', 1),
    ('\t\tuint8_t sample = (uint8_t)a5_keys_read(A5_GPA2_DAT);\n',
     '\t\tkeys_t sample = KEYS_SAMPLE();\n', 1),
    ('\t\t\t\tuint8_t bit = 1u << pins[key];\n',
     '\t\t\t\tkeys_t bit = (keys_t)(1u << pins[key]);\n', 1),
    ('\tint enabled = 1;\n\tif (a5_keys_started) {',
     '\tint enabled = A5_KEYS_DEFAULT;\n\tif (a5_keys_started) {', 1),
])
