/* SPDX-License-Identifier: MIT
 *
 * Generates the Mali-G71 (Bifrost v6) "present" blob for the Galaxy S8 XNU
 * port: one fragment job that stretches an iOS frame (a linear BGRA8
 * IOSurface, read as a texture) over the 1440x2960 S-Boot framebuffer.
 *
 * It follows Mesa's Panfrost preload path (src/gallium/drivers/panfrost/
 * pan_fb_preload.c): a pre-frame shader (mode ALWAYS) samples the texture at
 * an interpolated varying, so no tiler or vertex job is needed. The varying
 * corners are the source rectangle, the position corners the panel, so the
 * GPU interpolates the scale. Sampler: unnormalised coordinates, nearest.
 *
 * Build against a Mesa 25.2.8 build tree (tools/dreamlte/build-mali-blit.sh);
 * run: mali-blit-gen BASE_PA OUT.h. All GPU pointers in the blob are absolute
 * (BASE_PA, address space in IDENTITY mode); the kernel patches the texture
 * size, surface pointer/stride and source corners per frame (offsets below).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "genxml/gen_macros.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/glsl_types.h"
#include "pan_shader.h"
#include "pan_format.h"
#include "util/format/u_formats.h"

#define GPU_ID          0x60a00001u     /* boot 36: Mali-G71 r0p0 */
#define PANEL_W         1440
#define PANEL_H         2960
#define PANEL_STRIDE    5760
#define PANEL_PA        0xcc000000ull
#define CANVAS_W        1125
#define CANVAS_H        2436

/* Blob layout (byte offsets from BASE_PA). */
#define O_JOB           0x0000  /* fragment job (header + payload) */
#define O_FBD           0x0100  /* framebuffer parameters + padding */
#define O_RT            0x0180  /* render target, right after the FBD */
#define O_DCD           0x0200  /* 3 frame-shader DCDs (pre 0, pre 1, post) */
#define O_RSD           0x0400  /* renderer state + 1 blend */
#define O_SAMPLER       0x0480
#define O_TEXTURE       0x04c0
#define O_SURFACE       0x0500
#define O_VIEWPORT      0x0540
#define O_TLS           0x0580
#define O_VARYING       0x05c0
#define O_VBUF          0x0600  /* 2 attribute buffers (data + terminator) */
#define O_POSITION      0x0640  /* 4 x vec4 panel corners */
#define O_TEXCOORD      0x0680  /* 4 x vec4 source corners (patched) */
#define O_SAMPLES       0x0700  /* single-sample positions */
#define O_SHADER        0x0800
#define BLOB_SIZE       0x2000

static uint8_t blob[BLOB_SIZE];

static nir_shader *
build_shader(void)
{
	nir_builder b = nir_builder_init_simple_shader(
		MESA_SHADER_FRAGMENT, pan_shader_get_compiler_options(6), "s8_present");
	nir_def *bary = nir_load_barycentric(&b, nir_intrinsic_load_barycentric_pixel,
	    INTERP_MODE_SMOOTH);
	/* Attribute hw index 0 as in pan_fb_preload.c (attr_hw_index(0)). */
	nir_def *coord = nir_load_interpolated_input(&b, 2, 32, bary, nir_imm_int(&b, 0),
	    .base = 0, .dest_type = nir_type_float32,
	    .io_semantics.location = VARYING_SLOT_VAR0, .io_semantics.num_slots = 1);
	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 1);
	tex->dest_type = nir_type_float32;
	tex->texture_index = 0;
	tex->sampler_index = 0;
	tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
	tex->op = nir_texop_txl;
	tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, coord);
	tex->coord_components = 2;
	nir_def_init(&tex->instr, &tex->def, 4, 32);
	nir_builder_instr_insert(&b, &tex->instr);
	nir_store_output(&b, &tex->def, nir_imm_int(&b, 0), .base = 0,
	    .src_type = nir_type_float32,
	    .io_semantics.location = FRAG_RESULT_DATA0, .io_semantics.num_slots = 1,
	    .write_mask = 0xf);
	return b.shader;
}

int
main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s BASE_PA OUT.h\n", argv[0]);
		return 2;
	}
	uint64_t base = strtoull(argv[1], NULL, 0);
	glsl_type_singleton_init_or_ref();

	/* Shader. */
	nir_shader *s = build_shader();
	struct pan_compile_inputs inputs = {
		.gpu_id = GPU_ID,
		.is_blit = true,
		.no_idvs = true,
	};
	struct pan_shader_info info;
	struct util_dynarray binary;
	util_dynarray_init(&binary, NULL);
	nir_shader_gather_info(s, nir_shader_get_entrypoint(s));
	BITSET_SET(s->info.textures_used, 0);
	pan_shader_preprocess(s, inputs.gpu_id);
	pan_shader_compile(s, &inputs, &binary, &info);
	if (O_SHADER + binary.size > BLOB_SIZE) {
		fprintf(stderr, "shader too big: %u\n", binary.size);
		return 1;
	}
	memcpy(blob + O_SHADER, binary.data, binary.size);

	/* Renderer state + blend, as pan_preload_emit_rsd/emit_blend (v6). */
	pan_cast_and_pack((void *)(blob + O_RSD), RENDERER_STATE, cfg) {
		pan_shader_prepare_rsd(&info, base + O_SHADER, &cfg);
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
	pan_cast_and_pack((void *)(blob + O_RSD + pan_size(RENDERER_STATE)), BLEND, cfg) {
		cfg.round_to_fb_precision = true;
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
		    GENX(pan_dithered_format_from_pipe_format)(PIPE_FORMAT_B8G8R8A8_UNORM, false);
		cfg.internal.fixed_function.conversion.register_format =
		    MALI_REGISTER_FILE_FORMAT_F32;
		cfg.internal.fixed_function.rt = 0;
	}

	pan_cast_and_pack((void *)(blob + O_SAMPLER), SAMPLER, cfg) {
		cfg.seamless_cube_map = false;
		cfg.normalized_coordinates = false;
		cfg.minify_nearest = true;
		cfg.magnify_nearest = true;
	}

	/* Texture: linear BGRA8 2D, one level; size and surface patched. */
	pan_cast_and_pack((void *)(blob + O_TEXTURE), TEXTURE, cfg) {
		cfg.dimension = MALI_TEXTURE_DIMENSION_2D;
		cfg.format = GENX(pan_format_from_pipe_format)(PIPE_FORMAT_B8G8R8A8_UNORM)->hw;
		cfg.width = CANVAS_W;
		cfg.height = CANVAS_H;
		cfg.swizzle = (MALI_CHANNEL_R << 0) | (MALI_CHANNEL_G << 3) |
		    (MALI_CHANNEL_B << 6) | (MALI_CHANNEL_A << 9);
		cfg.texel_ordering = MALI_TEXTURE_LAYOUT_LINEAR;
		cfg.levels = 1;
		cfg.surfaces = base + O_SURFACE;
		cfg.array_size = 1;
		cfg.depth = 1;
	}
	pan_cast_and_pack((void *)(blob + O_SURFACE), SURFACE_WITH_STRIDE, cfg) {
		cfg.pointer = 0;
		cfg.row_stride = CANVAS_W * 4;
		cfg.surface_stride = 0;
	}

	pan_cast_and_pack((void *)(blob + O_VIEWPORT), VIEWPORT, cfg) {
		cfg.scissor_minimum_x = 0;
		cfg.scissor_minimum_y = 0;
		cfg.scissor_maximum_x = PANEL_W - 1;
		cfg.scissor_maximum_y = PANEL_H - 1;
	}
	pan_cast_and_pack((void *)(blob + O_TLS), LOCAL_STORAGE, cfg) {
		cfg.wls_instances = MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM;
	}
	pan_cast_and_pack((void *)(blob + O_VARYING), ATTRIBUTE, cfg) {
		cfg.buffer_index = 0;
		cfg.offset_enable = false;
		cfg.format = GENX(pan_format_from_pipe_format)(PIPE_FORMAT_R32G32B32_FLOAT)->hw;
	}
	pan_cast_and_pack((void *)(blob + O_VBUF), ATTRIBUTE_BUFFER, cfg) {
		cfg.pointer = base + O_TEXCOORD;
		cfg.stride = 16;
		cfg.size = 64;
	}
	pan_cast_and_pack((void *)(blob + O_VBUF + pan_size(ATTRIBUTE_BUFFER)), ATTRIBUTE_BUFFER, cfg);

	float pos[16] = {
		0, 0, 0, 1,   PANEL_W, 0, 0, 1,
		0, PANEL_H, 0, 1,   PANEL_W, PANEL_H, 0, 1,
	};
	float tc[16] = {
		0, 0, 0, 1,   CANVAS_W, 0, 0, 1,
		0, CANVAS_H, 0, 1,   CANVAS_W, CANVAS_H, 0, 1,
	};
	memcpy(blob + O_POSITION, pos, sizeof(pos));
	memcpy(blob + O_TEXCOORD, tc, sizeof(tc));

	/* pan_samples.c single-sampled pattern: position 0 and origin 128,128. */
	uint32_t centre = 128u | (128u << 16);
	memcpy(blob + O_SAMPLES, &centre, 4);
	memcpy(blob + O_SAMPLES + 32 * 4, &centre, 4);

	/* Pre-frame DCD 0, as pan_preload_emit_dcd (v6). */
	pan_cast_and_pack((void *)(blob + O_DCD), DRAW, cfg) {
		cfg.thread_storage = base + O_TLS;
		cfg.state = base + O_RSD;
		cfg.position = base + O_POSITION;
		cfg.viewport = base + O_VIEWPORT;
		cfg.varyings = base + O_VARYING;
		cfg.varying_buffers = base + O_VBUF;
		cfg.textures = base + O_TEXTURE;
		cfg.samplers = base + O_SAMPLER;
		cfg.clean_fragment_write = false;
	}

	/* Framebuffer: the scanned-out panel memory, one RGBA8 RT whose
	 * writeback swizzle stores B,G,R,A (XRGB8888 in memory). */
	pan_cast_and_pack((void *)(blob + O_FBD), FRAMEBUFFER_PARAMETERS, cfg) {
		cfg.pre_frame_0 = MALI_PRE_POST_FRAME_SHADER_MODE_ALWAYS;
		cfg.sample_locations = base + O_SAMPLES;
		cfg.frame_shader_dcds = base + O_DCD;
		cfg.width = PANEL_W;
		cfg.height = PANEL_H;
		cfg.bound_max_x = PANEL_W - 1;
		cfg.bound_max_y = PANEL_H - 1;
		cfg.sample_count = 1;
		cfg.sample_pattern = MALI_SAMPLE_PATTERN_SINGLE_SAMPLED;
		cfg.effective_tile_size = 16 * 16;
		cfg.render_target_count = 1;
		cfg.color_buffer_allocation = 1024;
		cfg.tiler = 0;
	}
	pan_cast_and_pack((void *)(blob + O_RT), RGB_RENDER_TARGET, cfg) {
		cfg.internal_format = MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8;
		cfg.write_enable = true;
		cfg.writeback_format = MALI_COLOR_FORMAT_R8G8B8A8;
		cfg.writeback_block_format = MALI_BLOCK_FORMAT_LINEAR;
		cfg.swizzle = (MALI_CHANNEL_B << 0) | (MALI_CHANNEL_G << 3) |
		    (MALI_CHANNEL_R << 6) | (MALI_CHANNEL_A << 9);
		cfg.writeback_buffer.base = PANEL_PA;
		cfg.writeback_buffer.row_stride = PANEL_STRIDE;
		cfg.writeback_buffer.surface_stride = 0;
		cfg.dithering_enable = false;
	}

	/* Fragment job covering all 16x16 tiles of the panel. */
	pan_cast_and_pack((void *)(blob + O_JOB), JOB_HEADER, cfg) {
		cfg.type = MALI_JOB_TYPE_FRAGMENT;
		cfg.index = 1;
	}
	pan_cast_and_pack((void *)(blob + O_JOB + pan_size(JOB_HEADER)), FRAGMENT_JOB_PAYLOAD, cfg) {
		cfg.bound_min_x = 0;
		cfg.bound_min_y = 0;
		cfg.bound_max_x = (PANEL_W - 1) >> 4;
		cfg.bound_max_y = (PANEL_H - 1) >> 4;
		cfg.framebuffer = (base + O_FBD) | 1;	/* MFBD, 1 RT */
	}

	FILE *f = fopen(argv[2], "w");
	if (f == NULL) {
		perror(argv[2]);
		return 1;
	}
	fprintf(f, "/* Generated by tools/dreamlte/mali-blit-gen.c (Mesa 25.2.8 Panfrost v6). */\n");
	fprintf(f, "#define S8_BLIT_BASE_PA     0x%llxULL\n", (unsigned long long)base);
	fprintf(f, "#define S8_BLIT_SIZE        0x%x\n", BLOB_SIZE);
	fprintf(f, "#define S8_BLIT_JOB         0x%x\n", O_JOB);
	fprintf(f, "#define S8_BLIT_TEX_SIZE    0x%x   /* u32: (w-1) | (h-1) << 16 */\n", O_TEXTURE + 4);
	fprintf(f, "#define S8_BLIT_SURF_PTR    0x%x   /* u64 */\n", O_SURFACE);
	fprintf(f, "#define S8_BLIT_SURF_STRIDE 0x%x   /* s32 row stride */\n", O_SURFACE + 8);
	fprintf(f, "#define S8_BLIT_TEXCOORD    0x%x   /* 4 x vec4 float source corners */\n", O_TEXCOORD);
	fprintf(f, "#define S8_BLIT_SHADER_SIZE %u\n", binary.size);
	fprintf(f, "#define S8_BLIT_WORK_REGS   %u\n", info.work_reg_count);
	fprintf(f, "static const unsigned char s8_blit_blob[S8_BLIT_SIZE] = {");
	for (unsigned i = 0; i < BLOB_SIZE; i++)
		fprintf(f, "%s0x%02x,", (i % 16) ? "" : "\n\t", blob[i]);
	fprintf(f, "\n};\n");
	fclose(f);
	printf("shader %u bytes, work regs %u, texture count %u, varyings in %u\n",
	    binary.size, info.work_reg_count, info.texture_count, info.varyings.input_count);
	return 0;
}
