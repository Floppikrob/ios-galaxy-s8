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
 *
 * Boot 36: GPU_ID 0x60a00001 (product 0x60a0 = TMIx, Mali-G71 r0p0),
 * SHADER_PRESENT 0x17771777 (20 cores), L2_PRESENT 1, pd-g3d already on.
 * Step 2, the first GPU jobs: soft reset, power L2/tiler/shaders, address
 * space 0 in IDENTITY mode (GPU VA = PA, no page tables; kbase
 * AS_TRANSCFG_ADRMODE_IDENTITY 2), then on job slot 0 a NULL job (type 1)
 * and a WRITE_VALUE job (type 2, immediate 32-bit) that must store a magic
 * word in RAM. Registers and the 32-byte job header follow Samsung's kbase
 * r0p0 (mali_midg_regmap.h, struct job_descriptor_header); the write-value
 * payload (address, value type 6 = immediate 32, immediate) follows Mesa's
 * Bifrost job description.
 */
#include <stdint.h>
#include "gpu_probe.h"

#define PMU		0x16480000UL
#define G3D_CFG		(PMU + 0x40e0)
#define G3D_STAT	(PMU + 0x40e4)
#define CMU_G3D		0x13800000UL
#define MALI		0x13900000UL
#define TIMER_HZ	26000000UL

#define GPU_IRQ_RAWSTAT	0x020
#define GPU_IRQ_CLEAR	0x024
#define GPU_COMMAND	0x030
#define RESET_DONE	(1u << 8)
#define JOB_IRQ_RAWSTAT	0x1000
#define JOB_IRQ_CLEAR	0x1004
#define JS0		0x1800
#define AS0		0x2400

/* Job descriptors and the write target: MMU off, so CPU and GPU see RAM
 * uncached; GPU addresses are physical (IDENTITY). */
static volatile uint8_t gpu_mem[1024] __attribute__((aligned(256)));

static inline uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static uint64_t ticks(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static int wait_mask(uintptr_t a, uint32_t mask, uint32_t want, unsigned ms)
{
	uint64_t end = ticks() + TIMER_HZ / 1000 * ms;
	while (ticks() < end)
		if ((rd(a) & mask) == want)
			return 1;
	return 0;
}

static void put32(unsigned off, uint32_t v)
{
	*(volatile uint32_t *)(gpu_mem + off) = v;
}

static uint32_t get32(unsigned off)
{
	return *(volatile uint32_t *)(gpu_mem + off);
}

static void put64(unsigned off, uint64_t v)
{
	put32(off, (uint32_t)v);
	put32(off + 4, (uint32_t)(v >> 32));
}

/* Runs the one-job chain at gpu_mem + 0 on slot 0; returns JOB_IRQ_RAWSTAT. */
static uint32_t run_job(uint32_t shaders)
{
	uint64_t job = (uintptr_t)gpu_mem;
	wr(MALI + JOB_IRQ_CLEAR, 0xffffffffu);
	wr(MALI + JS0 + 0x40, (uint32_t)job);		/* JS_HEAD_NEXT */
	wr(MALI + JS0 + 0x44, (uint32_t)(job >> 32));
	wr(MALI + JS0 + 0x50, shaders);			/* JS_AFFINITY_NEXT */
	wr(MALI + JS0 + 0x54, 0);
	/* JS_CONFIG: AS 0, flush clean+invalidate at start and end, prio 8. */
	wr(MALI + JS0 + 0x58, (8u << 16) | (3u << 12) | (3u << 8));
	wr(MALI + JS0 + 0x60, 1);			/* JS_COMMAND_START */
	uint64_t end = ticks() + TIMER_HZ / 10;
	while (ticks() < end && !(rd(MALI + JOB_IRQ_RAWSTAT) & ((1u << 16) | 1u)))
		;
	return rd(MALI + JOB_IRQ_RAWSTAT);
}

/* Job header (kbase struct job_descriptor_header), 64-bit descriptors. */
static void job_header(unsigned type)
{
	for (unsigned i = 0; i < 0x40; i += 4)
		put32(i, 0);
	put32(0x10, 1u | (type << 1) | (1u << 16));	/* size 64, type, job_index 1 */
}

static void gpu_jobs(void (*kv)(const char *, uint64_t), void (*out)(const char *), uint32_t shaders)
{
	out("GPU jobs\n");
	wr(MALI + GPU_IRQ_CLEAR, 0xffffffffu);
	wr(MALI + GPU_COMMAND, 1);			/* SOFT_RESET */
	kv("reset", wait_mask(MALI + GPU_IRQ_RAWSTAT, RESET_DONE, RESET_DONE, 100));
	wr(MALI + GPU_IRQ_CLEAR, 0xffffffffu);
	uint32_t l2 = rd(MALI + 0x120), tiler = rd(MALI + 0x110);
	wr(MALI + 0x1a0, l2);				/* L2_PWRON */
	kv("l2 ready", wait_mask(MALI + 0x160, l2, l2, 100));
	wr(MALI + 0x190, tiler);			/* TILER_PWRON */
	kv("tiler ready", wait_mask(MALI + 0x150, tiler, tiler, 100));
	wr(MALI + 0x180, shaders);			/* SHADER_PWRON */
	kv("shaders ready", wait_mask(MALI + 0x140, shaders, shaders, 100));
	out("\n");
	kv("shader ready", rd(MALI + 0x140));
	kv("GPU_STATUS", rd(MALI + 0x034));
	out("\n");

	wr(MALI + AS0 + 0x30, 2);			/* AS_TRANSCFG: IDENTITY */
	wr(MALI + AS0 + 0x34, 0);
	wr(MALI + AS0 + 0x08, 0x88888888u);		/* AS_MEMATTR: impl. defined */
	wr(MALI + AS0 + 0x0c, 0x88888888u);
	wr(MALI + AS0 + 0x00, 0);
	wr(MALI + AS0 + 0x04, 0);
	wr(MALI + AS0 + 0x18, 1);			/* AS_COMMAND_UPDATE */
	kv("as0 update", wait_mask(MALI + AS0 + 0x28, 1, 0, 100));
	out("\n");

	job_header(1);					/* NULL job */
	uint32_t irq = run_job(shaders);
	kv("NULL job irq", irq);
	kv("js0 status", rd(MALI + JS0 + 0x24));
	kv("exc", get32(0x00));
	out("\n");

	const uint32_t magic = 0x5a5a1234u;
	put32(0x100, 0xdeadbeefu);
	job_header(2);					/* WRITE_VALUE */
	put64(0x20, (uintptr_t)gpu_mem + 0x100);	/* address */
	put32(0x28, 6);					/* immediate 32 */
	put64(0x30, magic);
	irq = run_job(shaders);
	kv("WRITE job irq", irq);
	kv("js0 status", rd(MALI + JS0 + 0x24));
	kv("exc", get32(0x00));
	kv("value", get32(0x100));
	out("\n");
	kv("AS0 fault", rd(MALI + AS0 + 0x1c));
	kv("GPU_IRQ", rd(MALI + GPU_IRQ_RAWSTAT));
	out("\n");
	kv("GPU JOB WORKS", get32(0x100) == magic);
	out("\n");
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
	/* kbase GPU_ID2_PRODUCT_TMIX = MAKE(6, 0, 10, 0) = 0x60a0 (Mali-G71). */
	int g71 = (id >> 16) == 0x60a0;
	kv("MALI G71 FOUND", g71);
	out("\n");
	if (g71)
		gpu_jobs(kv, out, rd(MALI + 0x100));
	out("GPU probe done\n");
	return g71;
}
