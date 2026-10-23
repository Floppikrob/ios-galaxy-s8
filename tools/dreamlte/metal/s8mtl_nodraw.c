/*
 * The draw interface for a bundle built without Mesa (metal/build.sh with
 * no S8_MESA_LIB): every call reports "unavailable", so render passes fall
 * back to stage 2 (CPU clears, draws skipped).
 */
#include "s8mtl_draw.h"

#define NULL ((void *)0)

int s8d_init(void) { return -1; }
struct s8d_pipeline *s8d_pipeline_new(const struct s8d_pipeline_desc *d, unsigned id) { (void)d; (void)id; return NULL; }
void s8d_pipeline_free(struct s8d_pipeline *p) { (void)p; }
struct s8d_cmdbuf *s8d_cmdbuf_new(void) { return NULL; }
void s8d_cmdbuf_free(struct s8d_cmdbuf *cb) { (void)cb; }
int s8d_pass_begin(struct s8d_cmdbuf *cb, const struct s8d_texture *t, unsigned load, const double c[4])
{
	(void)cb; (void)t; (void)load; (void)c;
	return -1;
}
void s8d_pass_end(struct s8d_cmdbuf *cb) { (void)cb; }
void s8d_set_pipeline(struct s8d_cmdbuf *cb, struct s8d_pipeline *p) { (void)cb; (void)p; }
void s8d_set_buffer(struct s8d_cmdbuf *cb, unsigned s, unsigned i, uint64_t g, const void *c, uint64_t l)
{
	(void)cb; (void)s; (void)i; (void)g; (void)c; (void)l;
}
void s8d_set_bytes(struct s8d_cmdbuf *cb, unsigned s, unsigned i, const void *b, uint64_t l)
{
	(void)cb; (void)s; (void)i; (void)b; (void)l;
}
void s8d_set_texture(struct s8d_cmdbuf *cb, unsigned s, unsigned i, const struct s8d_texture *t)
{
	(void)cb; (void)s; (void)i; (void)t;
}
void s8d_set_sampler(struct s8d_cmdbuf *cb, unsigned s, unsigned i, const struct s8d_sampler *x)
{
	(void)cb; (void)s; (void)i; (void)x;
}
void s8d_set_viewport(struct s8d_cmdbuf *cb, double x, double y, double w, double h, double n, double f)
{
	(void)cb; (void)x; (void)y; (void)w; (void)h; (void)n; (void)f;
}
void s8d_set_scissor(struct s8d_cmdbuf *cb, unsigned x, unsigned y, unsigned w, unsigned h)
{
	(void)cb; (void)x; (void)y; (void)w; (void)h;
}
void s8d_set_cull(struct s8d_cmdbuf *cb, unsigned m, unsigned w) { (void)cb; (void)m; (void)w; }
void s8d_set_blend_color(struct s8d_cmdbuf *cb, float r, float g, float b, float a)
{
	(void)cb; (void)r; (void)g; (void)b; (void)a;
}
void s8d_draw(struct s8d_cmdbuf *cb, unsigned prim, unsigned start, unsigned count, unsigned n,
    unsigned isz, uint64_t ig, const void *ic)
{
	(void)cb; (void)prim; (void)start; (void)count; (void)n; (void)isz; (void)ig; (void)ic;
}
int s8d_pass_execute(struct s8d_cmdbuf *cb, int pass) { (void)cb; (void)pass; return -1; }
