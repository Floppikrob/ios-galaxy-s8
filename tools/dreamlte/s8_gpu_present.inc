/*
 * Galaxy S8: iOS frames presented by the Mali-G71 (boot-arg sweetfb_gpu=1).
 *
 * Loader boots 37/38 proved the GPU path on this phone: soft reset, L2/
 * tiler/20 shader cores powered, address space 0 in IDENTITY mode (GPU VA
 * = PA), jobs on slot 0, and a fragment job writing the scanned-out panel.
 * Here the kernel does the same once, copies the descriptor blob generated
 * by tools/dreamlte/mali-blit-gen.c (Mesa Panfrost v6: pre-frame shader
 * VAR_TEX + BLEND, unnormalised nearest sampler) to the free RAM right
 * after XNU's 512 MiB (S8_BLIT_BASE_PA), and for each full-screen layer
 * patches the texture (size, physical pointer, row stride) and the source
 * corners, then runs the fragment job: the GPU stretches the iOS frame over
 * the panel instead of the CPU copy. Any GPU error falls back to the CPU
 * path; three errors switch the GPU path off.
 */
extern "C" void clean_dcache(vm_offset_t addr, unsigned count, int phys);

#define S8GPU_MALI_PA           0x13900000UL
#define S8GPU_PMU_G3D_STAT      0x164840e4UL    /* pd-g3d status, 0xF = on */
#define S8GPU_JOB_IRQ_RAWSTAT   0x1000
#define S8GPU_JOB_IRQ_CLEAR     0x1004
#define S8GPU_JS0               0x1800
#define S8GPU_AS0               0x2400

static volatile uint32_t *s8gpu_regs;
static volatile uint32_t *s8gpu_blob;
static uint32_t s8gpu_shaders;
static bool s8gpu_ready;
static uint32_t s8gpu_frames, s8gpu_errors, s8gpu_skips;

static inline uint32_t
s8gpu_rd(uint32_t off)
{
	return s8gpu_regs[off / 4];
}

static inline void
s8gpu_wr(uint32_t off, uint32_t v)
{
	s8gpu_regs[off / 4] = v;
}

static bool
s8gpu_wait(uint32_t off, uint32_t mask, uint32_t want, unsigned us)
{
	for (unsigned i = 0; i < us; i++) {
		if ((s8gpu_rd(off) & mask) == want) {
			return true;
		}
		IODelay(1);
	}
	return (s8gpu_rd(off) & mask) == want;
}

/* IEEE-754 bits of a small positive integer, without FP registers. */
static uint32_t
s8gpu_float(uint32_t v)
{
	if (v == 0) {
		return 0;
	}
	uint32_t e = 31 - (uint32_t)__builtin_clz(v);
	uint32_t m = e > 23 ? (v >> (e - 23)) : (v << (23 - e));
	return ((e + 127) << 23) | (m & 0x7fffff);
}

static void
s8gpu_init(void)
{
	int on = 0;
	if (!PE_parse_boot_argn("sweetfb_gpu", &on, sizeof(on)) || on == 0) {
		return;
	}
	volatile uint32_t *pmu = (volatile uint32_t *)ml_io_map(S8GPU_PMU_G3D_STAT & ~0xfffUL, 0x1000);
	if (pmu == NULL || (pmu[(S8GPU_PMU_G3D_STAT & 0xfff) / 4] & 0xf) != 0xf) {
		IOLog("S8 GPU: pd-g3d is off (loader s8gpu=1 powers it); CPU present\n");
		return;
	}
	s8gpu_regs = (volatile uint32_t *)ml_io_map(S8GPU_MALI_PA, 0x5000);
	if (s8gpu_regs == NULL || (s8gpu_rd(0x000) >> 16) != 0x60a0) {
		IOLog("S8 GPU: no Mali-G71 (GPU_ID 0x%x)\n", s8gpu_regs ? s8gpu_rd(0x000) : 0);
		return;
	}
	s8gpu_wr(0x024, 0xffffffffu);                   /* GPU_IRQ_CLEAR */
	s8gpu_wr(0x030, 1);                             /* SOFT_RESET */
	bool reset = s8gpu_wait(0x020, 1u << 8, 1u << 8, 100000);
	s8gpu_wr(0x024, 0xffffffffu);
	uint32_t l2 = s8gpu_rd(0x120), tiler = s8gpu_rd(0x110);
	s8gpu_shaders = s8gpu_rd(0x100);
	s8gpu_wr(0x1a0, l2);
	bool l2_on = s8gpu_wait(0x160, l2, l2, 100000);
	s8gpu_wr(0x190, tiler);
	bool tiler_on = s8gpu_wait(0x150, tiler, tiler, 100000);
	s8gpu_wr(0x180, s8gpu_shaders);
	bool sh_on = s8gpu_wait(0x140, s8gpu_shaders, s8gpu_shaders, 100000);
	s8gpu_wr(S8GPU_AS0 + 0x30, 2);                  /* AS_TRANSCFG: IDENTITY */
	s8gpu_wr(S8GPU_AS0 + 0x34, 0);
	s8gpu_wr(S8GPU_AS0 + 0x08, 0x88888888u);        /* AS_MEMATTR */
	s8gpu_wr(S8GPU_AS0 + 0x0c, 0x88888888u);
	s8gpu_wr(S8GPU_AS0 + 0x00, 0);
	s8gpu_wr(S8GPU_AS0 + 0x04, 0);
	s8gpu_wr(S8GPU_AS0 + 0x18, 1);                  /* AS_COMMAND_UPDATE */
	bool as_on = s8gpu_wait(S8GPU_AS0 + 0x28, 1, 0, 100000);
	s8gpu_blob = (volatile uint32_t *)ml_io_map(S8_BLIT_BASE_PA, S8_BLIT_SIZE);
	if (s8gpu_blob == NULL) {
		IOLog("S8 GPU: cannot map the descriptor blob at 0x%llx\n", S8_BLIT_BASE_PA);
		return;
	}
	for (unsigned i = 0; i < S8_BLIT_SIZE / 4; i++) {
		uint32_t w;
		memcpy(&w, s8_blit_blob + i * 4, 4);
		s8gpu_blob[i] = w;
	}
	__builtin_arm_dsb(0xf);
	s8gpu_ready = reset && l2_on && tiler_on && sh_on && as_on;
	IOLog("S8 GPU: Mali-G71 0x%x reset=%d l2=%d tiler=%d shaders=0x%x/%d as0=%d -> %s\n",
	    s8gpu_rd(0x000), reset, l2_on, tiler_on, s8gpu_shaders, sh_on, as_on,
	    s8gpu_ready ? "GPU present on" : "CPU present");
}

/* One frame: texture at physical pa (w x h, bpr) stretched over the panel. */
static bool
s8gpu_present(uint64_t pa, uint32_t bpr, uint32_t w, uint32_t h)
{
	if (!s8gpu_ready || (bpr & 15) != 0) {
		return false;
	}
	/* Fresh job header and payload: the GPU writes its status back. */
	for (unsigned i = 0; i < 0x40 / 4; i++) {
		uint32_t v;
		memcpy(&v, s8_blit_blob + S8_BLIT_JOB + i * 4, 4);
		s8gpu_blob[(S8_BLIT_JOB / 4) + i] = v;
	}
	s8gpu_blob[S8_BLIT_TEX_SIZE / 4] = (w - 1) | ((h - 1) << 16);
	s8gpu_blob[S8_BLIT_SURF_PTR / 4] = (uint32_t)pa;
	s8gpu_blob[S8_BLIT_SURF_PTR / 4 + 1] = (uint32_t)(pa >> 32);
	s8gpu_blob[S8_BLIT_SURF_STRIDE / 4] = bpr;
	uint32_t fw = s8gpu_float(w), fh = s8gpu_float(h);
	uint32_t tc = S8_BLIT_TEXCOORD / 4;     /* corners 0,0 / w,0 / 0,h / w,h */
	s8gpu_blob[tc + 0] = 0;
	s8gpu_blob[tc + 1] = 0;
	s8gpu_blob[tc + 4] = fw;
	s8gpu_blob[tc + 5] = 0;
	s8gpu_blob[tc + 8] = 0;
	s8gpu_blob[tc + 9] = fh;
	s8gpu_blob[tc + 12] = fw;
	s8gpu_blob[tc + 13] = fh;
	__builtin_arm_dsb(0xf);

	uint64_t job = S8_BLIT_BASE_PA + S8_BLIT_JOB;
	s8gpu_wr(S8GPU_JOB_IRQ_CLEAR, 0xffffffffu);
	s8gpu_wr(S8GPU_JS0 + 0x40, (uint32_t)job);      /* JS_HEAD_NEXT */
	s8gpu_wr(S8GPU_JS0 + 0x44, (uint32_t)(job >> 32));
	s8gpu_wr(S8GPU_JS0 + 0x50, s8gpu_shaders);      /* JS_AFFINITY_NEXT */
	s8gpu_wr(S8GPU_JS0 + 0x54, 0);
	/* AS 0, clean+invalidate GPU caches at start and end, priority 8. */
	s8gpu_wr(S8GPU_JS0 + 0x58, (8u << 16) | (3u << 12) | (3u << 8));
	s8gpu_wr(S8GPU_JS0 + 0x60, 1);                  /* JS_COMMAND_START */
	uint32_t irq = 0;
	for (unsigned i = 0; i < 20000; i++) {          /* 200 ms */
		irq = s8gpu_rd(S8GPU_JOB_IRQ_RAWSTAT);
		if (irq & ((1u << 16) | 1u)) {
			break;
		}
		IODelay(10);
	}
	s8gpu_wr(S8GPU_JOB_IRQ_CLEAR, 0xffffffffu);
	if ((irq & 1u) && !(irq & (1u << 16))) {
		if ((++s8gpu_frames % 300) == 1) {
			IOLog("S8 GPU: %u frames presented by the GPU (%ux%u stride %u at 0x%llx)\n",
			    s8gpu_frames, w, h, bpr, pa);
		}
		return true;
	}
	IOLog("S8 GPU: present failed irq=0x%x js0 status=0x%x job exc=0x%x as0 fault=0x%x addr=0x%x\n",
	    irq, s8gpu_rd(S8GPU_JS0 + 0x24), s8gpu_blob[S8_BLIT_JOB / 4], s8gpu_rd(S8GPU_AS0 + 0x1c),
	    s8gpu_rd(S8GPU_AS0 + 0x20));
	if (++s8gpu_errors >= 3) {
		s8gpu_ready = false;
		IOLog("S8 GPU: GPU present off after %u errors; CPU present\n", s8gpu_errors);
	}
	return false;
}
