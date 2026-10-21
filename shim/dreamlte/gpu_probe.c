/* SPDX-License-Identifier: GPL-2.0-only
 * Galaxy S8 Mali-G71 bring-up probe, run by the loader (MMU off), boot-arg
 * s8gpu=1. Step 1 of a GPU for iOS: power the GPU and read its ID.
 *
 * Sources (Samsung universal8895 kernel): exynos8895.dtsi mali@13900000
 * (pd_g3d), exynos8895-pm-domains.dtsi pd-g3d@164840e0, cal-if/exynos8895
 * pmucal_local_exynos8895.h g3d_on: CMU_G3D 0x13800000 + 0x148 :=
 * 0x30000003, + 0x140 bit 4, + 0x100 bit 4 (MUX_CLKCMU_EMBEDDED_G3D_USER),
 * PMU 0x40E0 := 0xF, wait PMU 0x40E4 & 0xF == 0xF. The GPU registers are
 * read only once the domain reports on, so a gated block is never touched.
 * Mali registers (mali_kbase_gpu_regmap.h): GPU_ID 0x000, L2_FEATURES
 * 0x004, GPU_STATUS 0x034, SHADER_PRESENT_LO 0x100, L2_PRESENT_LO 0x120.
 */
#include <stdint.h>
#include "gpu_probe.h"

#define PMU		0x16480000UL
#define G3D_CFG		(PMU + 0x40e0)
#define G3D_STAT	(PMU + 0x40e4)
#define CMU_G3D		0x13800000UL
#define MALI		0x13900000UL
#define TIMER_HZ	26000000UL

static inline uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static uint64_t ticks(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

int gpu_probe(void (*kv)(const char *, uint64_t), void (*out)(const char *))
{
	out("GPU probe (Mali-G71, pd-g3d)\n");
	kv("g3d cfg", rd(G3D_CFG));
	kv("stat", rd(G3D_STAT));
	out("\n");
	if ((rd(G3D_STAT) & 0xf) != 0xf) {
		wr(CMU_G3D + 0x148, 0x30000003);
		wr(CMU_G3D + 0x140, rd(CMU_G3D + 0x140) | (1u << 4));
		wr(CMU_G3D + 0x100, rd(CMU_G3D + 0x100) | (1u << 4));
		wr(G3D_CFG, rd(G3D_CFG) | 0xf);
		uint64_t end = ticks() + TIMER_HZ / 20;
		while (ticks() < end && (rd(G3D_STAT) & 0xf) != 0xf)
			;
		kv("g3d on: stat", rd(G3D_STAT));
		out("\n");
	}
	if ((rd(G3D_STAT) & 0xf) != 0xf) {
		out("GPU: power domain stays off\n");
		return 0;
	}
	kv("cmu g3d pll", rd(CMU_G3D + 0x120));
	kv("0x140", rd(CMU_G3D + 0x140));
	kv("mux", rd(CMU_G3D + 0x100));
	out("\n");
	uint32_t id = rd(MALI + 0x000);
	kv("GPU_ID", id);
	kv("L2_FEATURES", rd(MALI + 0x004));
	kv("GPU_STATUS", rd(MALI + 0x034));
	out("\n");
	kv("SHADER_PRESENT", rd(MALI + 0x100));
	kv("L2_PRESENT", rd(MALI + 0x120));
	out("\n");
	/* Mali-G71 is product 0x6000 in GPU_ID[31:16]. */
	kv("MALI G71 FOUND", (id >> 16) == 0x6000);
	out("\nGPU probe done\n");
	return (id >> 16) == 0x6000;
}
