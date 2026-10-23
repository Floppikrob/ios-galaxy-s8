/*
 * S8MaliMetal stage 3: Metal render passes on the Mali-G71 (Bifrost v6).
 *
 * After PanVK's job-manager path (src/panfrost/vulkan/jm in Mesa 25.2.8),
 * reduced to what CoreAnimation asks for: one colour attachment, no depth or
 * stencil, no MSAA. Descriptors are packed with Mesa's genxml headers into
 * per-command-buffer GPU memory (write-combined, VA = PA, see s8mtl_core.m)
 * while the encoders record, so executing a pass only finishes its
 * framebuffer, tiler and thread storage descriptors and submits:
 *
 *   slot 1: per draw a VERTEX job (vertex shader -> varying buffers) and a
 *           TILER job (primitive assembly, binning into the tiler heap);
 *   slot 0: one FRAGMENT job over the render target's 16x16 tiles; its
 *           framebuffer descriptor clears the tile buffer, or reloads the
 *           target with the "@preload" frame shader (MTLLoadActionLoad).
 *
 * Shader ABI (tools/dreamlte/metal/air2nir.cpp): Metal constant buffer N is
 * UBO N, device buffers are 64-bit addresses in UBO 30, function constants
 * UBO 29 (index * 16: value, +8 defined), the shader's constant tables UBO
 * 28, viewport scale/offset and render target conversions UBO 31; textures
 * 0..30 plus a transparent 1x1 at 31; samplers 0..15, inline samplers from
 * 16; stage_in attribute L is attribute L; varyings are linked by location
 * (one table for all shaders of a cache).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "genxml/gen_macros.h"
#include "pan_desc.h"
#include "pan_encoder.h"
#include "pan_format.h"
#include "pan_jc.h"
#include "pan_util.h"
#include "util/blend.h"
#include "util/format/u_format.h"
#include "util/u_math.h"
#include "compiler/shader_enums.h"

#include "s8mtl_core.h"
#include "s8mtl_draw.h"
#include "../s8mali_ioctl.h"

#define S8_UBO_DATA     28
#define S8_UBO_FC       29
#define S8_UBO_ADDRS    30
#define S8_UBO_SYSVALS  31
#define S8_UBO_COUNT    32
#define S8_TEX_COUNT    32
#define S8_NULL_TEXTURE 31
#define S8_SAMPLER_COUNT 32
#define S8_INLINE_SAMPLER_BASE 16
/* UBO S8_UBO_SYSVALS layout (air2nir.cpp) */
#define S8_SV_VERTEX_OFFSET 96
#define S8_SV_TEX_SIZE  128
#define S8_SV_SIZE      (128 + 32 * 8)

#define S8_CACHE_PATH   "/System/Library/Extensions/S8MaliMetal.bundle/quartzcore.s8sc"
#define S8_TILER_HEAP   (32u << 20)
#define S8_TILE_SIZE    (16 * 16)
#define S8_CHUNK        (1u << 20)

/* Varying buffers, as PanVK's PANVK_VARY_BUF_*. */
enum { S8_VARY_GENERAL, S8_VARY_POSITION, S8_VARY_PSIZ, S8_VARY_COUNT };

/* ---- fixed-function blending (libs8mesa: Mesa's pan_blend.c, FF part) ---- */

struct pan_blend_equation {
	unsigned blend_enable : 1;
	enum pipe_blend_func rgb_func : 3;
	enum pipe_blendfactor rgb_src_factor : 5;
	enum pipe_blendfactor rgb_dst_factor : 5;
	enum pipe_blend_func alpha_func : 3;
	enum pipe_blendfactor alpha_src_factor : 5;
	enum pipe_blendfactor alpha_dst_factor : 5;
	unsigned color_mask : 4;
	unsigned padding : 1;
};
bool pan_blend_can_fixed_function(const struct pan_blend_equation equation, bool supports_2src);
void pan_blend_to_fixed_function_equation(const struct pan_blend_equation equation,
    struct MALI_BLEND_EQUATION *out);
bool pan_blend_is_opaque(const struct pan_blend_equation equation);
bool pan_blend_alpha_zero_nop(const struct pan_blend_equation eq);
bool pan_blend_alpha_one_store(const struct pan_blend_equation eq);
bool pan_blend_reads_dest(const struct pan_blend_equation equation);
unsigned pan_blend_constant_mask(const struct pan_blend_equation eq);

/* ---- GPU memory arenas ---- */

/* Descriptor memory comes from 1 MiB chunks; a command buffer keeps its
 * chunks until it is freed, then they go back to a shared pool. */
struct s8_chunk {
	s8_mem mem;
	uint64_t used;
	struct s8_chunk *next;
};

static pthread_mutex_t s8_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static struct s8_chunk *s8_pool;

struct s8_arena {
	struct s8_chunk *chunks;        /* current first */
	struct s8_chunk *big;           /* allocations above a chunk */
};

struct s8_ptr {
	void *cpu;
	uint64_t gpu;
};

static struct s8_chunk *s8_chunk_get(void)
{
	pthread_mutex_lock(&s8_pool_lock);
	struct s8_chunk *c = s8_pool;
	if (c != NULL)
		s8_pool = c->next;
	pthread_mutex_unlock(&s8_pool_lock);
	if (c == NULL) {
		c = calloc(1, sizeof(*c));
		if (c == NULL || s8_mem_alloc(&c->mem, S8_CHUNK, 4096) != 0) {
			free(c);
			return NULL;
		}
	}
	c->used = 0;
	c->next = NULL;
	return c;
}

static void s8_arena_release(struct s8_arena *a)
{
	while (a->chunks != NULL) {
		struct s8_chunk *c = a->chunks;
		a->chunks = c->next;
		pthread_mutex_lock(&s8_pool_lock);
		c->next = s8_pool;
		s8_pool = c;
		pthread_mutex_unlock(&s8_pool_lock);
	}
	while (a->big != NULL) {
		struct s8_chunk *c = a->big;
		a->big = c->next;
		s8_mem_free(&c->mem);
		free(c);
	}
}

static struct s8_ptr s8_alloc(struct s8_arena *a, uint64_t size, uint64_t align)
{
	struct s8_ptr p = { NULL, 0 };
	if (align < 16)
		align = 16;
	if (size > S8_CHUNK / 2) {
		struct s8_chunk *c = calloc(1, sizeof(*c));
		if (c == NULL || s8_mem_alloc(&c->mem, size, 4096) != 0) {
			free(c);
			return p;
		}
		c->next = a->big;
		a->big = c;
		p.cpu = c->mem.cpu;
		p.gpu = c->mem.gpu;
		return p;
	}
	struct s8_chunk *c = a->chunks;
	uint64_t at = c ? ALIGN_POT(c->used, align) : 0;
	if (c == NULL || at + size > S8_CHUNK) {
		c = s8_chunk_get();
		if (c == NULL)
			return p;
		c->next = a->chunks;
		a->chunks = c;
		at = 0;
	}
	c->used = at + size;
	p.cpu = (uint8_t *)c->mem.cpu + at;
	p.gpu = c->mem.gpu + at;
	return p;
}

#define s8_alloc_desc(a, T) s8_alloc(a, pan_size(T), pan_alignment(T))
#define s8_alloc_descs(a, n, T) s8_alloc(a, (uint64_t)(n) * pan_size(T), pan_alignment(T))

/* ---- the shader cache (S8SC of air2nir S8SH blobs) ---- */

struct s8_constant {
	uint32_t index, bits;
	char name[48];
};

struct s8_shader {
	char name[64];
	unsigned stage;                 /* 0 vertex, 4 fragment */
	unsigned work_regs, attribute_count, ubo_count, texture_count, sampler_count;
	unsigned nvar;
	struct { uint32_t location, format; } var[64];   /* one per slot up to the highest */
	unsigned ninline;
	uint64_t inline_state[16];
	uint32_t fs_flags;              /* 1 discard, 2 frag coord, 4 depth, 8 reads fb */
	struct mali_renderer_state_packed rsd;
	unsigned ptr_word;
	unsigned nconst;
	struct s8_constant *consts;
	uint32_t rt_regfmt[8];
	uint32_t data_size;
	uint64_t data_gpu;              /* UBO 28 */
	uint32_t tls_size;
	uint32_t writes_psiz;
	uint64_t code_gpu;
	uint32_t code_size;
	int bad;                        /* failed to parse or upload */
};

static struct {
	pthread_mutex_t lock;
	int state;                      /* 0 not tried, 1 ready, -1 unusable */
	uint8_t *file;
	size_t size;
	unsigned count;
	struct s8_shader *shaders;      /* parsed on first use */
	uint8_t *parsed;
	s8_mem tiler_heap;
	s8_mem tls;
	uint32_t tls_per_thread;        /* the size tls was made for */
	s8_mem statics;                 /* sample positions, null texture */
	uint64_t null_texture_desc;     /* the 1x1 texture descriptor */
	uint64_t sample_positions;
	struct s8_arena pipelines;      /* per-pipeline constant memory */
	unsigned core_id_range, threads_per_core;
	uint32_t tiler_features;
} s8g = { .lock = PTHREAD_MUTEX_INITIALIZER };

static uint32_t rd32(const uint8_t *p)
{
	uint32_t v;
	memcpy(&v, p, 4);
	return v;
}

static int s8_upload(s8_mem *m, const void *data, uint64_t size, uint64_t align)
{
	if (s8_mem_alloc(m, size ? size : 1, align) != 0)
		return -1;
	if (size)
		memcpy(m->cpu, data, size);
	return 0;
}

static uint32_t s8_next32(const uint8_t **p, const uint8_t *end, int *short_read)
{
	if (end - *p < 4) {
		*short_read = 1;
		return 0;
	}
	*p += 4;
	return rd32(*p - 4);
}

/* S8SH v2, see air2nir.cpp. */
static int s8_parse_shader(struct s8_shader *s, const uint8_t *b, uint32_t size)
{
	const uint8_t *p = b, *end = b + size;
	int short_read = 0;
#define NEED(n) do { if ((size_t)(end - p) < (size_t)(n)) return -1; } while (0)
#define U32() s8_next32(&p, end, &short_read)
	if (U32() != 0x48533853 || U32() != 2)
		return -1;
	s->stage = U32();
	s->code_size = U32();
	s->work_regs = U32();
	s->attribute_count = U32();
	s->ubo_count = U32();
	s->texture_count = U32();
	s->sampler_count = U32();
	s->nvar = U32();
	if (s->nvar > 64)
		return -1;
	for (unsigned i = 0; i < s->nvar; i++) {
		s->var[i].location = U32();
		s->var[i].format = U32();
	}
	s->ninline = U32();
	if (s->ninline > 16)
		return -1;
	for (unsigned i = 0; i < s->ninline; i++) {
		uint64_t lo = U32();
		s->inline_state[i] = lo | (uint64_t)U32() << 32;
	}
	s->fs_flags = U32();
	unsigned nw = U32();
	if (nw * 4 != sizeof(s->rsd))
		return -1;
	for (unsigned i = 0; i < nw; i++)
		s->rsd.opaque[i] = U32();
	s->ptr_word = U32();
	s->nconst = U32();
	if (s->nconst > 256)
		return -1;
	s->consts = calloc(s->nconst ? s->nconst : 1, sizeof(*s->consts));
	for (unsigned i = 0; i < s->nconst; i++) {
		s->consts[i].index = U32();
		s->consts[i].bits = U32();
		NEED(48);
		memcpy(s->consts[i].name, p, 48);
		s->consts[i].name[47] = 0;
		p += 48;
	}
	for (unsigned i = 0; i < 8; i++)
		s->rt_regfmt[i] = U32();
	s->data_size = U32();
	NEED(s->data_size);
	const uint8_t *data = p;
	p += s->data_size;
	s->tls_size = U32();
	s->writes_psiz = U32();
	if (short_read)
		return -1;
	NEED(s->code_size);
	s8_mem code, cdata;
	/* Bifrost fetches instructions in 128-byte clauses: pad the end. */
	if (s8_mem_alloc(&code, s->code_size + 256, 128) != 0)
		return -1;
	memcpy(code.cpu, p, s->code_size);
	s->code_gpu = code.gpu;
	if (s->data_size) {
		if (s8_upload(&cdata, data, s->data_size, 16) != 0)
			return -1;
		s->data_gpu = cdata.gpu;
	}
#undef U32
#undef NEED
	/* The RSD template carries shader pointer 0: patch in the code. */
	s->rsd.opaque[s->ptr_word] |= (uint32_t)s->code_gpu;
	s->rsd.opaque[s->ptr_word + 1] |= (uint32_t)(s->code_gpu >> 32);
	return 0;
}

static struct s8_shader *s8_shader(const char *name)
{
	if (name == NULL)
		return NULL;
	for (unsigned i = 0; i < s8g.count; i++) {
		const uint8_t *e = s8g.file + 12 + i * 72;
		if (strncmp((const char *)e, name, 64) != 0)
			continue;
		struct s8_shader *s = &s8g.shaders[i];
		if (!s8g.parsed[i]) {
			s8g.parsed[i] = 1;
			uint32_t off = rd32(e + 64), size = rd32(e + 68);
			memcpy(s->name, e, 63);
			if (off > s8g.size || size > s8g.size - off || s8_parse_shader(s, s8g.file + off, size) != 0) {
				s->bad = 1;
				s8_log("MTL shader %s: bad cache entry", name);
			}
		}
		return s->bad ? NULL : s;
	}
	s8_log("MTL shader %s: not in the cache", name);
	return NULL;
}

/* ---- device ---- */

int s8d_init(void)
{
	pthread_mutex_lock(&s8g.lock);
	if (s8g.state != 0) {
		int r = s8g.state > 0 ? 0 : -1;
		pthread_mutex_unlock(&s8g.lock);
		return r;
	}
	s8g.state = -1;
	struct s8mali_info info = { 0 };
	if (s8_fd() < 0 || ioctl(s8_fd(), S8MALI_INFO, &info) != 0 || !info.ready) {
		s8_log("MTL draw: no GPU (/dev/s8mali)");
		goto out;
	}
	const char *path = S8_CACHE_PATH;
#ifdef S8_TEST
	path = getenv("S8_TEST_CACHE");         /* test/draw-test.c */
#endif
	int fd = open(path, O_RDONLY);
	struct stat st;
	if (fd < 0 || fstat(fd, &st) != 0 || st.st_size < 12) {
		s8_log("MTL draw: no shader cache %s", path);
		if (fd >= 0)
			close(fd);
		goto out;
	}
	s8g.size = (size_t)st.st_size;
	s8g.file = malloc(s8g.size);
	size_t got = 0;
	while (s8g.file && got < s8g.size) {
		ssize_t n = read(fd, s8g.file + got, s8g.size - got);
		if (n <= 0)
			break;
		got += (size_t)n;
	}
	close(fd);
	if (got != s8g.size || memcmp(s8g.file, "S8SC", 4) != 0 || rd32(s8g.file + 4) != 1) {
		s8_log("MTL draw: bad shader cache");
		goto out;
	}
	s8g.count = rd32(s8g.file + 8);
	if (12 + (uint64_t)s8g.count * 72 > s8g.size)
		goto out;
	s8g.shaders = calloc(s8g.count, sizeof(*s8g.shaders));
	s8g.parsed = calloc(s8g.count, 1);
	if (s8_mem_alloc(&s8g.tiler_heap, S8_TILER_HEAP, 4096) != 0 ||
	    s8_mem_alloc(&s8g.statics, 4096, 4096) != 0) {
		s8_log("MTL draw: no GPU memory for the tiler heap");
		goto out;
	}
	/* Single-sampled positions as pan_samples.c (mali-blit-gen.c): position
	 * 0 and the origin at 128,128. */
	uint32_t centre = 128u | (128u << 16);
	uint8_t *st8 = s8g.statics.cpu;
	memcpy(st8, &centre, 4);
	memcpy(st8 + 32 * 4, &centre, 4);
	s8g.sample_positions = s8g.statics.gpu;
	/* Null texture: 1x1 transparent black RGBA8 at +0x400, its surface at
	 * +0x440, its descriptor at +0x480. */
	memset(st8 + 0x400, 0, 64);
	memset(st8 + 0x500, 0, 64);     /* unbound UBOs */
	pan_cast_and_pack(st8 + 0x440, SURFACE_WITH_STRIDE, cfg) {
		cfg.pointer = s8g.statics.gpu + 0x400;
		cfg.row_stride = 64;
	}
	pan_cast_and_pack(st8 + 0x480, TEXTURE, cfg) {
		cfg.dimension = MALI_TEXTURE_DIMENSION_2D;
		cfg.format = GENX(pan_format_from_pipe_format)(PIPE_FORMAT_R8G8B8A8_UNORM)->hw;
		cfg.width = 1;
		cfg.height = 1;
		cfg.swizzle = pan_get_default_swizzle(4);
		cfg.texel_ordering = MALI_TEXTURE_LAYOUT_LINEAR;
		cfg.levels = 1;
		cfg.surfaces = s8g.statics.gpu + 0x440;
		cfg.array_size = 1;
		cfg.depth = 1;
	}
	s8g.null_texture_desc = s8g.statics.gpu + 0x480;
	s8g.tiler_features = info.tiler_features;
	s8g.core_id_range = util_last_bit(info.shader_present);
	s8g.threads_per_core = 256;     /* G71: THREAD_MAX_THREADS */
	s8g.state = 1;
	s8_log("MTL draw: %u shaders in the cache, tiler heap %u MiB, %u cores",
	    s8g.count, S8_TILER_HEAP >> 20, util_bitcount(info.shader_present));
out:
	pthread_mutex_unlock(&s8g.lock);
	return s8g.state > 0 ? 0 : -1;
}

/* Thread local storage for a pass: one stack per thread on every core. */
static uint64_t s8_tls(uint32_t per_thread)
{
	if (per_thread == 0)
		return 0;
	pthread_mutex_lock(&s8g.lock);
	if (per_thread > s8g.tls_per_thread) {
		unsigned size = pan_get_total_stack_size(per_thread, s8g.threads_per_core, s8g.core_id_range);
		s8_mem_free(&s8g.tls);
		if (s8_mem_alloc(&s8g.tls, size, 4096) == 0) {
			s8g.tls_per_thread = per_thread;
		} else {
			s8g.tls_per_thread = 0;
			s8_log("MTL draw: no %u KiB for thread storage", size >> 10);
		}
	}
	uint64_t r = s8g.tls_per_thread ? s8g.tls.gpu : 0;
	pthread_mutex_unlock(&s8g.lock);
	return r;
}

/* ---- formats ---- */

static enum pipe_format s8_pixel_format(uint32_t mtl)
{
	switch (mtl) {
	case 1: return PIPE_FORMAT_A8_UNORM;
	case 10: return PIPE_FORMAT_R8_UNORM;
	case 11: return PIPE_FORMAT_R8_SRGB;
	case 25: return PIPE_FORMAT_R16_FLOAT;
	case 30: return PIPE_FORMAT_R8G8_UNORM;
	case 55: return PIPE_FORMAT_R32_FLOAT;
	case 65: return PIPE_FORMAT_R16G16_FLOAT;
	case 70: return PIPE_FORMAT_R8G8B8A8_UNORM;
	case 71: return PIPE_FORMAT_R8G8B8A8_SRGB;
	case 80: return PIPE_FORMAT_B8G8R8A8_UNORM;
	case 81: return PIPE_FORMAT_B8G8R8A8_SRGB;
	case 90: return PIPE_FORMAT_R10G10B10A2_UNORM;
	case 94: return PIPE_FORMAT_B10G10R10A2_UNORM;
	case 105: return PIPE_FORMAT_R32G32_FLOAT;
	case 115: return PIPE_FORMAT_R16G16B16A16_FLOAT;
	case 125: return PIPE_FORMAT_R32G32B32A32_FLOAT;
	default: return PIPE_FORMAT_NONE;
	}
}

static enum pipe_format s8_vertex_format(uint32_t f)
{
	switch (f) {
	case 1: return PIPE_FORMAT_R8G8_UINT;
	case 3: return PIPE_FORMAT_R8G8B8A8_UINT;
	case 7: return PIPE_FORMAT_R8G8_UNORM;
	case 8: return PIPE_FORMAT_R8G8B8_UNORM;
	case 9: return PIPE_FORMAT_R8G8B8A8_UNORM;
	case 10: return PIPE_FORMAT_R8G8_SNORM;
	case 12: return PIPE_FORMAT_R8G8B8A8_SNORM;
	case 13: return PIPE_FORMAT_R16G16_UINT;
	case 15: return PIPE_FORMAT_R16G16B16A16_UINT;
	case 16: return PIPE_FORMAT_R16G16_SINT;
	case 18: return PIPE_FORMAT_R16G16B16A16_SINT;
	case 19: return PIPE_FORMAT_R16G16_UNORM;
	case 21: return PIPE_FORMAT_R16G16B16A16_UNORM;
	case 22: return PIPE_FORMAT_R16G16_SNORM;
	case 24: return PIPE_FORMAT_R16G16B16A16_SNORM;
	case 25: return PIPE_FORMAT_R16G16_FLOAT;
	case 26: return PIPE_FORMAT_R16G16B16_FLOAT;
	case 27: return PIPE_FORMAT_R16G16B16A16_FLOAT;
	case 28: return PIPE_FORMAT_R32_FLOAT;
	case 29: return PIPE_FORMAT_R32G32_FLOAT;
	case 30: return PIPE_FORMAT_R32G32B32_FLOAT;
	case 31: return PIPE_FORMAT_R32G32B32A32_FLOAT;
	case 32: return PIPE_FORMAT_R32_SINT;
	case 33: return PIPE_FORMAT_R32G32_SINT;
	case 34: return PIPE_FORMAT_R32G32B32_SINT;
	case 35: return PIPE_FORMAT_R32G32B32A32_SINT;
	case 36: return PIPE_FORMAT_R32_UINT;
	case 37: return PIPE_FORMAT_R32G32_UINT;
	case 38: return PIPE_FORMAT_R32G32B32_UINT;
	case 39: return PIPE_FORMAT_R32G32B32A32_UINT;
	case 42: return PIPE_FORMAT_B8G8R8A8_UNORM;
	case 45: return PIPE_FORMAT_R8_UINT;
	case 47: return PIPE_FORMAT_R8_UNORM;
	case 49: return PIPE_FORMAT_R16_UINT;
	case 51: return PIPE_FORMAT_R16_UNORM;
	case 53: return PIPE_FORMAT_R16_FLOAT;
	default: return PIPE_FORMAT_NONE;
	}
}

static enum pipe_blendfactor s8_blend_factor(unsigned f, bool *ok)
{
	switch (f) {
	case 0: return PIPE_BLENDFACTOR_ZERO;
	case 1: return PIPE_BLENDFACTOR_ONE;
	case 2: return PIPE_BLENDFACTOR_SRC_COLOR;
	case 3: return PIPE_BLENDFACTOR_INV_SRC_COLOR;
	case 4: return PIPE_BLENDFACTOR_SRC_ALPHA;
	case 5: return PIPE_BLENDFACTOR_INV_SRC_ALPHA;
	case 6: return PIPE_BLENDFACTOR_DST_COLOR;
	case 7: return PIPE_BLENDFACTOR_INV_DST_COLOR;
	case 8: return PIPE_BLENDFACTOR_DST_ALPHA;
	case 9: return PIPE_BLENDFACTOR_INV_DST_ALPHA;
	case 10: return PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE;
	case 11: return PIPE_BLENDFACTOR_CONST_COLOR;
	case 12: return PIPE_BLENDFACTOR_INV_CONST_COLOR;
	case 13: return PIPE_BLENDFACTOR_CONST_ALPHA;
	case 14: return PIPE_BLENDFACTOR_INV_CONST_ALPHA;
	default: *ok = false; return PIPE_BLENDFACTOR_ONE;
	}
}

static enum pipe_blend_func s8_blend_op(unsigned op)
{
	switch (op) {
	case 1: return PIPE_BLEND_SUBTRACT;
	case 2: return PIPE_BLEND_REVERSE_SUBTRACT;
	case 3: return PIPE_BLEND_MIN;
	case 4: return PIPE_BLEND_MAX;
	default: return PIPE_BLEND_ADD;
	}
}

/* Render target writeback/internal formats and swizzle (pan_desc.c
 * get_rt_formats). */
static bool s8_rt_formats(enum pipe_format pf, uint32_t *writeback, uint32_t *internal, uint32_t *swizzle)
{
	const struct util_format_description *desc = util_format_description(pf);
	const struct pan_blendable_format *bf = GENX(pan_blendable_format_from_pipe_format)(pf);
	if (desc == NULL || bf == NULL || !bf->internal)
		return false;
	unsigned char sw[4] = { PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W };
	*internal = bf->internal;
	*writeback = bf->writeback;
	pan_invert_swizzle(desc->swizzle, sw);
	*swizzle = pan_translate_swizzle_4(sw);
	return true;
}

/* Upper word of the internal blend descriptor the fragment shader's
 * framebuffer fetch converts with (pan_blend_get_internal_desc). */
static uint32_t s8_rt_conversion(enum pipe_format pf, unsigned size)
{
	const struct util_format_description *desc = util_format_description(pf);
	struct mali_internal_blend_packed res;
	pan_pack(&res, INTERNAL_BLEND, cfg) {
		cfg.mode = MALI_BLEND_MODE_OPAQUE;
		cfg.fixed_function.num_comps = desc->nr_channels;
		cfg.fixed_function.rt = 0;
		cfg.fixed_function.conversion.register_format =
		    size == 16 ? MALI_REGISTER_FILE_FORMAT_F16 : MALI_REGISTER_FILE_FORMAT_F32;
		cfg.fixed_function.conversion.memory_format =
		    GENX(pan_dithered_format_from_pipe_format)(pf, false);
	}
	return res.opaque[1];
}

/* ---- pipelines ---- */

struct s8_varying_link {
	uint64_t vs_attribs, fs_attribs;        /* ATTRIBUTE arrays */
	uint32_t stride[S8_VARY_COUNT];
};

struct s8d_pipeline {
	unsigned id;
	struct s8_shader *vs, *fs;
	enum pipe_format color;
	uint64_t vs_fc, fs_fc;                  /* UBO 29 contents */
	uint32_t vs_fc_size, fs_fc_size;
	uint64_t vs_rsd;
	struct mali_renderer_state_packed fs_rsd;
	struct mali_blend_packed blend;         /* blend constant patched per draw */
	bool blend_uses_constant;
	unsigned constant_mask;
	struct s8_varying_link link;
	struct s8d_vertex_attrib attribs[S8D_MAX_ATTRIBS];
	struct s8d_vertex_layout layouts[S8D_MAX_BUFFERS];
	unsigned nattribs;
	uint32_t rt_conv[2];                    /* 32-bit, 16-bit */
	uint32_t tls_size;
	bool writes_psiz;
};

/* Function constants for UBO 29: each constant at index * 16, value then
 * a "defined" word at +8. Values come from "name=0xbits;..." keys. */
static int s8_fc_ubo(const struct s8_shader *s, const char *key, uint64_t *gpu, uint32_t *size)
{
	unsigned top = 0;
	for (unsigned i = 0; i < s->nconst; i++)
		top = MAX2(top, s->consts[i].index + 1);
	*size = MAX2(top, 1) * 16;
	struct s8_ptr p = s8_alloc(&s8g.pipelines, *size, 16);
	if (p.cpu == NULL)
		return -1;
	uint8_t buf[4096];
	if (*size > sizeof(buf))
		return -1;
	memset(buf, 0, *size);
	for (unsigned i = 0; i < s->nconst; i++) {
		const char *name = s->consts[i].name;
		size_t nl = strlen(name);
		const char *k = key;
		while (k != NULL && *k) {
			const char *eq = strchr(k, '=');
			const char *semi = strchr(k, ';');
			if (eq == NULL)
				break;
			if ((size_t)(eq - k) == nl && strncmp(k, name, nl) == 0) {
				uint64_t v = strtoull(eq + 1, NULL, 0);
				uint32_t one = 1;
				memcpy(buf + s->consts[i].index * 16, &v, 8);
				memcpy(buf + s->consts[i].index * 16 + 8, &one, 4);
				break;
			}
			k = semi ? semi + 1 : NULL;
		}
	}
	memcpy(p.cpu, buf, *size);
	*gpu = p.gpu;
	return 0;
}

static mali_pixel_format s8_varying_hw(gl_varying_slot loc, enum pipe_format pf)
{
	switch (loc) {
	case VARYING_SLOT_PNTC:
	case VARYING_SLOT_PSIZ:
		return (MALI_R16F << 12) | pan_get_default_swizzle(1);
	case VARYING_SLOT_POS:
		return (MALI_SNAP_4 << 12) | pan_get_default_swizzle(4);
	default:
		if (pf == PIPE_FORMAT_NONE)
			return (MALI_CONSTANT << 12) | PAN_V6_SWIZZLE(0, 0, 0, 0);
		return GENX(pan_format_from_pipe_format)(pf)->hw;
	}
}

static unsigned s8_varying_buf(gl_varying_slot loc)
{
	return loc == VARYING_SLOT_POS ? S8_VARY_POSITION : loc == VARYING_SLOT_PSIZ ? S8_VARY_PSIZ : S8_VARY_GENERAL;
}

/* PanVK link_shaders: the vertex outputs the fragment shader reads go to
 * the general buffer in the fragment shader's formats; the rest are sunk. */
static int s8_link(struct s8d_pipeline *p)
{
	enum pipe_format out[VARYING_SLOT_MAX] = { 0 }, in[VARYING_SLOT_MAX] = { 0 };
	unsigned offset[VARYING_SLOT_MAX] = { 0 };
	const struct s8_shader *vs = p->vs, *fs = p->fs;
	for (unsigned i = 0; i < vs->nvar; i++)
		if (vs->var[i].location < VARYING_SLOT_MAX && vs->var[i].format)
			out[vs->var[i].location] = vs->var[i].format;
	for (unsigned i = 0; fs && i < fs->nvar; i++)
		if (fs->var[i].location < VARYING_SLOT_MAX && fs->var[i].format)
			in[fs->var[i].location] = fs->var[i].format;
	memset(p->link.stride, 0, sizeof(p->link.stride));
	if (out[VARYING_SLOT_POS])
		p->link.stride[S8_VARY_POSITION] = 16;
	if (out[VARYING_SLOT_PSIZ])
		p->link.stride[S8_VARY_PSIZ] = 2;
	for (unsigned loc = 0; loc < VARYING_SLOT_MAX; loc++) {
		if (loc == VARYING_SLOT_POS || loc == VARYING_SLOT_PSIZ)
			continue;
		if (!out[loc] || !in[loc]) {
			out[loc] = in[loc] = PIPE_FORMAT_NONE;
			continue;
		}
		out[loc] = in[loc];
		offset[loc] = p->link.stride[S8_VARY_GENERAL];
		p->link.stride[S8_VARY_GENERAL] += ALIGN_POT(util_format_get_blocksize(in[loc]), 4);
	}
	for (int which = 0; which < 2; which++) {
		const struct s8_shader *s = which ? fs : vs;
		if (s == NULL)
			continue;
		const enum pipe_format *fmts = which ? in : out;
		struct s8_ptr a = s8_alloc_descs(&s8g.pipelines, MAX2(s->nvar, 1), ATTRIBUTE);
		if (a.cpu == NULL)
			return -1;
		struct mali_attribute_packed *attrs = a.cpu;
		for (unsigned i = 0; i < s->nvar; i++) {
			/* entries are indexed by location; unused ones are (0, NONE) */
			gl_varying_slot loc = s->var[i].location;
			enum pipe_format pf = loc < VARYING_SLOT_MAX ? fmts[loc] : PIPE_FORMAT_NONE;
			bool special = s->var[i].format != PIPE_FORMAT_NONE &&
			    (loc == VARYING_SLOT_POS || loc == VARYING_SLOT_PSIZ);
			if (s->var[i].format == PIPE_FORMAT_NONE)
				pf = PIPE_FORMAT_NONE;
			pan_pack(&attrs[i], ATTRIBUTE, cfg) {
				if (!special && pf == PIPE_FORMAT_NONE) {
					cfg.format = (MALI_CONSTANT << 12) | PAN_V6_SWIZZLE(0, 0, 0, 0);
				} else {
					cfg.buffer_index = s8_varying_buf(loc);
					cfg.offset = special ? 0 : offset[loc];
					cfg.format = s8_varying_hw(loc, pf);
				}
				cfg.offset_enable = false;
			}
		}
		if (which)
			p->link.fs_attribs = a.gpu;
		else
			p->link.vs_attribs = a.gpu;
	}
	return 0;
}

struct s8d_pipeline *s8d_pipeline_new(const struct s8d_pipeline_desc *d, unsigned id)
{
	if (s8d_init() != 0)
		return NULL;
	pthread_mutex_lock(&s8g.lock);
	struct s8d_pipeline *p = calloc(1, sizeof(*p));
	const char *why = NULL;
	p->id = id;
	p->vs = s8_shader(d->vs_name);
	p->fs = d->fs_name ? s8_shader(d->fs_name) : NULL;
	p->color = s8_pixel_format(d->color_format);
	if (p->vs == NULL || (d->fs_name != NULL && p->fs == NULL)) {
		why = "shader missing";
		goto fail;
	}
	if (p->color == PIPE_FORMAT_NONE && d->color_format != 0) {
		why = "colour format";
		goto fail;
	}
	if (s8_fc_ubo(p->vs, d->vs_constants, &p->vs_fc, &p->vs_fc_size) != 0 ||
	    (p->fs && s8_fc_ubo(p->fs, d->fs_constants, &p->fs_fc, &p->fs_fc_size) != 0) ||
	    s8_link(p) != 0) {
		why = "memory";
		goto fail;
	}
	struct s8_ptr vrsd = s8_alloc_desc(&s8g.pipelines, RENDERER_STATE);
	if (vrsd.cpu == NULL) {
		why = "memory";
		goto fail;
	}
	memcpy(vrsd.cpu, &p->vs->rsd, sizeof(p->vs->rsd));
	p->vs_rsd = vrsd.gpu;
	p->writes_psiz = p->vs->writes_psiz;
	p->tls_size = MAX2(p->vs->tls_size, p->fs ? p->fs->tls_size : 0);

	/* Fragment renderer state: the shader's template plus fixed state as
	 * mali-blit-gen.c (no depth/stencil, single sample). */
	struct pan_blend_equation eq = { 0 };
	bool ok = true;
	eq.blend_enable = d->blending;
	eq.rgb_func = s8_blend_op(d->rgb_op);
	eq.alpha_func = s8_blend_op(d->alpha_op);
	eq.rgb_src_factor = s8_blend_factor(d->src_rgb, &ok);
	eq.rgb_dst_factor = s8_blend_factor(d->dst_rgb, &ok);
	eq.alpha_src_factor = s8_blend_factor(d->src_alpha, &ok);
	eq.alpha_dst_factor = s8_blend_factor(d->dst_alpha, &ok);
	/* MTLColorWriteMask: red 8, green 4, blue 2, alpha 1. */
	eq.color_mask = ((d->write_mask & 8) ? 1 : 0) | ((d->write_mask & 4) ? 2 : 0) |
	    ((d->write_mask & 2) ? 4 : 0) | ((d->write_mask & 1) ? 8 : 0);
	if (!ok || !pan_blend_can_fixed_function(eq, false)) {
		why = "blend equation";
		goto fail;
	}
	bool opaque = pan_blend_is_opaque(eq) && eq.color_mask == 0xf;
	p->constant_mask = pan_blend_constant_mask(eq);
	p->blend_uses_constant = p->constant_mask != 0;
	if (p->fs != NULL) {
		pan_unpack(&p->fs->rsd, RENDERER_STATE, rs);
		pan_pack(&p->fs_rsd, RENDERER_STATE, cfg) {
			cfg = rs;
			cfg.multisample_misc.sample_mask = 0xFFFF;
			cfg.multisample_misc.depth_function = MALI_FUNC_ALWAYS;
			cfg.multisample_misc.shader_depth_range_fixed = true;
			cfg.multisample_misc.fixed_function_depth_range_fixed = true;
			cfg.stencil_mask_misc.stencil_mask_front = 0xFF;
			cfg.stencil_mask_misc.stencil_mask_back = 0xFF;
			cfg.stencil_mask_misc.alpha_test_compare_function = MALI_FUNC_ALWAYS;
			cfg.stencil_front.compare_function = MALI_FUNC_ALWAYS;
			cfg.stencil_front.stencil_fail = MALI_STENCIL_OP_KEEP;
			cfg.stencil_front.depth_fail = MALI_STENCIL_OP_KEEP;
			cfg.stencil_front.depth_pass = MALI_STENCIL_OP_KEEP;
			cfg.stencil_front.mask = 0xFF;
			cfg.stencil_back = cfg.stencil_front;
			bool side = (p->fs->fs_flags & (1 | 4 | 8)) != 0;
			cfg.properties.zs_update_operation = MALI_PIXEL_KILL_FORCE_EARLY;
			cfg.properties.pixel_kill_operation = side ? MALI_PIXEL_KILL_WEAK_EARLY : MALI_PIXEL_KILL_FORCE_EARLY;
			cfg.properties.allow_forward_pixel_to_kill = opaque && !side;
		}
	}
	pan_pack(&p->blend, BLEND, cfg) {
		cfg.round_to_fb_precision = true;
		cfg.srgb = util_format_is_srgb(p->color);
		cfg.load_destination = pan_blend_reads_dest(eq);
		pan_blend_to_fixed_function_equation(eq, &cfg.equation);
		cfg.internal.mode = opaque ? MALI_BLEND_MODE_OPAQUE : MALI_BLEND_MODE_FIXED_FUNCTION;
		cfg.internal.fixed_function.num_comps = 4;
		cfg.internal.fixed_function.rt = 0;
		if (p->color != PIPE_FORMAT_NONE)
			cfg.internal.fixed_function.conversion.memory_format =
			    GENX(pan_dithered_format_from_pipe_format)(p->color, false);
		if (!opaque) {
			cfg.internal.fixed_function.alpha_zero_nop = pan_blend_alpha_zero_nop(eq);
			cfg.internal.fixed_function.alpha_one_store = pan_blend_alpha_one_store(eq);
		}
		cfg.internal.fixed_function.conversion.register_format =
		    p->fs && p->fs->rt_regfmt[0] ? p->fs->rt_regfmt[0] : MALI_REGISTER_FILE_FORMAT_F32;
	}
	if (p->color != PIPE_FORMAT_NONE) {
		p->rt_conv[0] = s8_rt_conversion(p->color, 32);
		p->rt_conv[1] = s8_rt_conversion(p->color, 16);
	}
	memcpy(p->attribs, d->attribs, sizeof(p->attribs));
	memcpy(p->layouts, d->layouts, sizeof(p->layouts));
	for (unsigned i = 0; i < S8D_MAX_ATTRIBS; i++)
		if (p->attribs[i].format)
			p->nattribs = i + 1;
	pthread_mutex_unlock(&s8g.lock);
	return p;
fail:
	s8_log("MTL pipeline %u: no GPU pipeline (%s: vs %s fs %s fmt %u)", id, why,
	    d->vs_name ? d->vs_name : "-", d->fs_name ? d->fs_name : "-", d->color_format);
	free(p);
	pthread_mutex_unlock(&s8g.lock);
	return NULL;
}

void s8d_pipeline_free(struct s8d_pipeline *p)
{
	/* Its constant memory stays in the pipeline arena (pipelines are made
	 * once per process and live as long as it). */
	free(p);
}

/* ---- command buffers ---- */

struct s8_binding {
	uint64_t gpu;
	const void *cpu;
	uint64_t length;
	bool set;
};

struct s8_stage_state {
	struct s8_binding buffers[S8D_MAX_BUFFERS];
	struct s8d_texture textures[S8D_MAX_TEXTURES];
	bool texture_set[S8D_MAX_TEXTURES];
	struct s8d_sampler samplers[S8D_MAX_SAMPLERS];
	bool sampler_set[S8D_MAX_SAMPLERS];
	/* descriptor tables built for the current bindings (0: rebuild) */
	uint64_t ubos, textures_desc, samplers_desc, addrs;
	struct s8d_pipeline *for_pipeline;
};

struct s8_pass {
	int valid;
	struct s8d_texture target;
	enum pipe_format format;
	unsigned load;
	uint32_t clear[4];
	struct pan_jc jc;
	unsigned draws;
	struct s8_ptr fbd;              /* FRAMEBUFFER + RENDER_TARGET */
	struct s8_ptr tiler_ctx, tiler_heap, tls, fragment, preload_dcd;
	uint32_t tls_size;
	unsigned skipped;
};

struct s8d_cmdbuf {
	struct s8_arena arena;
	struct s8_pass *passes;
	unsigned npasses, cap;
	int current;                    /* pass being encoded, -1 none */
	struct s8d_pipeline *pipeline;
	struct s8_stage_state stage[2];
	double viewport[6];
	bool viewport_set;
	unsigned scissor[4];
	bool scissor_set;
	unsigned cull, winding;
	float blend_color[4];
	uint64_t vp_desc;               /* VIEWPORT for the current state, 0: rebuild */
};

struct s8d_cmdbuf *s8d_cmdbuf_new(void)
{
	struct s8d_cmdbuf *cb = calloc(1, sizeof(*cb));
	if (cb != NULL)
		cb->current = -1;
	return cb;
}

void s8d_cmdbuf_free(struct s8d_cmdbuf *cb)
{
	if (cb == NULL)
		return;
	s8_arena_release(&cb->arena);
	free(cb->passes);
	free(cb);
}

static void s8_reset_state(struct s8d_cmdbuf *cb)
{
	memset(cb->stage, 0, sizeof(cb->stage));
	cb->pipeline = NULL;
	cb->viewport_set = cb->scissor_set = false;
	cb->cull = cb->winding = 0;
	memset(cb->blend_color, 0, sizeof(cb->blend_color));
	cb->vp_desc = 0;
}

static struct s8_pass *s8_cur(struct s8d_cmdbuf *cb)
{
	return cb->current >= 0 && cb->passes[cb->current].valid ? &cb->passes[cb->current] : NULL;
}

int s8d_pass_begin(struct s8d_cmdbuf *cb, const struct s8d_texture *target, unsigned load,
    const double clear[4])
{
	static unsigned logged;
	if (cb == NULL || s8d_init() != 0)
		return -1;
	if (cb->npasses == cb->cap) {
		cb->cap = cb->cap ? cb->cap * 2 : 8;
		cb->passes = realloc(cb->passes, cb->cap * sizeof(*cb->passes));
	}
	int idx = (int)cb->npasses++;
	struct s8_pass *p = &cb->passes[idx];
	memset(p, 0, sizeof(*p));
	cb->current = idx;
	s8_reset_state(cb);
	enum pipe_format pf = target ? s8_pixel_format(target->format) : PIPE_FORMAT_NONE;
	uint32_t wb, internal, sw;
	if (target == NULL || target->gpu == 0 || pf == PIPE_FORMAT_NONE || !s8_rt_formats(pf, &wb, &internal, &sw) ||
	    target->width == 0 || target->height == 0 || target->width > 4096 || target->height > 4096) {
		if (logged++ < 20)
			s8_log("MTL pass skipped: target %ux%u fmt %u gpu 0x%llx", target ? target->width : 0,
			    target ? target->height : 0, target ? target->format : 0,
			    (unsigned long long)(target ? target->gpu : 0));
		return idx;     /* valid = 0: draws are dropped */
	}
	p->target = *target;
	p->format = pf;
	p->load = load;
	if (load == 2 && clear != NULL) {
		union pipe_color_union c;
		for (int i = 0; i < 4; i++)
			c.f[i] = (float)clear[i];
		pan_pack_color(GENX(pan_blendable_formats), p->clear, &c, pf, false);
	}
	p->fbd = s8_alloc(&cb->arena, pan_size(FRAMEBUFFER) + pan_size(RENDER_TARGET), pan_alignment(FRAMEBUFFER));
	p->tiler_ctx = s8_alloc_desc(&cb->arena, TILER_CONTEXT);
	p->tiler_heap = s8_alloc_desc(&cb->arena, TILER_HEAP);
	p->tls = s8_alloc_desc(&cb->arena, LOCAL_STORAGE);
	p->fragment = s8_alloc_desc(&cb->arena, FRAGMENT_JOB);
	if (!p->fbd.cpu || !p->tiler_ctx.cpu || !p->tiler_heap.cpu || !p->tls.cpu || !p->fragment.cpu)
		return idx;
	p->valid = 1;
	return idx;
}

void s8d_pass_end(struct s8d_cmdbuf *cb)
{
	if (cb != NULL)
		cb->current = -1;
}

void s8d_set_pipeline(struct s8d_cmdbuf *cb, struct s8d_pipeline *p)
{
	if (cb->pipeline != p) {
		cb->pipeline = p;
		cb->stage[0].for_pipeline = cb->stage[1].for_pipeline = NULL;
	}
}

void s8d_set_buffer(struct s8d_cmdbuf *cb, unsigned stage, unsigned index, uint64_t gpu,
    const void *cpu, uint64_t length)
{
	if (stage > 1 || index >= S8D_MAX_BUFFERS)
		return;
	struct s8_stage_state *s = &cb->stage[stage];
	s->buffers[index] = (struct s8_binding){ gpu, cpu, length, true };
	s->ubos = s->addrs = 0;
}

void s8d_set_bytes(struct s8d_cmdbuf *cb, unsigned stage, unsigned index, const void *bytes,
    uint64_t length)
{
	if (stage > 1 || index >= S8D_MAX_BUFFERS || length > (64u << 10))
		return;
	struct s8_ptr p = s8_alloc(&cb->arena, length ? length : 16, 64);
	if (p.cpu == NULL)
		return;
	memcpy(p.cpu, bytes, length);
	s8d_set_buffer(cb, stage, index, p.gpu, p.cpu, length);
}

void s8d_set_texture(struct s8d_cmdbuf *cb, unsigned stage, unsigned index, const struct s8d_texture *t)
{
	if (stage > 1 || index >= S8D_MAX_TEXTURES)
		return;
	struct s8_stage_state *s = &cb->stage[stage];
	if (t != NULL)
		s->textures[index] = *t;
	s->texture_set[index] = t != NULL && t->gpu != 0;
	s->textures_desc = 0;
}

void s8d_set_sampler(struct s8d_cmdbuf *cb, unsigned stage, unsigned index, const struct s8d_sampler *smp)
{
	if (stage > 1 || index >= S8D_MAX_SAMPLERS)
		return;
	struct s8_stage_state *s = &cb->stage[stage];
	if (smp != NULL)
		s->samplers[index] = *smp;
	s->sampler_set[index] = smp != NULL;
	s->samplers_desc = 0;
}

void s8d_set_viewport(struct s8d_cmdbuf *cb, double x, double y, double w, double h, double znear,
    double zfar)
{
	cb->viewport[0] = x;
	cb->viewport[1] = y;
	cb->viewport[2] = w;
	cb->viewport[3] = h;
	cb->viewport[4] = znear;
	cb->viewport[5] = zfar;
	cb->viewport_set = true;
	cb->vp_desc = 0;
}

void s8d_set_scissor(struct s8d_cmdbuf *cb, unsigned x, unsigned y, unsigned w, unsigned h)
{
	cb->scissor[0] = x;
	cb->scissor[1] = y;
	cb->scissor[2] = w;
	cb->scissor[3] = h;
	cb->scissor_set = true;
	cb->vp_desc = 0;
}

void s8d_set_cull(struct s8d_cmdbuf *cb, unsigned cull_mode, unsigned winding)
{
	cb->cull = cull_mode;
	cb->winding = winding;
}

void s8d_set_blend_color(struct s8d_cmdbuf *cb, float r, float g, float b, float a)
{
	cb->blend_color[0] = r;
	cb->blend_color[1] = g;
	cb->blend_color[2] = b;
	cb->blend_color[3] = a;
}

/* ---- per-draw descriptors ---- */

static enum mali_wrap_mode s8_wrap(unsigned m)
{
	switch (m) {
	case 2: return MALI_WRAP_MODE_REPEAT;
	case 3: return MALI_WRAP_MODE_MIRRORED_REPEAT;
	case 4:
	case 5: return MALI_WRAP_MODE_CLAMP_TO_BORDER;
	case 1: return MALI_WRAP_MODE_MIRRORED_CLAMP_TO_EDGE;
	default: return MALI_WRAP_MODE_CLAMP_TO_EDGE;
	}
}

static void s8_pack_sampler(void *out, const struct s8d_sampler *s)
{
	pan_pack((struct mali_sampler_packed *)out, SAMPLER, cfg) {
		cfg.seamless_cube_map = false;
		cfg.normalized_coordinates = s->normalized;
		cfg.minify_nearest = !s->min_linear;
		cfg.magnify_nearest = !s->mag_linear;
		cfg.mipmap_mode = s->mip == 2 ? MALI_MIPMAP_MODE_TRILINEAR : MALI_MIPMAP_MODE_NEAREST;
		cfg.wrap_mode_s = s8_wrap(s->address[0]);
		cfg.wrap_mode_t = s8_wrap(s->address[1]);
		cfg.wrap_mode_r = s8_wrap(s->address[2]);
		/* only level 0 is stored */
		cfg.minimum_lod = 0;
		cfg.maximum_lod = 0;
		cfg.compare_function = MALI_FUNC_NEVER;
		/* border colour 0 = transparent black (MTLSamplerBorderColor default) */
	}
}

/* Metal constexpr sampler bits as AIR stores them: address s/t/r in bits
 * 0-8 (3 each: 0 clamp_to_zero, 1 clamp_to_edge, 2 repeat, 3 mirrored),
 * mag/min/mip linear in bits 9/11/13, coord::pixel in bit 15 (assumed). */
static void s8_inline_sampler(struct s8d_sampler *out, uint64_t st)
{
	static const uint8_t addr[8] = { 4, 0, 2, 3, 0, 0, 0, 0 };
	memset(out, 0, sizeof(*out));
	for (int i = 0; i < 3; i++)
		out->address[i] = addr[(st >> (3 * i)) & 7];
	out->mag_linear = (st >> 9) & 1;
	out->min_linear = (st >> 11) & 1;
	out->mip = ((st >> 13) & 1) ? 2 : 0;
	out->normalized = !((st >> 15) & 1);
}

static void s8_pack_texture(void *desc, void *surface, uint64_t surface_gpu, const struct s8d_texture *t)
{
	enum pipe_format pf = s8_pixel_format(t->format);
	pan_cast_and_pack(surface, SURFACE_WITH_STRIDE, cfg) {
		cfg.pointer = t->gpu;
		cfg.row_stride = t->row_bytes;
		cfg.surface_stride = 0;
	}
	pan_pack((struct mali_texture_packed *)desc, TEXTURE, cfg) {
		cfg.dimension = MALI_TEXTURE_DIMENSION_2D;
		cfg.format = GENX(pan_format_from_pipe_format)(pf)->hw;
		cfg.width = t->width;
		cfg.height = t->height;
		cfg.swizzle = pan_get_default_swizzle(4);
		cfg.texel_ordering = MALI_TEXTURE_LAYOUT_LINEAR;
		cfg.levels = 1;
		cfg.surfaces = surface_gpu;
		cfg.array_size = 1;
		cfg.depth = 1;
	}
}

/* Texture table: 32 descriptors, unbound ones are the null texture. */
static uint64_t s8_texture_table(struct s8d_cmdbuf *cb, struct s8_stage_state *s, const struct s8_shader *sh)
{
	/* (the compiler's texture_count stays 0: always bind the full table) */
	(void)sh;
	if (s->textures_desc)
		return s->textures_desc;
	unsigned n = S8_TEX_COUNT;
	struct s8_ptr d = s8_alloc_descs(&cb->arena, n, TEXTURE);
	struct s8_ptr surf = s8_alloc_descs(&cb->arena, n, SURFACE_WITH_STRIDE);
	if (!d.cpu || !surf.cpu)
		return 0;
	for (unsigned i = 0; i < n; i++) {
		void *desc = (uint8_t *)d.cpu + i * pan_size(TEXTURE);
		if (i < S8D_MAX_TEXTURES && s->texture_set[i] && s8_pixel_format(s->textures[i].format) != PIPE_FORMAT_NONE)
			s8_pack_texture(desc, (uint8_t *)surf.cpu + i * pan_size(SURFACE_WITH_STRIDE),
			    surf.gpu + i * pan_size(SURFACE_WITH_STRIDE), &s->textures[i]);
		else
			memcpy(desc, (uint8_t *)s8g.statics.cpu + 0x480, pan_size(TEXTURE));
	}
	s->textures_desc = d.gpu;
	return d.gpu;
}

static uint64_t s8_sampler_table(struct s8d_cmdbuf *cb, struct s8_stage_state *s, const struct s8_shader *sh)
{
	if (s->samplers_desc && s->for_pipeline == cb->pipeline)
		return s->samplers_desc;
	struct s8_ptr d = s8_alloc_descs(&cb->arena, S8_SAMPLER_COUNT, SAMPLER);
	if (!d.cpu)
		return 0;
	for (unsigned i = 0; i < S8_SAMPLER_COUNT; i++) {
		struct s8d_sampler smp = { .normalized = 1 };
		if (i < S8D_MAX_SAMPLERS && s->sampler_set[i])
			smp = s->samplers[i];
		else if (i >= S8_INLINE_SAMPLER_BASE && i - S8_INLINE_SAMPLER_BASE < sh->ninline)
			s8_inline_sampler(&smp, sh->inline_state[i - S8_INLINE_SAMPLER_BASE]);
		s8_pack_sampler((uint8_t *)d.cpu + i * pan_size(SAMPLER), &smp);
	}
	s->samplers_desc = d.gpu;
	return d.gpu;
}

/* UBO table: Metal buffers 0..27, the shader's tables, function constants,
 * device buffer addresses and the system values. */
static uint64_t s8_ubo_table(struct s8d_cmdbuf *cb, struct s8_stage_state *s, const struct s8_shader *sh,
    uint64_t fc, uint32_t fc_size, uint64_t sysvals)
{
	struct s8_ptr d = s8_alloc_descs(&cb->arena, S8_UBO_COUNT, UNIFORM_BUFFER);
	if (!d.cpu)
		return 0;
	if (!s->addrs) {
		struct s8_ptr a = s8_alloc(&cb->arena, S8D_MAX_BUFFERS * 8, 16);
		if (!a.cpu)
			return 0;
		uint64_t *addr = a.cpu;
		for (unsigned i = 0; i < S8D_MAX_BUFFERS; i++)
			addr[i] = s->buffers[i].set ? s->buffers[i].gpu : 0;
		s->addrs = a.gpu;
	}
	struct mali_uniform_buffer_packed *ubo = d.cpu;
	for (unsigned i = 0; i < S8_UBO_COUNT; i++) {
		uint64_t ptr = 0, size = 0;
		if (i < S8_UBO_DATA && i < S8D_MAX_BUFFERS && s->buffers[i].set && s->buffers[i].gpu) {
			ptr = s->buffers[i].gpu;
			size = s->buffers[i].length;
		} else if (i == S8_UBO_DATA) {
			ptr = sh->data_gpu;
			size = sh->data_size;
		} else if (i == S8_UBO_FC) {
			ptr = fc;
			size = fc_size;
		} else if (i == S8_UBO_ADDRS) {
			ptr = s->addrs;
			size = S8D_MAX_BUFFERS * 8;
		} else if (i == S8_UBO_SYSVALS) {
			ptr = sysvals;
			size = S8_SV_SIZE;
		}
		/* An unbound UBO reads 64 zero bytes (entries must be >= 1). */
		if (ptr == 0 || size == 0) {
			ptr = s8g.statics.gpu + 0x500;
			size = 64;
		}
		pan_pack(&ubo[i], UNIFORM_BUFFER, cfg) {
			cfg.pointer = ptr;
			cfg.entries = MIN2(DIV_ROUND_UP(size, 16), 4096);
		}
	}
	return d.gpu;
}

/* Metal buffers without GPU memory (bytesNoCopy) are copied for the draw. */
static void s8_gpu_visible(struct s8d_cmdbuf *cb, struct s8_binding *b)
{
	if (!b->set || b->gpu || b->cpu == NULL || b->length == 0)
		return;
	struct s8_ptr p = s8_alloc(&cb->arena, b->length, 64);
	if (p.cpu == NULL)
		return;
	memcpy(p.cpu, b->cpu, b->length);
	b->gpu = p.gpu;
}

static uint64_t s8_viewport(struct s8d_cmdbuf *cb, struct s8_pass *p, float sysval[8])
{
	double vx = 0, vy = 0, vw = p->target.width, vh = p->target.height, zn = 0, zf = 1;
	if (cb->viewport_set) {
		vx = cb->viewport[0];
		vy = cb->viewport[1];
		vw = cb->viewport[2];
		vh = cb->viewport[3];
		zn = cb->viewport[4];
		zf = cb->viewport[5];
	}
	/* Metal NDC: y up, z 0..1; window y down. */
	sysval[0] = (float)(vw / 2);
	sysval[1] = (float)(-vh / 2);
	sysval[2] = (float)(zf - zn);
	sysval[4] = (float)(vx + vw / 2);
	sysval[5] = (float)(vy + vh / 2);
	sysval[6] = (float)zn;
	if (cb->vp_desc)
		return cb->vp_desc;
	int minx = (int)vx, miny = (int)vy, maxx = (int)(vx + vw), maxy = (int)(vy + vh);
	if (cb->scissor_set) {
		minx = MAX2(minx, (int)cb->scissor[0]);
		miny = MAX2(miny, (int)cb->scissor[1]);
		maxx = MIN2(maxx, (int)(cb->scissor[0] + cb->scissor[2]));
		maxy = MIN2(maxy, (int)(cb->scissor[1] + cb->scissor[3]));
	}
	minx = CLAMP(minx, 0, (int)p->target.width);
	maxx = CLAMP(maxx, 0, (int)p->target.width);
	miny = CLAMP(miny, 0, (int)p->target.height);
	maxy = CLAMP(maxy, 0, (int)p->target.height);
	maxx = maxx > minx ? maxx - 1 : maxx;
	maxy = maxy > miny ? maxy - 1 : maxy;
	struct s8_ptr d = s8_alloc_desc(&cb->arena, VIEWPORT);
	if (!d.cpu)
		return 0;
	pan_pack((struct mali_viewport_packed *)d.cpu, VIEWPORT, cfg) {
		cfg.scissor_minimum_x = minx;
		cfg.scissor_minimum_y = miny;
		cfg.scissor_maximum_x = maxx;
		cfg.scissor_maximum_y = maxy;
		cfg.minimum_z = (float)MIN2(zn, zf);
		cfg.maximum_z = (float)MAX2(zn, zf);
	}
	cb->vp_desc = d.gpu;
	return d.gpu;
}

static enum mali_draw_mode s8_draw_mode(unsigned prim, unsigned *verts)
{
	switch (prim) {
	case 0: *verts = 1; return MALI_DRAW_MODE_POINTS;
	case 1: *verts = 2; return MALI_DRAW_MODE_LINES;
	case 2: *verts = 2; return MALI_DRAW_MODE_LINE_STRIP;
	case 4: *verts = 3; return MALI_DRAW_MODE_TRIANGLE_STRIP;
	default: *verts = 3; return MALI_DRAW_MODE_TRIANGLES;
	}
}

/* Vertex attributes from the pipeline's vertex descriptor; buffer slots
 * pair up as in PanVK (2 per buffer for the NPOT continuation). */
static int s8_attributes(struct s8d_cmdbuf *cb, struct s8d_pipeline *pl, unsigned padded, unsigned instances,
    uint64_t *attribs, uint64_t *bufs)
{
	*attribs = *bufs = 0;
	if (pl->nattribs == 0)
		return 0;
	int slot_of[S8D_MAX_BUFFERS];
	unsigned nslots = 0, used[S8D_MAX_BUFFERS];
	for (unsigned i = 0; i < S8D_MAX_BUFFERS; i++)
		slot_of[i] = -1;
	for (unsigned i = 0; i < pl->nattribs; i++) {
		unsigned b = pl->attribs[i].buffer;
		if (pl->attribs[i].format && b < S8D_MAX_BUFFERS && slot_of[b] < 0) {
			slot_of[b] = (int)nslots;
			used[nslots++] = b;
		}
	}
	struct s8_ptr bd = s8_alloc_descs(&cb->arena, nslots * 2 + 1, ATTRIBUTE_BUFFER);
	struct s8_ptr ad = s8_alloc_descs(&cb->arena, pl->nattribs, ATTRIBUTE);
	if (!bd.cpu || !ad.cpu)
		return -1;
	struct mali_attribute_buffer_packed *bdesc = bd.cpu;
	struct s8_stage_state *vs = &cb->stage[S8D_STAGE_VERTEX];
	for (unsigned k = 0; k < nslots; k++) {
		unsigned b = used[k];
		struct s8_binding *bind = &vs->buffers[b];
		s8_gpu_visible(cb, bind);
		uint64_t addr = bind->gpu & ~63ULL;
		uint64_t size = bind->length + (bind->gpu & 63);
		bool per_instance = pl->layouts[b].step == 2;
		pan_pack(&bdesc[k * 2], ATTRIBUTE_BUFFER, cfg) {
			if (per_instance && instances > 1 && pl->layouts[b].rate == 1) {
				cfg.type = MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR;
				cfg.divisor_r = __builtin_ctz(padded);
			} else if (!per_instance && instances > 1) {
				cfg.type = MALI_ATTRIBUTE_TYPE_1D_MODULUS;
				cfg.divisor = padded;
			} else {
				cfg.type = MALI_ATTRIBUTE_TYPE_1D;
			}
			cfg.stride = (per_instance && instances <= 1) || pl->layouts[b].step == 0 ? 0 : pl->layouts[b].stride;
			cfg.pointer = addr;
			cfg.size = size;
		}
		memset(&bdesc[k * 2 + 1], 0, pan_size(ATTRIBUTE_BUFFER));
	}
	memset(&bdesc[nslots * 2], 0, pan_size(ATTRIBUTE_BUFFER));
	struct mali_attribute_packed *adesc = ad.cpu;
	for (unsigned i = 0; i < pl->nattribs; i++) {
		const struct s8d_vertex_attrib *a = &pl->attribs[i];
		enum pipe_format pf = s8_vertex_format(a->format);
		unsigned b = a->buffer < S8D_MAX_BUFFERS ? a->buffer : 0;
		pan_pack(&adesc[i], ATTRIBUTE, cfg) {
			if (!a->format || pf == PIPE_FORMAT_NONE || slot_of[b] < 0) {
				cfg.format = (MALI_CONSTANT << 12) | PAN_V6_SWIZZLE(0, 0, 0, 0);
			} else {
				cfg.buffer_index = slot_of[b] * 2;
				cfg.offset = a->offset + (vs->buffers[b].gpu & 63);
				cfg.offset_enable = true;
				cfg.format = GENX(pan_format_from_pipe_format)(pf)->hw;
			}
		}
	}
	*attribs = ad.gpu;
	*bufs = bd.gpu;
	return 0;
}

void s8d_draw(struct s8d_cmdbuf *cb, unsigned prim, unsigned start, unsigned count,
    unsigned instances, unsigned index_size, uint64_t index_gpu, const void *index_cpu)
{
	static unsigned logged;
	struct s8_pass *p = s8_cur(cb);
	struct s8d_pipeline *pl = cb->pipeline;
	if (p == NULL || count == 0 || instances == 0)
		return;
	if (pl == NULL || pl->fs == NULL) {
		p->skipped++;
		return;
	}
	if (pl->color != p->format && logged++ < 20)
		s8_log("MTL draw: pipeline %u format %d on a format %d target", pl->id, pl->color, p->format);
	if (p->jc.job_index + 4 >= UINT16_MAX) {
		p->skipped++;
		return;
	}
	struct s8_stage_state *vst = &cb->stage[0], *fst = &cb->stage[1];
	for (unsigned i = 0; i < S8D_MAX_BUFFERS; i++) {
		if (vst->buffers[i].set && !vst->buffers[i].gpu) {
			s8_gpu_visible(cb, &vst->buffers[i]);
			vst->ubos = vst->addrs = 0;
		}
		if (fst->buffers[i].set && !fst->buffers[i].gpu) {
			s8_gpu_visible(cb, &fst->buffers[i]);
			fst->ubos = fst->addrs = 0;
		}
	}

	/* Index range: the vertex job shades min..max of the indices. */
	unsigned min_index = start, vertex_range = count;
	uint64_t indices = 0;
	if (index_size) {
		const uint8_t *ic = index_cpu;
		if (ic == NULL) {
			p->skipped++;
			return;
		}
		uint32_t lo = UINT32_MAX, hi = 0;
		for (unsigned i = 0; i < count; i++) {
			uint32_t v = index_size == 2 ? ((const uint16_t *)ic)[i] : ((const uint32_t *)ic)[i];
			lo = MIN2(lo, v);
			hi = MAX2(hi, v);
		}
		min_index = lo;
		vertex_range = hi - lo + 1;
		if (index_gpu == 0) {
			struct s8_ptr ib = s8_alloc(&cb->arena, (uint64_t)count * index_size, 64);
			if (!ib.cpu) {
				p->skipped++;
				return;
			}
			memcpy(ib.cpu, ic, (size_t)count * index_size);
			index_gpu = ib.gpu;
		}
		indices = index_gpu;
	}
	unsigned padded = instances > 1 ? pan_padded_vertex_count(vertex_range) : vertex_range;

	/* System values per stage: viewport transform, render target
	 * conversion, first vertex, texture sizes. */
	struct s8_ptr sv[2];
	float svf[8] = { 0 };
	uint64_t viewport = s8_viewport(cb, p, svf);
	for (int st = 0; st < 2; st++) {
		sv[st] = s8_alloc(&cb->arena, S8_SV_SIZE, 16);
		if (!sv[st].cpu) {
			p->skipped++;
			return;
		}
		uint32_t w[S8_SV_SIZE / 4] = { 0 };
		memcpy(w, svf, sizeof(svf));
		w[8] = pl->rt_conv[0];
		w[9] = pl->rt_conv[1];
		w[S8_SV_VERTEX_OFFSET / 4] = min_index;
		struct s8_stage_state *ss = &cb->stage[st];
		for (unsigned t = 0; t < S8D_MAX_TEXTURES; t++) {
			w[S8_SV_TEX_SIZE / 4 + t * 2] = ss->texture_set[t] ? ss->textures[t].width : 1;
			w[S8_SV_TEX_SIZE / 4 + t * 2 + 1] = ss->texture_set[t] ? ss->textures[t].height : 1;
		}
		w[S8_SV_TEX_SIZE / 4 + 31 * 2] = w[S8_SV_TEX_SIZE / 4 + 31 * 2 + 1] = 1;   /* null texture */
		memcpy(sv[st].cpu, w, sizeof(w));
	}

	uint64_t vs_ubos = s8_ubo_table(cb, vst, pl->vs, pl->vs_fc, pl->vs_fc_size, sv[0].gpu);
	uint64_t fs_ubos = s8_ubo_table(cb, fst, pl->fs, pl->fs_fc, pl->fs_fc_size, sv[1].gpu);
	uint64_t vs_tex = s8_texture_table(cb, vst, pl->vs), fs_tex = s8_texture_table(cb, fst, pl->fs);
	uint64_t vs_smp = s8_sampler_table(cb, vst, pl->vs), fs_smp = s8_sampler_table(cb, fst, pl->fs);
	vst->for_pipeline = fst->for_pipeline = pl;
	uint64_t attribs = 0, attrib_bufs = 0;
	if (s8_attributes(cb, pl, padded, instances, &attribs, &attrib_bufs) != 0 || !vs_ubos || !fs_ubos || !viewport) {
		p->skipped++;
		return;
	}

	/* Varying buffers. */
	unsigned nvert = padded * instances;
	struct s8_ptr vb = s8_alloc_descs(&cb->arena, S8_VARY_COUNT + 1, ATTRIBUTE_BUFFER);
	if (!vb.cpu) {
		p->skipped++;
		return;
	}
	struct mali_attribute_buffer_packed *vbd = vb.cpu;
	uint64_t position = 0, psiz = 0;
	for (unsigned i = 0; i < S8_VARY_COUNT; i++) {
		uint64_t size = (uint64_t)nvert * pl->link.stride[i];
		struct s8_ptr m = { NULL, 0 };
		if (size) {
			m = s8_alloc(&cb->arena, size, 64);
			if (!m.cpu) {
				p->skipped++;
				return;
			}
		}
		pan_pack(&vbd[i], ATTRIBUTE_BUFFER, cfg) {
			cfg.stride = pl->link.stride[i];
			cfg.size = size;
			cfg.pointer = m.gpu;
		}
		if (i == S8_VARY_POSITION)
			position = m.gpu;
		if (i == S8_VARY_PSIZ)
			psiz = m.gpu;
	}
	memset(&vbd[S8_VARY_COUNT], 0, pan_size(ATTRIBUTE_BUFFER));

	/* Fragment renderer state + blend (blend constant per draw). */
	struct s8_ptr rsd = s8_alloc(&cb->arena, pan_size(RENDERER_STATE) + pan_size(BLEND), pan_alignment(RENDERER_STATE));
	if (!rsd.cpu) {
		p->skipped++;
		return;
	}
	memcpy(rsd.cpu, &pl->fs_rsd, pan_size(RENDERER_STATE));
	struct mali_blend_packed *bl = (void *)((uint8_t *)rsd.cpu + pan_size(RENDERER_STATE));
	memcpy(bl, &pl->blend, pan_size(BLEND));
	if (pl->blend_uses_constant) {
		float c = pl->constant_mask ? cb->blend_color[ffs(pl->constant_mask) - 1] : 0;
		/* pack_blend_constant: unorm of the channel size, left aligned in 16 bits */
		unsigned chan = util_format_description(pl->color)->channel[0].size;
		uint16_t unorm = (uint16_t)(CLAMP(c, 0.0f, 1.0f) * ((1 << chan) - 1));
		bl->opaque[0] |= (uint32_t)(unorm << (16 - chan)) << 16;
	}

	p->tls_size = MAX2(p->tls_size, pl->tls_size);

	struct mali_invocation_packed invocation;
	pan_pack_work_groups_compute(&invocation, 1, vertex_range, instances, 1, 1, 1, true, false);

	/* Vertex job. */
	struct s8_ptr vj = s8_alloc_desc(&cb->arena, COMPUTE_JOB);
	struct s8_ptr tj = s8_alloc_desc(&cb->arena, TILER_JOB);
	if (!vj.cpu || !tj.cpu) {
		p->skipped++;
		return;
	}
	memcpy(pan_section_ptr(vj.cpu, COMPUTE_JOB, INVOCATION), &invocation, pan_size(INVOCATION));
	pan_section_pack(vj.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
		cfg.job_task_split = 5;
	}
	pan_section_pack(vj.cpu, COMPUTE_JOB, DRAW, cfg) {
		cfg.state = pl->vs_rsd;
		cfg.attributes = attribs;
		cfg.attribute_buffers = attrib_bufs;
		cfg.varyings = pl->link.vs_attribs;
		cfg.varying_buffers = vb.gpu;
		cfg.thread_storage = p->tls.gpu;
		cfg.offset_start = min_index;
		cfg.instance_size = instances > 1 ? padded : 1;
		cfg.uniform_buffers = vs_ubos;
		cfg.textures = vs_tex;
		cfg.samplers = vs_smp;
	}

	/* Tiler job. */
	unsigned pv;
	enum mali_draw_mode mode = s8_draw_mode(prim, &pv);
	memcpy(pan_section_ptr(tj.cpu, TILER_JOB, INVOCATION), &invocation, pan_size(INVOCATION));
	pan_section_pack(tj.cpu, TILER_JOB, PRIMITIVE, cfg) {
		cfg.draw_mode = mode;
		if (pl->writes_psiz && prim == 0)
			cfg.point_size_array_format = MALI_POINT_SIZE_ARRAY_FORMAT_FP16;
		cfg.first_provoking_vertex = true;
		cfg.job_task_split = 6;
		cfg.index_count = count;
		if (index_size) {
			cfg.indices = indices;
			cfg.base_vertex_offset = -(int64_t)min_index;
			cfg.index_type = index_size == 4 ? MALI_INDEX_TYPE_UINT32 : MALI_INDEX_TYPE_UINT16;
		} else {
			cfg.index_type = MALI_INDEX_TYPE_NONE;
		}
	}
	pan_section_pack(tj.cpu, TILER_JOB, PRIMITIVE_SIZE, cfg) {
		if (pl->writes_psiz && prim == 0)
			cfg.size_array = psiz;
		else
			cfg.fixed_sized = 1.0f;
	}
	pan_section_pack(tj.cpu, TILER_JOB, DRAW, cfg) {
		/* Metal's winding is in NDC (y up); the tiler sees window
		 * coordinates (y down), which flips it. */
		cfg.front_face_ccw = cb->winding == 0;
		cfg.cull_front_face = cb->cull == 1;
		cfg.cull_back_face = cb->cull == 2;
		if (mode == MALI_DRAW_MODE_LINES || mode == MALI_DRAW_MODE_LINE_STRIP)
			cfg.flat_shading_vertex = true;
		cfg.position = position;
		cfg.state = rsd.gpu;
		cfg.viewport = viewport;
		cfg.varyings = pl->link.fs_attribs;
		cfg.varying_buffers = pl->link.fs_attribs ? vb.gpu : 0;
		cfg.thread_storage = p->tls.gpu;
		cfg.offset_start = min_index;
		cfg.instance_size = instances > 1 ? padded : 1;
		cfg.instance_primitive_size = pan_padded_vertex_count(DIV_ROUND_UP(padded, pv));
		cfg.uniform_buffers = fs_ubos;
		cfg.textures = fs_tex;
		cfg.samplers = fs_smp;
	}
	pan_section_pack(tj.cpu, TILER_JOB, TILER, cfg) {
		cfg.address = p->tiler_ctx.gpu;
	}
	pan_section_pack(tj.cpu, TILER_JOB, PADDING, padding);

	struct pan_ptr vjp = { .cpu = vj.cpu, .gpu = vj.gpu }, tjp = { .cpu = tj.cpu, .gpu = tj.gpu };
	unsigned vid = pan_jc_add_job(&p->jc, MALI_JOB_TYPE_VERTEX, false, false, 0, 0, &vjp, false);
	pan_jc_add_job(&p->jc, MALI_JOB_TYPE_TILER, false, false, vid, 0, &tjp, false);
	p->draws++;
}

/* ---- execution ---- */

static int s8_submit(uint64_t jc, unsigned slot, const char *what, unsigned pass_draws)
{
	static unsigned faults;
	struct s8mali_submit s = { .jc = jc, .slot = slot, .timeout_ms = 2000 };
	if (ioctl(s8_fd(), S8MALI_SUBMIT, &s) != 0 || s.result != 0) {
		if (faults++ < 30)
			s8_log("MTL %s job chain failed: result %u js 0x%x irq 0x%x fault 0x%x at 0x%llx (%u draws)",
			    what, s.result, s.js_status, s.irq_rawstat, s.as_fault,
			    (unsigned long long)s.as_fault_addr, pass_draws);
		return -1;
	}
	return 0;
}

/* Frame shader DCD that reloads the target (pan_preload_emit_dcd). */
static int s8_preload(struct s8d_cmdbuf *cb, struct s8_pass *p)
{
	pthread_mutex_lock(&s8g.lock);         /* shaders are parsed on first use */
	struct s8_shader *sh = s8_shader("@preload");
	pthread_mutex_unlock(&s8g.lock);
	if (sh == NULL)
		return -1;
	struct s8_ptr dcds = s8_alloc_descs(&cb->arena, 3, DRAW);
	struct s8_ptr rsd = s8_alloc(&cb->arena, pan_size(RENDERER_STATE) + pan_size(BLEND), pan_alignment(RENDERER_STATE));
	struct s8_ptr tex = s8_alloc_desc(&cb->arena, TEXTURE);
	struct s8_ptr surf = s8_alloc_desc(&cb->arena, SURFACE_WITH_STRIDE);
	struct s8_ptr smp = s8_alloc_desc(&cb->arena, SAMPLER);
	if (!dcds.cpu || !rsd.cpu || !tex.cpu || !surf.cpu || !smp.cpu)
		return -1;
	pan_unpack(&sh->rsd, RENDERER_STATE, rs);
	pan_pack((struct mali_renderer_state_packed *)rsd.cpu, RENDERER_STATE, cfg) {
		cfg = rs;
		cfg.multisample_misc.sample_mask = 0xFFFF;
		cfg.multisample_misc.depth_function = MALI_FUNC_ALWAYS;
		cfg.stencil_mask_misc.stencil_mask_front = 0xFF;
		cfg.stencil_mask_misc.stencil_mask_back = 0xFF;
		cfg.stencil_front.compare_function = MALI_FUNC_ALWAYS;
		cfg.stencil_front.stencil_fail = MALI_STENCIL_OP_REPLACE;
		cfg.stencil_front.depth_fail = MALI_STENCIL_OP_REPLACE;
		cfg.stencil_front.depth_pass = MALI_STENCIL_OP_REPLACE;
		cfg.stencil_front.mask = 0xFF;
		cfg.stencil_back = cfg.stencil_front;
		cfg.properties.zs_update_operation = MALI_PIXEL_KILL_FORCE_EARLY;
		cfg.properties.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_EARLY;
		cfg.properties.allow_forward_pixel_to_kill = true;
		cfg.properties.allow_forward_pixel_to_be_killed = true;
	}
	pan_pack((struct mali_blend_packed *)((uint8_t *)rsd.cpu + pan_size(RENDERER_STATE)), BLEND, cfg) {
		cfg.round_to_fb_precision = true;
		cfg.srgb = util_format_is_srgb(p->format);
		cfg.internal.mode = MALI_BLEND_MODE_OPAQUE;
		cfg.equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
		cfg.equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
		cfg.equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
		cfg.equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
		cfg.equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
		cfg.equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
		cfg.equation.color_mask = 0xf;
		cfg.internal.fixed_function.num_comps = 4;
		cfg.internal.fixed_function.conversion.memory_format =
		    GENX(pan_dithered_format_from_pipe_format)(p->format, false);
		cfg.internal.fixed_function.conversion.register_format = MALI_REGISTER_FILE_FORMAT_F32;
		cfg.internal.fixed_function.rt = 0;
	}
	s8_pack_texture(tex.cpu, surf.cpu, surf.gpu, &p->target);
	struct s8d_sampler nearest = { .normalized = 0 };
	s8_pack_sampler(smp.cpu, &nearest);
	struct s8_ptr vpd = s8_alloc_desc(&cb->arena, VIEWPORT);
	if (!vpd.cpu)
		return -1;
	pan_pack((struct mali_viewport_packed *)vpd.cpu, VIEWPORT, cfg) {
		cfg.scissor_maximum_x = p->target.width - 1;
		cfg.scissor_maximum_y = p->target.height - 1;
	}
	memset(dcds.cpu, 0, 3 * pan_size(DRAW));
	pan_pack((struct mali_draw_packed *)dcds.cpu, DRAW, cfg) {
		cfg.thread_storage = p->tls.gpu;
		cfg.state = rsd.gpu;
		cfg.viewport = vpd.gpu;
		cfg.textures = tex.gpu;
		cfg.samplers = smp.gpu;
		cfg.clean_fragment_write = false;
	}
	p->preload_dcd = dcds;
	return 0;
}

int s8d_pass_execute(struct s8d_cmdbuf *cb, int idx)
{
	static unsigned logged;
	if (cb == NULL || idx < 0 || (unsigned)idx >= cb->npasses)
		return -1;
	struct s8_pass *p = &cb->passes[idx];
	if (!p->valid)
		return -1;
	uint32_t wb = 0, internal = 0, sw = 0;
	if (!s8_rt_formats(p->format, &wb, &internal, &sw))
		return -1;
	unsigned w = p->target.width, h = p->target.height;
	uint64_t t0 = s8_now_ns();

	/* Thread storage. */
	struct pan_tls_info tls = { 0 };
	tls.tls.size = p->tls_size;
	tls.tls.ptr = s8_tls(p->tls_size);
	if (p->tls_size && !tls.tls.ptr)
		return -1;
	GENX(pan_emit_tls)(&tls, p->tls.cpu);

	/* Tiler heap and context (fresh per pass: the tiler updates them). */
	if (p->draws) {
		pan_pack((struct mali_tiler_heap_packed *)p->tiler_heap.cpu, TILER_HEAP, cfg) {
			cfg.size = S8_TILER_HEAP;
			cfg.base = s8g.tiler_heap.gpu;
			cfg.bottom = s8g.tiler_heap.gpu;
			cfg.top = s8g.tiler_heap.gpu + S8_TILER_HEAP;
		}
		unsigned max_levels = (s8g.tiler_features >> 8) & 0xf;
		pan_pack((struct mali_tiler_context_packed *)p->tiler_ctx.cpu, TILER_CONTEXT, cfg) {
			cfg.hierarchy_mask = GENX(pan_select_tiler_hierarchy_mask)(w, h, max_levels ? max_levels : 8,
			    S8_TILE_SIZE, S8_TILER_HEAP / 2);
			cfg.fb_width = w;
			cfg.fb_height = h;
			cfg.heap = p->tiler_heap.gpu;
			cfg.sample_pattern = MALI_SAMPLE_PATTERN_SINGLE_SAMPLED;
		}
	}

	bool preload = p->load == 1 && s8_preload(cb, p) == 0;
	pan_section_pack(p->fbd.cpu, FRAMEBUFFER, PARAMETERS, cfg) {
		cfg.pre_frame_0 = preload ? MALI_PRE_POST_FRAME_SHADER_MODE_ALWAYS : MALI_PRE_POST_FRAME_SHADER_MODE_NEVER;
		cfg.pre_frame_1 = MALI_PRE_POST_FRAME_SHADER_MODE_NEVER;
		cfg.post_frame = MALI_PRE_POST_FRAME_SHADER_MODE_NEVER;
		cfg.sample_locations = s8g.sample_positions;
		cfg.frame_shader_dcds = preload ? p->preload_dcd.gpu : 0;
		cfg.tiler = p->draws ? p->tiler_ctx.gpu : 0;
		cfg.width = w;
		cfg.height = h;
		cfg.bound_max_x = w - 1;
		cfg.bound_max_y = h - 1;
		cfg.effective_tile_size = S8_TILE_SIZE;
		cfg.tie_break_rule = MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT;
		cfg.render_target_count = 1;
		cfg.z_internal_format = MALI_Z_INTERNAL_FORMAT_D24;
		cfg.color_buffer_allocation = 1024;
		cfg.sample_count = 1;
		cfg.sample_pattern = MALI_SAMPLE_PATTERN_SINGLE_SAMPLED;
	}
	pan_section_pack(p->fbd.cpu, FRAMEBUFFER, PADDING, padding);
	pan_cast_and_pack((uint8_t *)p->fbd.cpu + pan_size(FRAMEBUFFER), RGB_RENDER_TARGET, cfg) {
		cfg.internal_buffer_offset = 0;
		cfg.internal_format = internal;
		cfg.write_enable = true;
		cfg.writeback_format = wb;
		cfg.swizzle = sw;
		cfg.srgb = util_format_is_srgb(p->format);
		cfg.writeback_block_format = MALI_BLOCK_FORMAT_LINEAR;
		cfg.writeback_buffer.base = p->target.gpu;
		cfg.writeback_buffer.row_stride = p->target.row_bytes;
		cfg.writeback_buffer.surface_stride = 0;
		cfg.dithering_enable = false;
		cfg.clean_pixel_write_enable = p->load != 1;
		if (p->load == 2) {
			cfg.clear.color_0 = p->clear[0];
			cfg.clear.color_1 = p->clear[1];
			cfg.clear.color_2 = p->clear[2];
			cfg.clear.color_3 = p->clear[3];
		}
	}
	pan_section_pack(p->fragment.cpu, FRAGMENT_JOB, HEADER, cfg) {
		cfg.type = MALI_JOB_TYPE_FRAGMENT;
		cfg.index = 1;
	}
	pan_section_pack(p->fragment.cpu, FRAGMENT_JOB, PAYLOAD, cfg) {
		cfg.bound_min_x = 0;
		cfg.bound_min_y = 0;
		cfg.bound_max_x = (w - 1) >> 4;
		cfg.bound_max_y = (h - 1) >> 4;
		cfg.framebuffer = p->fbd.gpu | 1;     /* MFBD, one RT, no ZS/CRC extension */
	}

	if (p->draws && s8_submit(p->jc.first_job, 1, "vertex/tiler", p->draws) != 0)
		return -1;
	uint64_t t1 = s8_now_ns();
	if (s8_submit(p->fragment.gpu, 0, "fragment", p->draws) != 0)
		return -1;
	uint64_t t2 = s8_now_ns();
	if (logged++ < 40 || logged % 500 == 0)
		s8_log("MTL pass %ux%u fmt %u load %u: %u draws (%u skipped), vt %llu us, frag %llu us",
		    w, h, p->target.format, p->load, p->draws, p->skipped,
		    (unsigned long long)((t1 - t0) / 1000), (unsigned long long)((t2 - t1) / 1000));
	return 0;
}
