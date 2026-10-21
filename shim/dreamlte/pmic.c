/* SPDX-License-Identifier: GPL-2.0-only
 * Galaxy S8 PMIC (S2MPS17) access over the Exynos SPEEDY bus, MMU off.
 *
 * touch_probe v25 found the touch controller unpowered under our boot: SDA
 * held low (HSI2C master state LOSE) and the gpa1-0 interrupt line at 0.
 * Its rails are S2MPS17 LDO34 "tsp_io" 1.85 V and LDO35 "tsp_avdd" 3.0 V;
 * S-Boot leaves them off and Samsung's Linux turns them on. The SPEEDY
 * transfer and the LDO values follow uniLoader (board-dreamlte.c,
 * drivers/samsung/exynos-speedy.c); the RTC layout follows Samsung's
 * rtc-s2mp.h (RTC on SPEEDY slave 2, binary counters from 0x04).
 */
#include <stdint.h>
#include "pmic.h"

#define SPEEDY			0x15b50000UL
#define SP_FIFO_CTRL		0x004
#define SP_CMD			0x008
#define SP_INT_ENABLE		0x00c
#define SP_INT_STATUS		0x010
#define SP_TX_DATA		0x034
#define SP_RX_DATA		0x038
#define SP_FIFO_RESET		(1u << 31)
#define SP_LEN(rx, tx)		((rx) | (tx) << 8)
#define SP_RANDOM		(1u << 19)
#define SP_WRITE		(1u << 20)
#define SP_SLAVE(x)		(((x) & 0xf) << 15)
#define SP_ADDR(x)		(((x) & 0xff) << 7)
#define SP_DONE			(1u << 0)

#define SLAVE_PMIC		1
#define SLAVE_RTC		2
#define L34CTRL			0x5f
#define L35CTRL			0x60
#define LDO_EN			(3u << 6)
#define L34_1V85		(LDO_EN | (1850000 - 700000) / 25000)
#define L35_3V0			(LDO_EN | (3000000 - 1800000) / 25000)
#define B2CTRL			0x1b	/* BUCK2 vdd_cpucl0 (Mongoose) */
#define B2OUT			0x1c
#define B3CTRL			0x1d	/* BUCK3 vdd_cpucl1 (A53) */
#define B3OUT			0x1e
#define B6CTRL			0x23	/* BUCK6 vdd_g3d (Mali) */
#define B6OUT1			0x24
#define BUCK_EN			(3u << 6)	/* S2MPS17_ENABLE_MASK */
#define BUCK_VSEL(uv)		(((uv) - 300000) / 6250)	/* BUCK_MIN1, STEP1 */
#define RTC_UPDATE		0x02
#define RTC_RUDR		0x01
#define RTC_SEC			0x04

#define TIMER_HZ		26000000UL

static void (*kv)(const char *, uint64_t);
static void (*out)(const char *);

static inline uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static uint64_t ticks(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static void delay_us(unsigned us)
{
	uint64_t end = ticks() + TIMER_HZ / 1000000 * us;
	while (ticks() < end)
		;
}

/* One random-access byte transfer; returns the byte read, or < 0. */
static int speedy(unsigned slave, unsigned reg, int write, uint8_t val)
{
	wr(SPEEDY + SP_FIFO_CTRL, SP_FIFO_RESET);
	delay_us(10);
	wr(SPEEDY + SP_FIFO_CTRL, SP_LEN(1, 1));
	wr(SPEEDY + SP_INT_ENABLE, write ? 0x80300011u : 0x80070301u);
	wr(SPEEDY + SP_INT_STATUS, 0xffffffffu);
	wr(SPEEDY + SP_CMD, SP_RANDOM | (write ? SP_WRITE : 0) |
	   SP_SLAVE(slave) | SP_ADDR(reg));
	if (write)
		wr(SPEEDY + SP_TX_DATA, val);
	uint64_t end = ticks() + TIMER_HZ / 100;
	while (ticks() < end) {
		uint32_t s = rd(SPEEDY + SP_INT_STATUS);
		if (s & SP_DONE) {
			wr(SPEEDY + SP_INT_STATUS, 0xffffffffu);
			return write ? 0 : (int)(rd(SPEEDY + SP_RX_DATA) & 0xff);
		}
	}
	return -1;
}

static void show(const char *k, unsigned slave, unsigned reg)
{
	int v = speedy(slave, reg, 0, 0);
	kv(k, v < 0 ? 0xdead : (uint64_t)v);
}

/* Cycles of this core per 10 ms of the 26 MHz system timer. */
static void cpu_freq(void)
{
	uint64_t pmcr;
	__asm__ volatile("mrs %0, pmcr_el0" : "=r"(pmcr));
	__asm__ volatile("msr pmcr_el0, %0" :: "r"(pmcr | 1 | 4));	/* E, reset C */
	__asm__ volatile("msr pmcntenset_el0, %0" :: "r"(1UL << 31));
	__asm__ volatile("msr pmccfiltr_el0, %0" :: "r"(1UL << 27));	/* count EL2 too */
	uint64_t c0, c1, t0 = ticks();
	__asm__ volatile("isb; mrs %0, pmccntr_el0" : "=r"(c0));
	while (ticks() - t0 < TIMER_HZ / 100)
		;
	__asm__ volatile("isb; mrs %0, pmccntr_el0" : "=r"(c1));
	kv("cpu MHz", (c1 - c0) / 10000);
}

uint64_t pmic_init(void (*kv_fn)(const char *, uint64_t), void (*out_fn)(const char *))
{
	kv = kv_fn;
	out = out_fn;
	out("PMIC (S2MPS17 over SPEEDY 0x15b50000)\n");
	cpu_freq();
	show("pmic id", SLAVE_PMIC, 0x00);
	show("L34", SLAVE_PMIC, L34CTRL);
	show("L35", SLAVE_PMIC, L35CTRL);
	out("\n");
	int r1 = speedy(SLAVE_PMIC, L34CTRL, 1, L34_1V85);
	int r2 = speedy(SLAVE_PMIC, L35CTRL, 1, L35_3V0);
	kv("write rc", (uint64_t)(uint32_t)(r1 | r2));
	show("L34", SLAVE_PMIC, L34CTRL);
	show("L35", SLAVE_PMIC, L35CTRL);
	out("\n");

	/* RTC: latch the counters (RUDR), then sec..year at 0x04..0x0a. */
	int u = speedy(SLAVE_RTC, RTC_UPDATE, 0, 0);
	speedy(SLAVE_RTC, RTC_UPDATE, 1, (uint8_t)((u < 0 ? 0 : u) | RTC_RUDR));
	delay_us(200);
	uint64_t raw = 0;
	out("RTC");
	show(" ctrl", SLAVE_RTC, 0x00);
	for (unsigned i = 0; i < 8; i++) {
		int v = speedy(SLAVE_RTC, RTC_SEC + i, 0, 0);
		raw |= (uint64_t)(v < 0 ? 0xff : v) << (8 * i);
	}
	kv(" sec..year", raw);
	out("\n");
	return raw;
}

/*
 * Mongoose cluster rail. Boot 33: with its cores powered in the PMU the
 * cluster's NONCPU and L2 status stayed off and its PLL never locked. The
 * rail is S2MPS17 BUCK2 "vdd_cpucl0" (dream2lte_common.dtsi: 0.45-1.2 V,
 * regulator-always-on, so Samsung's Linux switches it on itself; S-Boot
 * may leave it off). Log BUCK2 next to BUCK3 (the running A53 rail); if
 * BUCK2 is not enabled, set at least 0.8 V (1066 MHz is the PLL's boot
 * rate) and enable it.
 */
void pmic_mngs_rail(void)
{
	out("MNGS rail");
	show(" b2ctrl", SLAVE_PMIC, B2CTRL);
	show("b2out", SLAVE_PMIC, B2OUT);
	show("b3ctrl", SLAVE_PMIC, B3CTRL);
	show("b3out", SLAVE_PMIC, B3OUT);
	out("\n");
	int ctrl = speedy(SLAVE_PMIC, B2CTRL, 0, 0);
	int vsel = speedy(SLAVE_PMIC, B2OUT, 0, 0);
	if (ctrl < 0 || vsel < 0 || (ctrl & BUCK_EN) == BUCK_EN)
		return;
	if (vsel < BUCK_VSEL(800000) || vsel > BUCK_VSEL(1100000))
		speedy(SLAVE_PMIC, B2OUT, 1, BUCK_VSEL(800000));
	speedy(SLAVE_PMIC, B2CTRL, 1, (uint8_t)(ctrl | BUCK_EN));
	delay_us(1000);			/* 12 mV/us ramp: < 100 us */
	out("MNGS rail on");
	show(" b2ctrl", SLAVE_PMIC, B2CTRL);
	show("b2out", SLAVE_PMIC, B2OUT);
	out("\n");
}

/*
 * Mali rail: S2MPS17 BUCK6 "vdd_g3d" (dreamlte_common.dtsi: 0.45-0.85 V,
 * regulator-always-on). Log it; if it is not enabled, set 0.8 V and
 * enable it.
 */
void pmic_g3d_rail(void)
{
	out("G3D rail");
	show(" b6ctrl", SLAVE_PMIC, B6CTRL);
	show("b6out1", SLAVE_PMIC, B6OUT1);
	out("\n");
	int ctrl = speedy(SLAVE_PMIC, B6CTRL, 0, 0);
	int vsel = speedy(SLAVE_PMIC, B6OUT1, 0, 0);
	if (ctrl < 0 || vsel < 0 || (ctrl & BUCK_EN))
		return;
	if (vsel < BUCK_VSEL(600000) || vsel > BUCK_VSEL(850000))
		speedy(SLAVE_PMIC, B6OUT1, 1, BUCK_VSEL(800000));
	speedy(SLAVE_PMIC, B6CTRL, 1, (uint8_t)(ctrl | BUCK_EN));
	delay_us(1000);
	out("G3D rail on");
	show(" b6ctrl", SLAVE_PMIC, B6CTRL);
	show("b6out1", SLAVE_PMIC, B6OUT1);
	out("\n");
}

/*
 * "s8boost=1": raise the A53 cluster from S-Boot's 970.67 MHz to 1449.5 MHz,
 * an entry of Samsung's pll_cpucl1_rate_table (M/P/S 223/4/0). Boot 42
 * showed iOS bound by CoreAnimation's CPU renderer. Voltage first: BUCK3
 * read 0x49 (756 mV); 1456 MHz needs at most ~0.9 V in the 8895 ASV
 * table, so set 0.95 V. Then Samsung's ra_pll_set_pmsk order: mux to the
 * 26 MHz input, disable, LOCKTIME = P * 150, new PMS + enable, wait for
 * STABLE, mux back. On a lock timeout the old PMS is restored.
 */
#define CMU_CPUCL1		0x16900000UL
#define PLL_LOCKTIME		0x0000
#define PLL_CON0		0x0120
#define PLL_EN			(1u << 31)
#define PLL_STABLE		(1u << 29)
#define PLL_MUX_BUSY		(1u << 7)
#define PLL_MUX_SEL		(1u << 4)
#define PLL_PMS_MASK		((0x3ffu << 16) | (0x3fu << 8) | 7u)
#define PLL_PMS(m, p, s)	((m) << 16 | (p) << 8 | (s))

static int pll_set(uint32_t pms)
{
	uintptr_t con = CMU_CPUCL1 + PLL_CON0;
	uint32_t v = rd(con);
	wr(con, v & ~PLL_MUX_SEL);
	uint64_t end = ticks() + TIMER_HZ / 100;
	while (ticks() < end && (rd(con) & PLL_MUX_BUSY))
		;
	wr(con, rd(con) & ~PLL_EN);
	wr(CMU_CPUCL1 + PLL_LOCKTIME, ((pms >> 8) & 0x3f) * 150);
	wr(con, (rd(con) & ~PLL_PMS_MASK) | pms | PLL_EN);
	end = ticks() + TIMER_HZ / 100;
	while (ticks() < end && !(rd(con) & PLL_STABLE))
		;
	int ok = (rd(con) & PLL_STABLE) != 0;
	if (ok)
		wr(con, rd(con) | PLL_MUX_SEL);
	return ok;
}

void pmic_cpu_boost(void)
{
	out("A53 boost");
	show(" b3out", SLAVE_PMIC, B3OUT);
	kv("cl1 pll", rd(CMU_CPUCL1 + PLL_CON0));
	out("\n");
	int vsel = speedy(SLAVE_PMIC, B3OUT, 0, 0);
	if (vsel < 0 || vsel > BUCK_VSEL(1000000)) {
		out("A53 boost skipped: BUCK3 unreadable or already high\n");
		return;
	}
	uint32_t old = rd(CMU_CPUCL1 + PLL_CON0) & PLL_PMS_MASK;
	if (vsel < BUCK_VSEL(950000)) {
		speedy(SLAVE_PMIC, B3OUT, 1, BUCK_VSEL(950000));
		delay_us(1000);
	}
	int ok = pll_set(PLL_PMS(223, 4, 0));
	if (!ok)
		pll_set(old);
	out(ok ? "A53 boost ok" : "A53 boost FAILED, restored");
	show(" b3out", SLAVE_PMIC, B3OUT);
	kv("cl1 pll", rd(CMU_CPUCL1 + PLL_CON0));
	out("\n");
	cpu_freq();
}
