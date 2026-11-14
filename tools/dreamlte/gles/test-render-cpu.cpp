#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using boolean_t=bool;
struct processor {int state;}; using processor_t=processor*;
struct thread {processor_t bound_processor;};using thread_t=thread*;
static processor fast{4};static thread submitter{};
static int real_ncpus=8;static constexpr int PROCESSOR_IDLE=4;
static bool available=true,gpu_lock=false;static unsigned cpu=0,binds=0,blocks=0,identities=0;
static const char* process_name="backboardd";static const char* identity="com.apple.backboardd";
static processor_t cpu_to_processor(unsigned target){assert(target==6);return available?&fast:nullptr;}
static thread_t current_thread(){return &submitter;}
static unsigned cpu_number(){return cpu;}
static processor_t thread_bind(processor_t p){assert(!gpu_lock);++binds;auto old=submitter.bound_processor;submitter.bound_processor=p;return old;}
static int thread_block(void*){assert(!gpu_lock&&submitter.bound_processor==&fast);++blocks;cpu=6;return 0;}
static void proc_selfname(char* dst,unsigned n){std::snprintf(dst,n,"%s",process_name);}
static void* current_proc(){return nullptr;}
static const char* csproc_get_identity(void*){++identities;return identity;}
#define DREAM_PANFROST_CPU_TEST 1
#include "dream_render_cpu.inc"
#include "DreamPanfrostCPU.inc"
static void reset(){real_ncpus=8;available=true;fast.state=4;cpu=0;binds=blocks=identities=0;submitter.bound_processor=nullptr;process_name="backboardd";identity="com.apple.backboardd";}
int main(){
 for(unsigned start: {0u,3u,4u,6u,7u}){
  reset();cpu=start;dpf_bind_render_cpu();assert(binds==1&&blocks==(start!=6)&&cpu==6&&submitter.bound_processor==&fast);
  auto queries=identities;dpf_bind_render_cpu();assert(binds==1&&identities==queries);
 }
 reset();fast.state=6;dpf_bind_render_cpu();assert(binds==1);
 reset();process_name="SpringBoard";dpf_bind_render_cpu();assert(!binds&&!identities);
 reset();identity="forged.backboardd";dpf_bind_render_cpu();assert(!binds);
 reset();identity=nullptr;dpf_bind_render_cpu();assert(!binds);
 reset();real_ncpus=4;dpf_bind_render_cpu();assert(!binds&&!identities);
 reset();available=false;dpf_bind_render_cpu();assert(!binds&&!identities);
 for(int state=0;state<4;state++){reset();fast.state=state;dpf_bind_render_cpu();assert(!binds&&!identities);}
 puts("PASS actual scheduler helper + signed compositor gate: topology, states, migration, idempotence, other-process exclusion; no GPU lock held");
}
