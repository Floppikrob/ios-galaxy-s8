#include <stdio.h>
#include "s8-v6-pack.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/glsl_types.h"
#include "compiler/bifrost_compile.h"
#include "compiler/bifrost/disassemble.h"
int main(void) {
 glsl_type_singleton_init_or_ref();
 nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
   &bifrost_nir_options_v6, "s8_scale_x");
 b.shader->info.workgroup_size[0] = 64;
 b.shader->info.workgroup_size[1] = 1;
 b.shader->info.workgroup_size[2] = 1;
 nir_def *gid = nir_load_global_invocation_id(&b, 32);
 nir_def *x = nir_channel(&b, gid, 0), *y = nir_channel(&b, gid, 1);
 nir_def *params = nir_load_global(&b, nir_imm_int(&b, 0x101000), 16, 4, 32);
 nir_def *inside = nir_iand(&b, nir_ult(&b,x,nir_channel(&b,params,0)),
                              nir_ult(&b,y,nir_channel(&b,params,1)));
 nir_push_if(&b, inside);
 nir_def *sy = nir_udiv_imm(&b,nir_imul_imm(&b,y,609),740);
 nir_def *srcrow = nir_imul(&b,sy,nir_channel(&b,params,2));
 nir_def *dx = nir_imul_imm(&b,x,8);
 nir_def *px[8];
 nir_def *sx0 = nir_udiv_imm(&b,nir_imul_imm(&b,nir_iadd_imm(&b,dx,0),125),152);
 px[0] = nir_load_global(&b,nir_iadd_imm(&b,nir_iadd(&b,srcrow,nir_imul_imm(&b,sx0,4)),0x10000000),4,1,32);
 nir_def *sx1 = nir_udiv_imm(&b,nir_imul_imm(&b,nir_iadd_imm(&b,dx,1),125),152);
 px[1] = nir_load_global(&b,nir_iadd_imm(&b,nir_iadd(&b,srcrow,nir_imul_imm(&b,sx1,4)),0x10000000),4,1,32);
 nir_def *sx2 = nir_udiv_imm(&b,nir_imul_imm(&b,nir_iadd_imm(&b,dx,2),125),152);
 px[2] = nir_load_global(&b,nir_iadd_imm(&b,nir_iadd(&b,srcrow,nir_imul_imm(&b,sx2,4)),0x10000000),4,1,32);
 nir_def *sx3 = nir_udiv_imm(&b,nir_imul_imm(&b,nir_iadd_imm(&b,dx,3),125),152);
 px[3] = nir_load_global(&b,nir_iadd_imm(&b,nir_iadd(&b,srcrow,nir_imul_imm(&b,sx3,4)),0x10000000),4,1,32);
 nir_def *sx4 = nir_udiv_imm(&b,nir_imul_imm(&b,nir_iadd_imm(&b,dx,4),125),152);
 px[4] = nir_load_global(&b,nir_iadd_imm(&b,nir_iadd(&b,srcrow,nir_imul_imm(&b,sx4,4)),0x10000000),4,1,32);
 nir_def *sx5 = nir_udiv_imm(&b,nir_imul_imm(&b,nir_iadd_imm(&b,dx,5),125),152);
 px[5] = nir_load_global(&b,nir_iadd_imm(&b,nir_iadd(&b,srcrow,nir_imul_imm(&b,sx5,4)),0x10000000),4,1,32);
 nir_def *sx6 = nir_udiv_imm(&b,nir_imul_imm(&b,nir_iadd_imm(&b,dx,6),125),152);
 px[6] = nir_load_global(&b,nir_iadd_imm(&b,nir_iadd(&b,srcrow,nir_imul_imm(&b,sx6,4)),0x10000000),4,1,32);
 nir_def *sx7 = nir_udiv_imm(&b,nir_imul_imm(&b,nir_iadd_imm(&b,dx,7),125),152);
 px[7] = nir_load_global(&b,nir_iadd_imm(&b,nir_iadd(&b,srcrow,nir_imul_imm(&b,sx7,4)),0x10000000),4,1,32);
 nir_def *doff = nir_iadd(&b,nir_imul_imm(&b,x,32),nir_imul(&b,y,nir_channel(&b,params,3)));
 nir_store_global(&b,nir_iadd_imm(&b,doff,0x20000090),16,nir_vec4(&b,px[0],px[1],px[2],px[3]),15);
 nir_store_global(&b,nir_iadd_imm(&b,doff,0x200000a0),16,nir_vec4(&b,px[4],px[5],px[6],px[7]),15);
 nir_pop_if(&b,NULL);
 nir_validate_shader(b.shader, "s8-before");
 bifrost_preprocess_nir(b.shader, 0x6000);
 struct panfrost_compile_inputs in = {.gpu_id=0x6000};
 struct pan_shader_info info = {0};
 struct util_dynarray binary; util_dynarray_init(&binary, NULL);
 bifrost_compile_shader_nir(b.shader, &in, &binary, &info);
 printf("bytes=%u regs=%u tls=%u wls=%u preload=%llx\n", binary.size,
 info.work_reg_count, info.tls_size, info.wls_size, (unsigned long long)info.preload);
 FILE *f=fopen("s8-scale-x.bin","wb"); if(!f) return 1;
 fwrite(binary.data, 1, binary.size, f); fclose(f);

 if (info.tls_size || info.wls_size || info.push.count || binary.size > 3072) return 2;
 uint8_t state[64] = {0}, tls[32] = {0}, job[192] = {0};
 pan_pack((struct mali_renderer_state_packed *)state, RENDERER_STATE, cfg) { cfg.shader.shader = 0x100400;
 cfg.preload.compute.pc = (info.preload >> 48) & 1;
 cfg.preload.compute.local_invocation_xy = (info.preload >> 55) & 1;
 cfg.preload.compute.local_invocation_z = (info.preload >> 56) & 1;
 cfg.preload.compute.work_group_x = (info.preload >> 57) & 1;
 cfg.preload.compute.work_group_y = (info.preload >> 58) & 1;
 cfg.preload.compute.work_group_z = (info.preload >> 59) & 1;
 cfg.preload.compute.global_invocation_x = (info.preload >> 60) & 1;
 cfg.preload.compute.global_invocation_y = (info.preload >> 61) & 1;
 cfg.preload.compute.global_invocation_z = (info.preload >> 62) & 1; }
 pan_pack((struct mali_local_storage_packed *)tls, LOCAL_STORAGE, cfg) { }
 pan_section_pack(job, COMPUTE_JOB, HEADER, cfg) { cfg.type=MALI_JOB_TYPE_COMPUTE; cfg.index=1; }
 pan_section_pack(job, COMPUTE_JOB, INVOCATION, cfg) { }
 pan_section_pack(job, COMPUTE_JOB, PARAMETERS, cfg) { cfg.job_task_split=3; }
 pan_section_pack(job, COMPUTE_JOB, DRAW, cfg) { cfg.state=0x100200; cfg.thread_storage=0x100240; }
 f=fopen("s8-scale-x-state.bin","wb"); fwrite(state,1,sizeof(state),f); fclose(f);
 f=fopen("s8-scale-x-tls.bin","wb"); fwrite(tls,1,sizeof(tls),f); fclose(f);
 f=fopen("s8-scale-x-job.bin","wb"); fwrite(job,1,sizeof(job),f); fclose(f);
 disassemble_bifrost(stdout, binary.data, binary.size, true);
 return 0;
}
