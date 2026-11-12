#include <stdio.h>
#include "s8-v6-pack.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/glsl_types.h"
#include "compiler/bifrost_compile.h"
#include "compiler/bifrost/disassemble.h"
int main(void) {
 glsl_type_singleton_init_or_ref();
 nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
   &bifrost_nir_options_v6, "s8_add7");
 b.shader->info.workgroup_size[0] = 1;
 b.shader->info.workgroup_size[1] = 1;
 b.shader->info.workgroup_size[2] = 1;
 nir_def *v = nir_load_global(&b, nir_imm_int(&b, 0x101000), 4, 1, 32);
 nir_store_global(&b, nir_imm_int(&b, 0x101004), 4, nir_iadd_imm(&b, v, 7), 1);
 nir_validate_shader(b.shader, "s8-before");
 bifrost_preprocess_nir(b.shader, 0x6000);
 struct panfrost_compile_inputs in = {.gpu_id=0x6000};
 struct pan_shader_info info = {0};
 struct util_dynarray binary; util_dynarray_init(&binary, NULL);
 bifrost_compile_shader_nir(b.shader, &in, &binary, &info);
 printf("bytes=%u regs=%u tls=%u wls=%u preload=%llx\n", binary.size,
 info.work_reg_count, info.tls_size, info.wls_size, (unsigned long long)info.preload);
 FILE *f=fopen("s8-add7.bin","wb"); if(!f) return 1;
 fwrite(binary.data, 1, binary.size, f); fclose(f);

 if (info.tls_size || info.wls_size || info.preload || info.push.count || binary.size > 3072) return 2;
 uint8_t state[64] = {0}, tls[32] = {0}, job[192] = {0};
 pan_pack((struct mali_renderer_state_packed *)state, RENDERER_STATE, cfg) { cfg.shader.shader = 0x100400; }
 pan_pack((struct mali_local_storage_packed *)tls, LOCAL_STORAGE, cfg) { }
 pan_section_pack(job, COMPUTE_JOB, HEADER, cfg) { cfg.type=MALI_JOB_TYPE_COMPUTE; cfg.index=1; }
 pan_section_pack(job, COMPUTE_JOB, INVOCATION, cfg) { }
 pan_section_pack(job, COMPUTE_JOB, PARAMETERS, cfg) { cfg.job_task_split=3; }
 pan_section_pack(job, COMPUTE_JOB, DRAW, cfg) { cfg.state=0x100200; cfg.thread_storage=0x100240; }
 f=fopen("s8-state.bin","wb"); fwrite(state,1,sizeof(state),f); fclose(f);
 f=fopen("s8-tls.bin","wb"); fwrite(tls,1,sizeof(tls),f); fclose(f);
 f=fopen("s8-job.bin","wb"); fwrite(job,1,sizeof(job),f); fclose(f);
 disassemble_bifrost(stdout, binary.data, binary.size, true);
 return 0;
}
