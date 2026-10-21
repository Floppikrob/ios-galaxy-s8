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
 * patches the texture (size, GPU address, row stride) and the source
 * corners, then runs the fragment job: the GPU stretches the iOS frame over
 * the panel instead of the CPU copy. Any GPU error falls back to the CPU
 * path; three errors switch the GPU path off.
 *
 * Boot 41: 17 MiB physically contiguous IOSurfaces could not be allocated,
 * so the GPU has its own page tables (kbase AArch64 4K format, levels 0-3:
 * table = PA | AF | 3, page = PA | attr 0 << 2 | RW | AF | 3; AS_TRANSCFG
 * mode 6). The blob and the panel are mapped GPU VA = PA; each iOS frame
 * surface gets a 32 MiB window at S8GPU_SLOT_VA mapping its scattered pages,
 * rebuilt only when a slot is reused for another surface.
 */
extern "C" void clean_dcache(vm_offset_t addr, unsigned count, int phys);

#define S8GPU_MALI_PA           0x13900000UL
#define S8GPU_PMU_G3D_STAT      0x164840e4UL    /* pd-g3d status, 0xF = on */
#define S8GPU_JOB_IRQ_RAWSTAT   0x1000
#define S8GPU_JOB_IRQ_CLEAR     0x1004
#define S8GPU_JS0               0x1800
#define S8GPU_AS0               0x2400
#define S8GPU_PT_PA             (S8_BLIT_BASE_PA + 0x10000)     /* page-table pool */
#define S8GPU_PT_PAGES          512                             /* 2 MiB pool */
#define S8GPU_SLOTS             4
#define S8GPU_SLOT_VA           0x100000000ULL
#define S8GPU_SLOT_SIZE         0x2000000ULL                    /* 32 MiB */
#define S8GPU_PANEL_PA          0xcc000000ULL
#define S8GPU_PANEL_SIZE        (5760ULL * 2960)
/* GPU memory for iOS programs (/dev/s8mali): free RAM of the high bank,
 * mapped GPU VA = PA. */
#define S8GPU_HEAP_PA           0x8b0000000ULL
#define S8GPU_HEAP_SIZE         0x20000000ULL                   /* 512 MiB */
#define S8GPU_HEAP_UNIT         0x10000ULL                      /* 64 KiB */
#define S8GPU_HEAP_UNITS        (S8GPU_HEAP_SIZE / S8GPU_HEAP_UNIT)
#define S8GPU_ALLOCS            256

static volatile uint32_t *s8gpu_regs;
static volatile uint32_t *s8gpu_blob;
static uint32_t s8gpu_shaders;
static bool s8gpu_ready;
static uint32_t s8gpu_frames, s8gpu_errors, s8gpu_skips;
static volatile uint64_t *s8gpu_pt;             /* S8GPU_PT_PAGES pages of 512 entries */
static unsigned s8gpu_pt_used;
static struct {
	IOMemoryDescriptor *md;                 /* not retained: identity only */
	uint32_t id;
	uint64_t len;
} s8gpu_slot[S8GPU_SLOTS];
static unsigned s8gpu_slot_next;
static IOLock *s8gpu_lock;                      /* one job chain at a time */
static uint8_t s8gpu_heap_used[S8GPU_HEAP_UNITS];
static struct {
	IOMemoryMap *map;                       /* the owner's mapping */
	uint64_t pa, len;
	int client;                             /* /dev/s8mali minor, -1 free */
} s8gpu_alloc[S8GPU_ALLOCS];
static uint32_t s8gpu_user_jobs, s8gpu_user_faults;

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

/* Entry for va at level 3, creating tables; NULL when the pool is full. */
static volatile uint64_t *
s8gpu_pte(uint64_t va)
{
	unsigned table = 0;                     /* level 0 is page 0 */
	for (unsigned level = 0; level < 3; level++) {
		unsigned idx = (unsigned)(va >> (39 - 9 * level)) & 511;
		volatile uint64_t *e = &s8gpu_pt[table * 512 + idx];
		if ((*e & 3) != 3) {
			if (s8gpu_pt_used == S8GPU_PT_PAGES) {
				return NULL;
			}
			unsigned t = s8gpu_pt_used++;
			*e = (S8GPU_PT_PA + (uint64_t)t * 4096) | (1ULL << 10) | 3;
			table = t;
		} else {
			table = (unsigned)(((*e & 0xfffffffff000ULL) - S8GPU_PT_PA) / 4096);
		}
	}
	return &s8gpu_pt[table * 512 + ((va >> 12) & 511)];
}

static bool
s8gpu_map(uint64_t va, uint64_t pa, uint64_t len)
{
	for (uint64_t o = 0; o < len; o += 4096) {
		volatile uint64_t *e = s8gpu_pte(va + o);
		if (e == NULL) {
			return false;
		}
		*e = ((pa + o) & 0xfffffffff000ULL) | (1ULL << 6) | (1ULL << 10) | 3;
	}
	return true;
}

/* After changing mappings: lock the whole VA space and flush (kbase). */
static void
s8gpu_flush_tlb(void)
{
	__builtin_arm_dsb(0xf);
	s8gpu_wr(S8GPU_AS0 + 0x10, 47);                 /* AS_LOCKADDR: 2^48 at 0 */
	s8gpu_wr(S8GPU_AS0 + 0x14, 0);
	s8gpu_wr(S8GPU_AS0 + 0x18, 2);                  /* AS_COMMAND_LOCK */
	s8gpu_wait(S8GPU_AS0 + 0x28, 1, 0, 100000);
	s8gpu_wr(S8GPU_AS0 + 0x18, 5);                  /* AS_COMMAND_FLUSH_MEM */
	s8gpu_wait(S8GPU_AS0 + 0x28, 1, 0, 100000);
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
	s8gpu_lock = IOLockAlloc();
	for (unsigned i = 0; i < S8GPU_ALLOCS; i++) {
		s8gpu_alloc[i].client = -1;
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
	s8gpu_blob = (volatile uint32_t *)ml_io_map(S8_BLIT_BASE_PA, S8_BLIT_SIZE);
	s8gpu_pt = (volatile uint64_t *)ml_io_map(S8GPU_PT_PA, S8GPU_PT_PAGES * 4096);
	if (s8gpu_blob == NULL || s8gpu_pt == NULL) {
		IOLog("S8 GPU: cannot map the blob/page tables at 0x%llx\n", S8_BLIT_BASE_PA);
		return;
	}
	for (unsigned i = 0; i < S8GPU_PT_PAGES * 512; i++) {
		s8gpu_pt[i] = 0;
	}
	s8gpu_pt_used = 1;                              /* page 0: level 0 */
	bool mapped = s8gpu_map(S8_BLIT_BASE_PA, S8_BLIT_BASE_PA, S8_BLIT_SIZE) &&
	    s8gpu_map(S8GPU_PANEL_PA, S8GPU_PANEL_PA, (S8GPU_PANEL_SIZE + 4095) & ~4095ULL);
	__builtin_arm_dsb(0xf);
	s8gpu_wr(S8GPU_AS0 + 0x00, (uint32_t)S8GPU_PT_PA);     /* AS_TRANSTAB */
	s8gpu_wr(S8GPU_AS0 + 0x04, (uint32_t)(S8GPU_PT_PA >> 32));
	s8gpu_wr(S8GPU_AS0 + 0x08, 0x88888888u);        /* AS_MEMATTR: all impl. defined */
	s8gpu_wr(S8GPU_AS0 + 0x0c, 0x88888888u);
	s8gpu_wr(S8GPU_AS0 + 0x30, 6);                  /* AS_TRANSCFG: AARCH64_4K */
	s8gpu_wr(S8GPU_AS0 + 0x34, 0);
	s8gpu_wr(S8GPU_AS0 + 0x18, 1);                  /* AS_COMMAND_UPDATE */
	bool as_on = mapped && s8gpu_wait(S8GPU_AS0 + 0x28, 1, 0, 100000);
	for (unsigned i = 0; i < S8_BLIT_SIZE / 4; i++) {
		uint32_t w;
		memcpy(&w, s8_blit_blob + i * 4, 4);
		s8gpu_blob[i] = w;
	}
	__builtin_arm_dsb(0xf);
	s8gpu_ready = reset && l2_on && tiler_on && sh_on && as_on;
	IOLog("S8 GPU: Mali-G71 0x%x reset=%d l2=%d tiler=%d shaders=0x%x/%d as0=%d pt=%u -> %s\n",
	    s8gpu_rd(0x000), reset, l2_on, tiler_on, s8gpu_shaders, sh_on, as_on, s8gpu_pt_used,
	    s8gpu_ready ? "GPU present on" : "CPU present");
}

/* GPU address of a surface's bytes, mapping it into a slot if needed. */
static uint64_t
s8gpu_surface_va(IOMemoryDescriptor *md, uint32_t id, uint64_t off, uint64_t len)
{
	uint64_t first = off & ~4095ULL, span = ((off + len + 4095) & ~4095ULL) - first;
	if (span > S8GPU_SLOT_SIZE) {
		return 0;
	}
	for (unsigned i = 0; i < S8GPU_SLOTS; i++) {
		if (s8gpu_slot[i].md == md && s8gpu_slot[i].id == id && s8gpu_slot[i].len >= span) {
			return S8GPU_SLOT_VA + i * S8GPU_SLOT_SIZE + (off - first);
		}
	}
	unsigned i = s8gpu_slot_next++ % S8GPU_SLOTS;
	uint64_t va = S8GPU_SLOT_VA + i * S8GPU_SLOT_SIZE;
	s8gpu_slot[i].md = NULL;
	for (uint64_t o = 0; o < span; o += 4096) {
		IOByteCount seg = 0;
		addr64_t pa = md->getPhysicalSegment(first + o, &seg, kIOMemoryMapperNone);
		if (pa == 0 || !s8gpu_map(va + o, pa, 4096)) {
			IOLog("S8 GPU: cannot map surface %u page at 0x%llx\n", id, (unsigned long long)o);
			return 0;
		}
	}
	s8gpu_flush_tlb();
	s8gpu_slot[i].md = md;
	s8gpu_slot[i].id = id;
	s8gpu_slot[i].len = span;
	IOLog("S8 GPU: surface %u mapped in slot %u (%llu KiB, %u table pages)\n",
	    id, i, (unsigned long long)(span >> 10), s8gpu_pt_used);
	return va + (off - first);
}

/* One frame: surface (w x h, bpr) stretched over the panel by the GPU. */
static bool
s8gpu_present_locked(IOMemoryDescriptor *md, uint32_t id, uint64_t off, uintptr_t kva,
    uint32_t bpr, uint32_t w, uint32_t h)
{
	if (!s8gpu_ready || (bpr & 15) != 0 || md == NULL) {
		return false;
	}
	uint64_t need = (uint64_t)(h - 1) * bpr + (uint64_t)w * 4;
	uint64_t pa = s8gpu_surface_va(md, id, off, need);
	if (pa == 0) {
		return false;
	}
	/* CoreAnimation drew through the CPU caches. */
	clean_dcache((vm_offset_t)kva, (unsigned)need, 0);
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

static bool
s8gpu_present(IOMemoryDescriptor *md, uint32_t id, uint64_t off, uintptr_t kva,
    uint32_t bpr, uint32_t w, uint32_t h)
{
	if (!s8gpu_ready || s8gpu_lock == NULL) {
		return false;
	}
	IOLockLock(s8gpu_lock);
	bool ok = s8gpu_present_locked(md, id, off, kva, bpr, w, h);
	IOLockUnlock(s8gpu_lock);
	return ok;
}

/*
 * /dev/s8mali (tools/dreamlte/s8mali_ioctl.h): iOS programs get GPU memory
 * and run job chains. The character device itself is bsd/dev/dreamlte_s8.c;
 * it calls these with the ioctl argument already copied in.
 */
static void
s8gpu_free_index(unsigned i)
{
	uint64_t first = (s8gpu_alloc[i].pa - S8GPU_HEAP_PA) / S8GPU_HEAP_UNIT;
	for (uint64_t u = 0; u < s8gpu_alloc[i].len / S8GPU_HEAP_UNIT; u++) {
		s8gpu_heap_used[first + u] = 0;
	}
	if (s8gpu_alloc[i].map != NULL) {
		s8gpu_alloc[i].map->release();  /* unmaps it from the owner */
	}
	s8gpu_alloc[i].map = NULL;
	s8gpu_alloc[i].client = -1;
}

static uint64_t
s8gpu_heap_free_bytes(void)
{
	uint64_t n = 0;
	for (unsigned u = 0; u < S8GPU_HEAP_UNITS; u++) {
		n += s8gpu_heap_used[u] == 0;
	}
	return n * S8GPU_HEAP_UNIT;
}

static int
s8gpu_user_alloc(int client, struct s8mali_alloc *a)
{
	uint64_t len = (a->size + S8GPU_HEAP_UNIT - 1) & ~(S8GPU_HEAP_UNIT - 1);
	if (len == 0 || len > S8GPU_HEAP_SIZE) {
		return EINVAL;
	}
	unsigned slot = S8GPU_ALLOCS;
	for (unsigned i = 0; i < S8GPU_ALLOCS; i++) {
		if (s8gpu_alloc[i].client < 0) {
			slot = i;
			break;
		}
	}
	if (slot == S8GPU_ALLOCS) {
		return ENOMEM;
	}
	uint64_t units = len / S8GPU_HEAP_UNIT, run = 0, first = 0;
	for (uint64_t u = 0; u < S8GPU_HEAP_UNITS && run < units; u++) {
		if (s8gpu_heap_used[u]) {
			run = 0;
		} else if (run++ == 0) {
			first = u;
		}
	}
	if (run < units) {
		return ENOMEM;
	}
	uint64_t pa = S8GPU_HEAP_PA + first * S8GPU_HEAP_UNIT;
	IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(pa, len,
	    kIODirectionInOut | kIOMemoryTypePhysical64, TASK_NULL);
	if (md == NULL) {
		return ENOMEM;
	}
	/* Zero it through a temporary kernel mapping (no stale data). */
	IOMemoryMap *km = md->createMappingInTask(kernel_task, 0, kIOMapAnywhere | kIOMapWriteCombineCache);
	if (km == NULL) {
		md->release();
		return ENOMEM;
	}
	volatile uint64_t *z = (volatile uint64_t *)km->getVirtualAddress();
	for (uint64_t o = 0; o < len / 8; o++) {
		z[o] = 0;
	}
	km->release();
	IOMemoryMap *um = md->createMappingInTask(current_task(), 0, kIOMapAnywhere | kIOMapWriteCombineCache);
	md->release();                          /* the mapping keeps it */
	if (um == NULL) {
		return ENOMEM;
	}
	if (!s8gpu_map(pa, pa, len)) {
		um->release();
		return ENOMEM;
	}
	s8gpu_flush_tlb();
	for (uint64_t u = 0; u < units; u++) {
		s8gpu_heap_used[first + u] = 1;
	}
	s8gpu_alloc[slot].map = um;
	s8gpu_alloc[slot].pa = pa;
	s8gpu_alloc[slot].len = len;
	s8gpu_alloc[slot].client = client;
	a->size = len;
	a->gpu_va = pa;
	a->user_va = um->getAddress();
	return 0;
}

static int
s8gpu_user_submit(struct s8mali_submit *s)
{
	if (s->slot > 2) {
		return EINVAL;
	}
	unsigned ms = s->timeout_ms == 0 ? 1000 : (s->timeout_ms > 5000 ? 5000 : s->timeout_ms);
	uint32_t js = S8GPU_JS0 + 0x80 * s->slot;
	uint32_t done = 1u << s->slot, fail = 1u << (16 + s->slot);
	uint64_t t0 = mach_absolute_time();
	s8gpu_wr(S8GPU_JOB_IRQ_CLEAR, done | fail);
	s8gpu_wr(js + 0x40, (uint32_t)s->jc);           /* JS_HEAD_NEXT */
	s8gpu_wr(js + 0x44, (uint32_t)(s->jc >> 32));
	s8gpu_wr(js + 0x50, s8gpu_shaders);             /* JS_AFFINITY_NEXT */
	s8gpu_wr(js + 0x54, 0);
	s8gpu_wr(js + 0x58, (8u << 16) | (3u << 12) | (3u << 8));
	s8gpu_wr(js + 0x60, 1);                         /* JS_COMMAND_START */
	uint32_t irq = 0;
	/* Busy-wait the first 2 ms (frame-sized jobs), then sleep-poll. */
	for (unsigned i = 0; i < 200; i++) {
		irq = s8gpu_rd(S8GPU_JOB_IRQ_RAWSTAT);
		if (irq & (done | fail)) {
			break;
		}
		IODelay(10);
	}
	for (unsigned i = 0; !(irq & (done | fail)) && i < ms; i++) {
		IOSleep(1);
		irq = s8gpu_rd(S8GPU_JOB_IRQ_RAWSTAT);
	}
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
	s->elapsed_us = ns / 1000;
	s->irq_rawstat = irq;
	s->js_status = s8gpu_rd(js + 0x24);
	s->as_fault = s8gpu_rd(S8GPU_AS0 + 0x1c);
	s->as_fault_addr = s8gpu_rd(S8GPU_AS0 + 0x20) | (uint64_t)s8gpu_rd(S8GPU_AS0 + 0x24) << 32;
	if (!(irq & (done | fail))) {
		s8gpu_wr(js + 0x60, 3);                 /* JS_COMMAND_HARD_STOP */
		s8gpu_wait(S8GPU_JOB_IRQ_RAWSTAT, done | fail, done | fail, 100000);
		s->result = 2;
	} else {
		s->result = (irq & fail) ? 1 : 0;
	}
	s8gpu_wr(S8GPU_JOB_IRQ_CLEAR, done | fail);
	s8gpu_wr(0x2004, 0xffffffffu);                  /* MMU_IRQ_CLEAR */
	s8gpu_user_jobs++;
	if (s->result != 0 && s8gpu_user_faults++ < 16) {
		IOLog("S8 MALI: slot %u job 0x%llx result %u js 0x%x irq 0x%x as fault 0x%x at 0x%llx\n",
		    s->slot, s->jc, s->result, s->js_status, irq, s->as_fault, s->as_fault_addr);
	}
	return 0;
}

extern "C" int
s8gpu_user_ioctl(int client, unsigned long cmd, void *data)
{
	if (s8gpu_lock == NULL) {
		return ENODEV;
	}
	int err = 0;
	IOLockLock(s8gpu_lock);
	switch (cmd) {
	case S8MALI_INFO: {
		struct s8mali_info *i = (struct s8mali_info *)data;
		i->gpu_id = s8gpu_rd(0x000);
		i->shader_present = s8gpu_shaders;
		i->tiler_features = s8gpu_rd(0x00c);
		i->ready = s8gpu_ready;
		i->heap_base = S8GPU_HEAP_PA;
		i->heap_size = S8GPU_HEAP_SIZE;
		i->heap_free = s8gpu_heap_free_bytes();
		break;
	}
	case S8MALI_ALLOC:
		err = s8gpu_ready ? s8gpu_user_alloc(client, (struct s8mali_alloc *)data) : ENODEV;
		break;
	case S8MALI_FREE: {
		uint64_t va = ((struct s8mali_free *)data)->gpu_va;
		err = ENOENT;
		for (unsigned i = 0; i < S8GPU_ALLOCS; i++) {
			if (s8gpu_alloc[i].client == client && s8gpu_alloc[i].pa == va) {
				s8gpu_free_index(i);
				err = 0;
				break;
			}
		}
		break;
	}
	case S8MALI_SUBMIT:
		if (s8gpu_ready) {
			err = s8gpu_user_submit((struct s8mali_submit *)data);
		} else {
			((struct s8mali_submit *)data)->result = 3;
		}
		break;
	case S8MALI_LOG: {
		struct s8mali_log *l = (struct s8mali_log *)data;
		l->text[sizeof(l->text) - 1] = 0;
		IOLog("S8 MALI| %s\n", l->text);
		break;
	}
	default:
		err = ENOTTY;
	}
	IOLockUnlock(s8gpu_lock);
	return err;
}

extern "C" void
s8gpu_user_close(int client)
{
	if (s8gpu_lock == NULL) {
		return;
	}
	IOLockLock(s8gpu_lock);
	unsigned n = 0;
	for (unsigned i = 0; i < S8GPU_ALLOCS; i++) {
		if (s8gpu_alloc[i].client == client) {
			s8gpu_free_index(i);
			n++;
		}
	}
	IOLockUnlock(s8gpu_lock);
	if (n != 0) {
		IOLog("S8 MALI: client %d closed, %u allocations freed (%u jobs so far)\n", client, n, s8gpu_user_jobs);
	}
}
