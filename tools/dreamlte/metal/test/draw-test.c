/*
 * Host test of the S8 Metal draw path (s8mtl_draw.c) without a phone.
 *
 * The /dev/s8mali calls are answered here: GPU memory is one host buffer
 * (GPU address = S8T_BASE + offset), and S8MALI_SUBMIT runs Mesa's
 * pandecode over the job chain instead of a GPU, which prints every
 * descriptor and reports malformed ones. The shaders are air2nir's
 * "@test_vs"/"@test_fs"/"@preload" in a cache built by build-draw-test.sh.
 *
 *   draw-test CACHE.s8sc
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "wrap.h"
#include "s8mtl_core.h"
#include "s8mtl_draw.h"
#include "../../s8mali_ioctl.h"

#define S8T_BASE  0x8b0000000ULL
#define S8T_SIZE  (96u << 20)

static uint8_t *mem;
static uint64_t used;
static struct pandecode_context *dec;
static int submits, failures;

void s8_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("S8 MALI| ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	if (strstr(fmt, "failed") || strstr(fmt, "bad") || strstr(fmt, "no GPU pipeline"))
		failures++;
}

int s8_fd(void) { return 3; }
uint64_t s8_now_ns(void) { return 0; }

int s8_mem_alloc(s8_mem *m, uint64_t size, uint64_t align)
{
	if (align < 64)
		align = 64;
	uint64_t at = (used + align - 1) & ~(align - 1);
	if (at + size > S8T_SIZE)
		return -1;
	used = at + size;
	memset(mem + at, 0xa5, size);   /* not zero: catch fields we never write */
	m->cpu = mem + at;
	m->gpu = S8T_BASE + at;
	m->size = size;
	m->big = 1;
	return 0;
}

void s8_mem_free(s8_mem *m) { (void)m; }

/* s8mtl_draw.c's /dev/s8mali requests. */
int ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	va_start(ap, req);
	void *arg = va_arg(ap, void *);
	va_end(ap);
	(void)fd;
	if (req == S8MALI_INFO) {
		struct s8mali_info *i = arg;
		memset(i, 0, sizeof(*i));
		i->gpu_id = 0x60a00001;
		i->shader_present = 0x17771777;
		i->tiler_features = 0x809;
		i->ready = 1;
		i->heap_free = S8T_SIZE;
		return 0;
	}
	if (req == S8MALI_SUBMIT) {
		struct s8mali_submit *s = arg;
		printf("---- submit slot %u jc 0x%llx\n", s->slot, (unsigned long long)s->jc);
		pandecode_jc(dec, s->jc, 0x60a00001);
		s->result = 0;
		submits++;
		return 0;
	}
	return -1;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s CACHE.s8sc\n", argv[0]);
		return 2;
	}
	setenv("S8_TEST_CACHE", argv[1], 1);
	mem = aligned_alloc(4096, S8T_SIZE);
	dec = pandecode_create_context(true);  /* decode to stderr */
	pandecode_inject_mmap(dec, S8T_BASE, mem, S8T_SIZE, "s8mali");
	if (s8d_init() != 0)
		return 1;

	struct s8d_pipeline_desc d = { 0 };
	d.vs_name = "@test_vs";
	d.fs_name = "@test_fs";
	d.vs_constants = d.fs_constants = "";
	d.color_format = 80;                    /* BGRA8Unorm */
	d.blending = 1;
	d.write_mask = 0xf;
	d.rgb_op = d.alpha_op = 0;
	d.src_rgb = d.src_alpha = 1;            /* one, one-minus-source-alpha */
	d.dst_rgb = d.dst_alpha = 5;
	d.attribs[0] = (struct s8d_vertex_attrib){ 29, 0, 0 };   /* Float2 */
	d.attribs[1] = (struct s8d_vertex_attrib){ 29, 8, 0 };
	d.layouts[0] = (struct s8d_vertex_layout){ 16, 1, 1 };
	struct s8d_pipeline *p = s8d_pipeline_new(&d, 1);
	if (p == NULL)
		return 1;

	s8_mem target, texmem, vbuf;
	s8_mem_alloc(&target, 256 * 128 * 4, 64);
	s8_mem_alloc(&texmem, 64 * 64 * 4, 64);
	s8_mem_alloc(&vbuf, 4 * 16, 64);
	float v[16] = { -1, -1, 0, 1,   1, -1, 1, 1,   -1, 1, 0, 0,   1, 1, 1, 0 };
	memcpy(vbuf.cpu, v, sizeof(v));
	struct s8d_texture rt = { target.gpu, 256, 128, 256 * 4, 80 };
	struct s8d_texture tex = { texmem.gpu, 64, 64, 64 * 4, 80 };
	struct s8d_sampler smp = { .min_linear = 1, .mag_linear = 1, .normalized = 1 };
	float scale[4] = { 1, 1, 1, 1 }, colour[4] = { 1, 0.5f, 0.25f, 1 };

	for (unsigned load = 1; load <= 2; load++) {
		struct s8d_cmdbuf *cb = s8d_cmdbuf_new();
		double clear[4] = { 0, 0, 1, 1 };
		int pass = s8d_pass_begin(cb, &rt, load, clear);
		s8d_set_pipeline(cb, p);
		s8d_set_buffer(cb, S8D_STAGE_VERTEX, 0, vbuf.gpu, vbuf.cpu, 64);
		s8d_set_bytes(cb, S8D_STAGE_VERTEX, 1, scale, sizeof(scale));
		s8d_set_bytes(cb, S8D_STAGE_FRAGMENT, 0, colour, sizeof(colour));
		s8d_set_texture(cb, S8D_STAGE_FRAGMENT, 0, &tex);
		s8d_set_sampler(cb, S8D_STAGE_FRAGMENT, 0, &smp);
		s8d_set_viewport(cb, 0, 0, 256, 128, 0, 1);
		s8d_draw(cb, 4, 0, 4, 1, 0, 0, NULL);            /* triangle strip */
		uint16_t idx[6] = { 0, 1, 2, 2, 1, 3 };
		s8d_draw(cb, 3, 0, 6, 1, 2, 0, idx);             /* indexed triangles */
		s8d_pass_end(cb);
		if (s8d_pass_execute(cb, pass) != 0)
			failures++;
		s8d_cmdbuf_free(cb);
	}
	printf("==== %d submits, %d failures\n", submits, failures);
	return failures ? 1 : 0;
}
