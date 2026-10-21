/* SPDX-License-Identifier: GPL-2.0-only
 * Galaxy S8 display pipeline probe, run by the loader (MMU off).
 *
 * Goal: show iOS frames without the CPU copy into the S-Boot framebuffer,
 * by pointing the DPU's input DMA at the frame instead (a "flip").
 * Exynos 8895 DPU (Samsung kernel drivers/video/fbdev/exynos/dpu): DECON_F
 * 0x12860000 (SHADOW_REG_UPDATE_REQ 0x60, WIN_CONTROL_0(w) 0x1000 + w*0x30)
 * fed by six DPP channels whose IDMA registers are the second DT reg block
 * (0x128b1000 + n*0x1000, n = 0..5; the sixth DPP's at 0x128b7000):
 * IDMA_ENABLE 0x00, IN_CON 0x08, SRC_SIZE 0x10, SRC_OFFSET 0x14,
 * IMG_SIZE 0x18, IN_BASE_ADDR_Y 0x40. The DT says default_idma = 0.
 *
 * Read-only dump, then one visible test on the channel that scans the
 * S-Boot framebuffer: its base moves 800 lines down for 3 s (the picture
 * jumps up), then it is restored.
 */
#include <stdint.h>
#include "display_probe.h"

#define DECON			0x12860000UL
#define SHADOW_REQ		0x60
#define SHADOW_GLOBAL		(1u << 31)
#define FB_PHYS			0xcc000000u
#define FB_STRIDE		(1440u * 4)
#define TIMER_HZ		26000000UL

static const uintptr_t idma[6] = {
	0x128b1000, 0x128b2000, 0x128b3000, 0x128b4000, 0x128b5000, 0x128b7000,
};
static const uintptr_t sysmmu[3] = { 0x12900000, 0x12910000, 0x12920000 };

static inline uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static uint64_t ticks(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static void delay_ms(unsigned ms)
{
	uint64_t end = ticks() + TIMER_HZ / 1000 * ms;
	while (ticks() < end)
		;
}

/* decon_reg_all_win_shadow_update_req + global (Samsung decon_reg.c). */
static void update(void)
{
	wr(DECON + SHADOW_REQ, rd(DECON + SHADOW_REQ) | SHADOW_GLOBAL | 0x3f);
	for (unsigned n = 0; n < 100 && rd(DECON + SHADOW_REQ); n++)
		delay_ms(1);
}

/* DECON INTERRUPT_PENDING (0x4c) bit 13 DPU_FRAME_DONE over 0.5 s, with
 * the frame-done source enabled (0x40 bit 13) for the count only. */
static uint64_t frames(void)
{
	uint32_t en = rd(DECON + 0x40);
	wr(DECON + 0x40, en | (1u << 13) | 1u);
	wr(DECON + 0x4c, 1u << 13);
	uint64_t n = 0, end = ticks() + TIMER_HZ / 2;
	while (ticks() < end) {
		if (rd(DECON + 0x4c) & (1u << 13)) {
			n++;
			wr(DECON + 0x4c, 1u << 13);
		}
	}
	wr(DECON + 0x40, en);
	return n;
}

/*
 * DECON output CRC (Samsung decon_reg.c decon_reg_set_start_crc /
 * get_crc_data: CRC_CONTROL 0x288 bit 0 start, CRC_DATA 0x280). A CRC
 * that changes when only the IDMA base moves proves the DECON fetches
 * from the new address: the zero-copy flip works without anyone watching
 * the screen. Two frames per sample so the CRC covers a whole new frame.
 */
static uint32_t crc(void)
{
	wr(DECON + 0x288, rd(DECON + 0x288) | 1u);
	wr(DECON + 0x4c, 1u << 13);
	for (unsigned f = 0; f < 3; f++) {
		uint64_t end = ticks() + TIMER_HZ / 20;
		while (ticks() < end && !(rd(DECON + 0x4c) & (1u << 13)))
			;
		wr(DECON + 0x4c, 1u << 13);
	}
	return rd(DECON + 0x280);
}

/* IDMA_ENABLE / DPP_ENABLE bit 4: SFR_UPDATE_FORCE (dpp_reg.c). */
static void force(unsigned n)
{
	static const uintptr_t dpp[6] = {
		0x12851000, 0x12852000, 0x12853000, 0x12854000, 0x12855000, 0x12890000,
	};
	wr(idma[n], rd(idma[n]) | (1u << 4));
	wr(dpp[n], rd(dpp[n]) | (1u << 4));
}

void display_probe(void (*kv)(const char *, uint64_t), void (*out)(const char *))
{
	out("DISPLAY probe (DECON_F + IDMA)\n");
	kv("decon 0x0", rd(DECON + 0x0));
	kv("0x60", rd(DECON + SHADOW_REQ));
	kv("0x70", rd(DECON + 0x70));
	kv("dp0", rd(DECON + 0x214));
	out("\n");
	for (unsigned w = 0; w < 6; w++) {
		uint32_t c = rd(DECON + 0x1000 + w * 0x30);
		if (c) {
			kv("win", w);
			kv("ctl0", c);
			kv("ctl1", rd(DECON + 0x1004 + w * 0x30));
			out("\n");
		}
	}
	int scan = -1;
	for (unsigned n = 0; n < 6; n++) {
		uintptr_t b = idma[n];
		uint32_t base = rd(b + 0x40);
		kv("idma", n);
		kv("en", rd(b + 0x00));
		kv("in", rd(b + 0x08));
		kv("src", rd(b + 0x10));
		kv("off", rd(b + 0x14));
		kv("img", rd(b + 0x18));
		kv("Y", base);
		out("\n");
		if (base == FB_PHYS && scan < 0)
			scan = (int)n;
	}
	if (scan >= 0) {
		/* Boot 29: forcing IDMA/DPP SFR updates left IDMA_ENABLE bit 4 set
		 * (0x14) and iOS showed heavy artifacts; no forcing any more. */
		(void)force;
		kv("flip test on idma", (uint64_t)scan);
		kv("frames/0.5s", frames());
		uint32_t en40 = rd(DECON + 0x40);
		wr(DECON + 0x40, en40 | (1u << 13) | 1u);
		uint32_t c0 = crc(), c0b = crc();
		kv("crc", c0);
		kv("crc again", c0b);
		wr(DECON + 0x40, en40);
		out("\n");
		uintptr_t y = idma[scan] + 0x40;
		out("TEST: base +800 lines. PICTURE JUMPS UP NOW?\n");
		wr(y, FB_PHYS + FB_STRIDE * 800);
		wr(DECON + SHADOW_REQ, rd(DECON + SHADOW_REQ) | SHADOW_GLOBAL | 0x3f);
		kv("req after write", rd(DECON + SHADOW_REQ));
		kv("frames/0.5s", frames());
		kv("req", rd(DECON + SHADOW_REQ));
		kv("Y", rd(y));
		wr(DECON + 0x40, en40 | (1u << 13) | 1u);
		uint32_t c1 = crc();
		wr(DECON + 0x40, en40);
		kv("crc moved", c1);
		/* Stable CRC before, a different one after: the flip works. */
		kv("FLIP WORKS", c0 == c0b && c1 != c0);
		out("\n");
		delay_ms(2500);
		wr(y, FB_PHYS);
		update();
		kv("restored Y", rd(y));
		kv("idma en", rd(idma[scan]));
		out("\nTEST DONE\n");
	} else {
		out("no IDMA reads 0xcc000000: no flip test\n");
	}
	/* Last: an IOMMU whose clock is gated could stall the bus. */
	for (unsigned s = 0; s < 3; s++) {
		kv("sysmmu", sysmmu[s]);
		kv("ctrl", rd(sysmmu[s] + 0x0));
		kv("cfg", rd(sysmmu[s] + 0x4));
		out("\n");
	}
	out("DISPLAY probe done\n");
}
