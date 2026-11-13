#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
using IOByteCount=uint64_t;
constexpr int kIODirectionInOut=1,kIOMemoryKernelUserShared=2,kIOReturnSuccess=0,kIOMemoryMapperNone=0;
static uint64_t nextPA=0x880000000ULL;
class IOBufferMemoryDescriptor {
 void *v;uint64_t pa;
 public:
 static IOBufferMemoryDescriptor *inTaskWithOptions(int,int,unsigned,unsigned){return new IOBufferMemoryDescriptor;}
 IOBufferMemoryDescriptor(){v=aligned_alloc(4096,4096);pa=nextPA;nextPA+=4096;}
 int prepare(){return 0;}void complete(){}void release(){free(v);delete this;}
 uint64_t getPhysicalSegment(int,IOByteCount *n,int){*n=4096;return pa;}
 void *getBytesNoCopy(){return v;}
};
static int kernel_task=0,dmb_lock;
static unsigned dmb_gpu;
static bool dmb_ready=true,dmb_poisoned;
static uint32_t dmb_shaders=0x17771777;
static const unsigned DMB_CONTEXT_LIMIT=8;
static std::map<unsigned,uint32_t> regs;
static bool injectFault;
static std::vector<unsigned> writes;
static void IOLockLock(int){}static void IOLockUnlock(int){}static void IOSleep(int){}
static void dm_clean(void *,unsigned){}
static uint32_t dm_read(unsigned,unsigned off){return regs[off];}
static void dm_write(unsigned,unsigned off,uint32_t val){
 writes.push_back(off);regs[off]=val;
 if(off==0x1004)regs[0x1000]&=~val;
 if(off==0x2004)regs[0x2000]&=~val;
 if(off==0x190)regs[0x150]=val;
 if(off==0x1860||off==0x18e0){
   regs[off]=0;
   if(injectFault)regs[0x2000]=1;
   else regs[0x1000]|=off==0x1860?1:2;
 }
}
static bool dmb_wait(unsigned off,uint32_t mask,uint32_t want){return (regs[off]&mask)==want;}
#define IOLog(...) std::printf(__VA_ARGS__)
#include "DreamPanfrostBackend.inc"
static void dream_panfrost_start(){}
int main(){
 regs[0]=0x60a00001;regs[0x110]=1;
 for(unsigned as=0;as<8;as++)for(unsigned i=0;i<6;i++)regs[0x2400+as*0x40+dpf_as_offsets[i]]=0x80000000+as*256+i;
 dpf_backend_start();assert(hakios_mali_ready());
 assert(!hakios_mali_address(17*1024*1024,true));
 uint64_t va=hakios_mali_address(8192,true);assert(va>=0x2000000&&va<0x100000000ULL);
 assert(hakios_mali_map(va,0x8ffffe000ULL));assert(hakios_mali_map(va+4096,0x8fffff000ULL));
 assert(!hakios_mali_map(va,0x880100000ULL));
 assert(!hakios_mali_map(va+8192,0x900000000ULL));
 assert(!hakios_mali_map(va+8192,0x87ffff000ULL));
 assert(!hakios_mali_map(va+8193,0x880100000ULL));
 assert((*dpf_pte(va,false)&0x000000fffffff000ULL)==0x8ffffe000ULL);
 assert((*dpf_pte(va+4096,false)&0x000000fffffff000ULL)==0x8fffff000ULL);
 auto before=regs;
 assert(hakios_mali_enqueue(va,false)==1);assert(hakios_mali_wait(1,0));assert(!hakios_mali_wait(2,0));
 for(unsigned as=0;as<8;as++)for(unsigned i=0;i<6;i++)assert(regs[0x2400+as*0x40+dpf_as_offsets[i]]==before[0x2400+as*0x40+dpf_as_offsets[i]]);
 assert(hakios_mali_enqueue(va,true)==2);
 assert(!hakios_mali_enqueue(va+1,false));assert(!hakios_mali_enqueue(va+8192,false));
 hakios_mali_unmap(va,8192);assert(!*dpf_pte(va,false));assert(!dpf_live_bytes);
 va=hakios_mali_address(4096,true);assert(hakios_mali_map(va,0x880100000ULL));
 injectFault=true;assert(!hakios_mali_enqueue(va,false));assert(dpf_failed&&dmb_poisoned);
 uint64_t old=*dpf_pte(va,false);auto oldPages=dpf_pages;hakios_mali_unmap(va,4096);
 assert(*dpf_pte(va,false)==old&&dpf_pages==oldPages);
 puts("PASS: production GPU transport mappings, RAM bounds, eight AS restoration, submit/wait, fault retention");
 dream_panfrost_start();return 0;
}
