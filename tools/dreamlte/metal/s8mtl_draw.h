/*
 * S8MaliMetal draw path (stage 3): Metal render passes as Mali-G71 job
 * chains. The Objective-C encoders (s8mtl_commands.m) record state and draws
 * through this C interface; s8mtl_draw.c builds the Bifrost (v6)
 * descriptors with Mesa's Panfrost headers, after PanVK's job-manager path:
 * per pass a tiler heap and context, thread storage, a framebuffer
 * descriptor and one fragment job; per draw a vertex (compute) job and a
 * tiler job on slot 1. Shaders come precompiled from the S8SC cache
 * (tools/dreamlte/metal/build-shaders.py, air2nir's ABI).
 *
 * The interface is plain C so the Objective-C side keeps its own minimal
 * headers; Metal enum values are passed through unchanged.
 */
#pragma once
#include <stdint.h>

#define S8D_STAGE_VERTEX        0
#define S8D_STAGE_FRAGMENT      1
#define S8D_MAX_BUFFERS         31
#define S8D_MAX_TEXTURES        31
#define S8D_MAX_SAMPLERS        16
#define S8D_MAX_ATTRIBS         16

/* A texture as the GPU sees it (resolved by the Objective-C side). */
struct s8d_texture {
	uint64_t gpu;                   /* 0: not GPU-visible */
	uint32_t width, height;
	uint32_t row_bytes;
	uint32_t format;                /* MTLPixelFormat */
};

struct s8d_sampler {
	uint8_t min_linear, mag_linear;
	uint8_t mip;                    /* MTLSamplerMipFilter */
	uint8_t address[3];             /* MTLSamplerAddressMode s, t, r */
	uint8_t normalized;
	float lod_min, lod_max;
};

struct s8d_vertex_attrib {
	uint32_t format;                /* MTLVertexFormat, 0 = unused */
	uint32_t offset;
	uint32_t buffer;                /* Metal buffer index */
};

struct s8d_vertex_layout {
	uint32_t stride;
	uint32_t step;                  /* MTLVertexStepFunction */
	uint32_t rate;
};

struct s8d_pipeline_desc {
	const char *vs_name, *fs_name;
	const char *vs_constants, *fs_constants;   /* "name=0xbits;..." */
	uint32_t color_format;          /* MTLPixelFormat of attachment 0 */
	uint8_t blending;
	uint8_t write_mask;             /* MTLColorWriteMask */
	uint8_t rgb_op, alpha_op;       /* MTLBlendOperation */
	uint8_t src_rgb, dst_rgb, src_alpha, dst_alpha;   /* MTLBlendFactor */
	struct s8d_vertex_attrib attribs[S8D_MAX_ATTRIBS];
	struct s8d_vertex_layout layouts[S8D_MAX_BUFFERS];
};

struct s8d_pipeline;
struct s8d_cmdbuf;

/* 0 when the shader cache and the GPU are usable (logged once otherwise). */
int s8d_init(void);
/* NULL when a shader is missing from the cache (the reason is logged). */
struct s8d_pipeline *s8d_pipeline_new(const struct s8d_pipeline_desc *d, unsigned id);
void s8d_pipeline_free(struct s8d_pipeline *p);

struct s8d_cmdbuf *s8d_cmdbuf_new(void);
void s8d_cmdbuf_free(struct s8d_cmdbuf *cb);

/* Render pass: load is MTLLoadAction (0 don't care, 1 load, 2 clear).
 * Returns the pass index for s8d_pass_execute, or -1 (the pass is skipped,
 * the reason is logged). */
int s8d_pass_begin(struct s8d_cmdbuf *cb, const struct s8d_texture *target, unsigned load,
    const double clear[4]);
void s8d_pass_end(struct s8d_cmdbuf *cb);

void s8d_set_pipeline(struct s8d_cmdbuf *cb, struct s8d_pipeline *p);
void s8d_set_buffer(struct s8d_cmdbuf *cb, unsigned stage, unsigned index, uint64_t gpu,
    const void *cpu, uint64_t length);
void s8d_set_bytes(struct s8d_cmdbuf *cb, unsigned stage, unsigned index, const void *bytes,
    uint64_t length);
void s8d_set_texture(struct s8d_cmdbuf *cb, unsigned stage, unsigned index, const struct s8d_texture *t);
void s8d_set_sampler(struct s8d_cmdbuf *cb, unsigned stage, unsigned index, const struct s8d_sampler *s);
void s8d_set_viewport(struct s8d_cmdbuf *cb, double x, double y, double w, double h, double znear,
    double zfar);
void s8d_set_scissor(struct s8d_cmdbuf *cb, unsigned x, unsigned y, unsigned w, unsigned h);
void s8d_set_cull(struct s8d_cmdbuf *cb, unsigned cull_mode, unsigned winding);
void s8d_set_blend_color(struct s8d_cmdbuf *cb, float r, float g, float b, float a);
/* prim: MTLPrimitiveType; index_size 0 (not indexed), 2 or 4. */
void s8d_draw(struct s8d_cmdbuf *cb, unsigned prim, unsigned start, unsigned count,
    unsigned instances, unsigned index_size, uint64_t index_gpu, const void *index_cpu);

/* Runs one recorded pass on the GPU (synchronously). 0 on success. */
int s8d_pass_execute(struct s8d_cmdbuf *cb, int pass);
