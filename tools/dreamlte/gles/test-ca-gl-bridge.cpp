#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>
using vm_address_t=uintptr_t;using mach_port_t=unsigned;
constexpr int FALSE=0,KERN_SUCCESS=0,VM_PROT_READ=1,VM_PROT_WRITE=2,VM_PROT_EXECUTE=4,VM_PROT_COPY=16;
constexpr unsigned MH_MAGIC_64=0xfeedfacf,LC_UUID=0x1b,LC_SEGMENT_64=0x19;
struct mach_header_64{unsigned magic,cpu,sub,type,ncmds,sizeofcmds,flags,reserved;};
struct load_command{unsigned cmd,cmdsize;};
struct uuid_command{unsigned cmd,cmdsize;uint8_t uuid[16];};
struct segment_command_64{unsigned cmd,cmdsize;char segname[16];uint64_t vmaddr,vmsize,fileoff,filesize;int maxprot,initprot;unsigned nsects,flags;};
static uint32_t vm_page_size=4096;static bool protect_fail;static std::vector<int>protections;
static unsigned mach_task_self(){return 77;}
static int vm_protect(unsigned task,uintptr_t page,size_t size,int maximum,int prot){
 assert(task==77&&!(page&4095)&&size==4096&&maximum==0&&!(prot&VM_PROT_EXECUTE));protections.push_back(prot);return protect_fail?5:0;
}
static unsigned _dyld_image_count(){return 0;}
static const char*_dyld_get_image_name(unsigned){return nullptr;}
static const mach_header_64*_dyld_get_image_header(unsigned){return nullptr;}
static intptr_t _dyld_get_image_vmaddr_slide(unsigned){return 0;}
static void*seen_context;static unsigned seen_function,seen_index;static size_t seen_components,seen_count;
static const float*seen_values;static int original_calls,uniform_calls,result=1,refused_calls;static std::vector<int>order;
void hakios_gl_function_context(void*c){seen_context=c;order.push_back(1);}
void hakios_gl_function_values(void*c,unsigned f,unsigned i,size_t components,size_t count,const float*v){seen_context=c;seen_function=f;seen_index=i;seen_components=components;seen_count=count;seen_values=v;order.push_back(3);}
void hakios_gl_function_refused(unsigned f){seen_function=f;++refused_calls;}
static int original_supported(void*){++original_calls;order.push_back(2);return result;}
static void original_uniform(void*c,unsigned i,size_t comp,size_t count,const float*v){
 ++uniform_calls;assert(seen_context==c&&seen_index==i&&seen_components==comp&&seen_count==count&&seen_values==v);order.push_back(4);
}
#include "CaHooks.production.inc"
int main(){
 alignas(4096) uintptr_t table[1024]{};auto*s=table+10,*u=table+12;
 uintptr_t os=(uintptr_t)original_supported,ou=(uintptr_t)original_uniform;
 *s=os;*u=ou;
 assert(!dream_ca_patch_slots(s,u,os+4,ou)&&protections.empty()&&*s==os&&*u==ou);
 assert(!dream_ca_patch_slots(s,table+512,os,ou)&&protections.empty());
 protect_fail=true;assert(!dream_ca_patch_slots(s,u,os,ou)&&*s==os&&*u==ou&&protections.size()==1);protect_fail=false;protections.clear();
 assert(dream_ca_patch_slots(s,u,os,ou)&&*s==(uintptr_t)dream_ca_supported&&*u==(uintptr_t)dream_ca_uniform);
 assert(protections==std::vector<int>({VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,VM_PROT_READ}));
 uint8_t context[40]{},state[40]{};void*statePointer=state;memcpy(context+16,&statePointer,sizeof(statePointer));state[16]=37;
 result=1;order.clear();assert(((int(*)(void*))*s)(context)==1&&original_calls==1&&seen_context==context&&refused_calls==0);
 assert(order==std::vector<int>({1,2}));
 result=0;assert(((int(*)(void*))*s)(context)==0&&refused_calls==1&&seen_function==37);
 const float values[20]{};order.clear();((void(*)(void*,unsigned,size_t,size_t,const float*))*u)(context,2,4,5,values);
 assert(uniform_calls==1&&seen_function==37&&order==std::vector<int>({3,4}));
 assert(dream_ca_function(nullptr)==90);memset(context+16,0,sizeof(void*));assert(dream_ca_function(context)==90);
 auto saved=*s;vm_page_size=0;assert(!dream_ca_patch_slots(s,u,os,ou)&&*s==saved);vm_page_size=4096;
 dream_ca_gl_hooks(); // No loaded exact image: rejected, existing state untouched.
 puts("PASS: production CA hook guard/callbacks; no forced support; exact forwarding; private writable data only; mismatch/protection/page rejection");
}
