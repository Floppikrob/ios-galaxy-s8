/* SPDX-License-Identifier: GPL-2.0-only
 * Galaxy S8 secondary-CPU probe, run by the loader before XNU (MMU off).
 *
 * s8-probe v3-v6: PSCI CPU_ON returns 0 (twice, never ALREADY_ON) yet no
 * secondary ever ran our entry. Samsung's 4.4 kernel powers cores through
 * the PMU itself (drivers/soc/samsung/exynos-pmu.c): CPUn_CONFIGURATION at
 * PMU + 0x2000 + ((Aff1 << 2 | Aff0) * 0x80), LOCAL_PWR_CFG = 0xF, status
 * at +4; per-cluster CPUSEQ_OPTION / NONCPU / L2 status at 0x2488 / 0x2404 /
 * 0x2604 + cluster * 0x20. This probe tries CPU_ON alone, then PMU power-up
 * followed by CPU_ON, for one A53 (0x101) and one Mongoose (0x000) core.
 * A started core marks its slot and turns itself off again (CPU_OFF).
 */
#include <stdint.h>
#include "smp_probe.h"

#define PMU		0x16480000UL
#define CPU_CFG(m)	(PMU + 0x2000 + ((((m) >> 8) & 0xf) << 2 | ((m) & 0xf)) * 0x80)
#define CPU_STAT(m)	(CPU_CFG(m) + 4)
#define CL_CPUSEQ(c)	(PMU + 0x2488 + (c) * 0x20)
#define CL_NONCPU(c)	(PMU + 0x2404 + (c) * 0x20)
#define CL_L2(c)	(PMU + 0x2604 + (c) * 0x20)

#define PSCI_CPU_ON	0xc4000003UL
#define TIMER_HZ	26000000UL

extern char smp_entry[];
extern volatile uint64_t smp_flags[8];	/* start.S: set by a started core */

static inline uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static uint64_t ticks(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static int64_t smc3(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : :
			 "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13",
			 "x14", "x15", "x16", "x17", "memory");
	return (int64_t)x0;
}

static int wait_flag(unsigned slot, unsigned ms)
{
	uint64_t end = ticks() + TIMER_HZ / 1000 * ms;
	while (ticks() < end)
		if (smp_flags[slot])
			return 1;
	return 0;
}

static void regs(void (*kv)(const char *, uint64_t), uint64_t m)
{
	unsigned c = (m >> 8) & 0xf;
	kv(" cfg", rd(CPU_CFG(m)));
	kv("stat", rd(CPU_STAT(m)));
	kv("cpuseq", rd(CL_CPUSEQ(c)));
	kv("noncpu", rd(CL_NONCPU(c)));
	kv("l2", rd(CL_L2(c)));
}

/* Poll a PMU bit with a timeout; 1 = reached. */
static int wait_bit(uintptr_t a, uint32_t bit, unsigned ms)
{
	uint64_t end = ticks() + TIMER_HZ / 1000 * ms;
	while (ticks() < end)
		if (rd(a) & bit)
			return 1;
	return 0;
}

#define DD(n)		(0x16840000UL + (n) * 0x400)	/* DDn_DD_DA_INPUT_0 */
#define CMU_CPUCL0	0x16800000UL	/* Mongoose cluster CMU (inside CL0) */
#define CMU_CPUCL1	0x16900000UL	/* A53 cluster CMU */
#define PLL_CON0	0x0120		/* bit 31 enable, bit 29 stable */
#define MUX_CLK_PLL	0x1000		/* bit 0 select, bit 16 busy */

/*
 * Mongoose (CLUSTER0, MPIDR 0x0-0x3) cluster power-up, in the order of
 * Samsung's cal-if/exynos8895/pmucal_cpu_exynos8895.h cluster0_on:
 * CPU_QCH 0x3C20 := 1, wait 0x3C24; CLKSTOP 0x45C0 := 1, wait 0x45C4;
 * DD4 := 1; CPUSEQUENCER_OPTION 0x2488 bit 0 := 0. Every wait has a
 * timeout, so a stuck step is logged instead of hanging the loader.
 */
static int mngs_cluster_on(void (*kv)(const char *, uint64_t), void (*out)(const char *))
{
	wr(PMU + 0x3c20, rd(PMU + 0x3c20) | 1);
	if (!wait_bit(PMU + 0x3c24, 1, 50)) {
		out("MNGS: QCH status stuck\n");
		return 0;
	}
	wr(PMU + 0x45c0, rd(PMU + 0x45c0) | 1);
	if (!wait_bit(PMU + 0x45c4, 1, 50)) {
		out("MNGS: CLKSTOP status stuck\n");
		return 0;
	}
	wr(DD(4), rd(DD(4)) | 1);
	wr(PMU + 0x2488, rd(PMU + 0x2488) & ~1u);
	kv("cluster on: seq st", rd(PMU + 0x2484));
	out("\n");
	return 1;
}

/*
 * One Mongoose core, MPIDR 0x00n. Boot 31 powered the core first, then
 * CPU_ON: status 0xf, rc 0, the core never ran our entry. This is the order
 * that starts the A53s (SweetCPU, patch 0124): power off (core0n_off: DDn,
 * then CONFIGURATION), CPU_ON (firmware records the entry), power 0 -> 0xF
 * edge plus DDn (core0n_on), CPU_ON again. The core marks smp_flags[n] in
 * smp_entry and turns itself off (CPU_OFF); XNU repeats the sequence.
 */
static int mngs_core_start(unsigned n, void (*kv)(const char *, uint64_t), void (*out)(const char *))
{
	uint64_t m = n;
	smp_flags[n] = 0;
	wr(DD(n), rd(DD(n)) & ~1u);
	wr(CPU_CFG(m), rd(CPU_CFG(m)) & ~0xfu);
	uint64_t end = ticks() + TIMER_HZ / 10;
	while (ticks() < end && (rd(CPU_STAT(m)) & 0xf))
		;
	int64_t rc0 = smc3(PSCI_CPU_ON, m, (uintptr_t)smp_entry, 0);
	wr(CPU_CFG(m), rd(CPU_CFG(m)) | 0xf);
	end = ticks() + TIMER_HZ / 10;
	while (ticks() < end && (rd(CPU_STAT(m)) & 0xf) != 0xf)
		;
	wr(DD(n), rd(DD(n)) | 1);
	int up = wait_flag(n, 100);
	int64_t rc = 0;
	if (!up) {
		rc = smc3(PSCI_CPU_ON, m, (uintptr_t)smp_entry, 0);
		up = wait_flag(n, 300);
	}
	kv("core", m);
	kv("rc0", (uint64_t)rc0);
	kv("rc", (uint64_t)rc);
	kv("st", rd(CPU_STAT(m)));
	kv("up", up);
	out("\n");
	return up;
}

/*
 * Mongoose cluster bring-up test, run before XNU. Returns how many of the
 * four Mongoose cores ran smp_entry (they stop at the first that does not).
 * The loader limits XNU's cpus= to 4 + that count, so a cpus=8 image still
 * boots on the A53s when the big cores do not start.
 * With "s8mngsdbg=1" it also dumps the cluster PLLs; the CL0 CMU sits in
 * the Mongoose power domain, so it is read only after the cluster is on.
 */
int mngs_probe(void (*kv)(const char *, uint64_t), void (*out)(const char *), int dbg)
{
	out("MNGS probe (Mongoose cluster 0)\n");
	kv("seq st", rd(PMU + 0x2484));
	kv("seq opt", rd(PMU + 0x2488));
	kv("qch", rd(PMU + 0x3c24));
	kv("clkstop", rd(PMU + 0x45c4));
	kv("dd4", rd(DD(4)));
	kv("cpu0 st", rd(CPU_STAT(0)));
	out("\n");
	if (dbg) {
		kv("cl1 pll", rd(CMU_CPUCL1 + PLL_CON0));
		kv("cl1 mux", rd(CMU_CPUCL1 + MUX_CLK_PLL));
		out("\n");
	}
	if (!mngs_cluster_on(kv, out))
		return 0;
	if (dbg) {
		kv("cl0 pll", rd(CMU_CPUCL0 + PLL_CON0));
		kv("cl0 mux", rd(CMU_CPUCL0 + MUX_CLK_PLL));
		out("\n");
	}
	int started = 0;
	while (started < 4 && mngs_core_start((unsigned)started, kv, out))
		started++;
	kv("MONGOOSE STARTED", (uint64_t)started);
	out("\nMNGS probe done\n");
	return started;
}

void smp_probe(void (*kv)(const char *, uint64_t), void (*out)(const char *))
{
	static const uint64_t targets[] = { 0x101, 0x000 };
	uint64_t self = 0;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(self));
	out("SMP probe\n");
	kv("boot mpidr", self & 0xffffff);
	regs(kv, self & 0xffffff);
	out("\n");
	for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); t++) {
		uint64_t m = targets[t];
		unsigned slot = (((m >> 8) & 1) << 2) | (m & 3);
		smp_flags[slot] = 0;
		kv("target", m);
		regs(kv, m);
		out("\n");
		int64_t rc = smc3(PSCI_CPU_ON, m, (uintptr_t)smp_entry, slot);
		int up = wait_flag(slot, 300);
		kv(" CPU_ON rc", (uint64_t)rc);
		kv("started", up);
		regs(kv, m);
		out("\n");
		if (up)
			continue;
		/* Power the core (and its cluster sequencer) through the PMU. */
		unsigned c = (m >> 8) & 0xf;
		wr(CL_CPUSEQ(c), rd(CL_CPUSEQ(c)) & ~1u);
		wr(CPU_CFG(m), rd(CPU_CFG(m)) | 0xf);
		uint64_t end = ticks() + TIMER_HZ / 10;
		while (ticks() < end && (rd(CPU_STAT(m)) & 0xf) != 0xf)
			;
		out(" after PMU power-up:");
		regs(kv, m);
		out("\n");
		rc = smc3(PSCI_CPU_ON, m, (uintptr_t)smp_entry, slot);
		up = wait_flag(slot, 300);
		kv(" CPU_ON rc", (uint64_t)rc);
		kv("started", up);
		regs(kv, m);
		out("\n");
	}
	out("SMP probe done\n");
}
