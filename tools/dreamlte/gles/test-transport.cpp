#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <algorithm>
class IOBufferMemoryDescriptor;
static std::vector<IOBufferMemoryDescriptor*> liveDescriptors;
using IOByteCount=uint64_t;
constexpr int kIODirectionInOut=1,kIOMemoryKernelUserShared=2,kIOReturnSuccess=0,kIOMemoryMapperNone=0;
static uint64_t nextPA=0x880000000ULL;
class IOBufferMemoryDescriptor {
 void *v;uint64_t pa;
 public:
 static IOBufferMemoryDescriptor *inTaskWithOptions(int,int,unsigned,unsigned){return new IOBufferMemoryDescriptor;}
 IOBufferMemoryDescriptor(){v=aligned_alloc(4096,4096);pa=nextPA;nextPA+=4096;liveDescriptors.push_back(this);}
 int prepare(){return 0;}void complete(){}void release(){liveDescriptors.erase(std::remove(liveDescriptors.begin(),liveDescriptors.end(),this),liveDescriptors.end());free(v);delete this;}
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
#define DREAM_PANFROST_WAIT_TEST 1
using IOLock=int;
static uint64_t fakeTime;
static IOLock waitLock;
static IOLock *IOLockAlloc(){return &waitLock;}
static void IOLockLock(IOLock*){}static void IOLockUnlock(IOLock*){}
static uint64_t mach_absolute_time(){return ++fakeTime;}
static constexpr uint64_t NSEC_PER_MSEC=1000000,NSEC_PER_USEC=1000;
static constexpr unsigned THREAD_UNINT=0;
static void clock_interval_to_deadline(unsigned n,uint64_t unit,uint64_t*out){*out=fakeTime+n*unit;}
static void absolutetime_to_nanoseconds(uint64_t n,uint64_t*out){*out=n;}
static void IODelay(unsigned n){fakeTime+=n*1000;}
static int IOLockSleepDeadline(IOLock*,void*,uint64_t until,unsigned){fakeTime=until;return 0;}
#define DREAM_PANFROST_QUEUE_TEST 1
using wait_result_t=int;using thread_t=int;
static constexpr int KERN_SUCCESS=0;
static constexpr uint64_t NSEC_PER_SEC=1000000000;
static void nanoseconds_to_absolutetime(uint64_t n,uint64_t*out){*out=n;}
static int kernel_thread_start(void(*)(void*,int),void*,thread_t*){return 1;}
static void thread_deallocate(thread_t){}
static void dream_bind_gpu_worker_cpu(){}
static void IOLockSleep(IOLock*,void*,unsigned){assert(false);}
static void IOLockWakeup(IOLock*,void*,bool){}
#include "DreamPanfrostBackend.inc"
static void dream_panfrost_start(){}
static uint64_t test_enqueue(uint64_t c,bool f){return dpf_enqueue_chain(c,f,false);}
int main(){
 regs[0]=0x60a00001;regs[0x110]=1;
 for(unsigned as=0;as<8;as++)for(unsigned i=0;i<6;i++)regs[0x2400+as*0x40+dpf_as_offsets[i]]=0x80000000+as*256+i;
 dpf_backend_start();assert(hakios_mali_ready());
 uint64_t product=0,revision=0;
 assert(hakios_mali_parameter(0,&product)&&product==0x6000);
 assert(hakios_mali_parameter(1,&revision)&&revision==1);
 assert(regs[0]==0x60a00001); // Keep raw hardware identity unchanged.
 assert(dpf_mesa_product(0x60000002)==0x6000);
 assert(dpf_mesa_product(0x62210010)==0x6221); // Preserve G72's exact Mesa row.
 assert(dpf_mesa_product(0x70930001)==0x7093);
 assert(dpf_mesa_product(0x07200001)==0x0720);
 assert(dpf_mesa_product(0x60a00003)==0x6000);

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
 assert(test_enqueue(va,false)==1);assert(hakios_mali_wait(1,0));assert(!hakios_mali_wait(2,0));
 for(unsigned as=0;as<8;as++)for(unsigned i=0;i<6;i++)assert(regs[0x2400+as*0x40+dpf_as_offsets[i]]==before[0x2400+as*0x40+dpf_as_offsets[i]]);
 assert(test_enqueue(va,true)==2);
 assert(!test_enqueue(va+1,false));assert(!test_enqueue(va+8192,false));
 hakios_mali_unmap(va,8192);assert(!*dpf_pte(va,false));assert(!dpf_live_bytes);
 va=hakios_mali_address(4096,true);assert(hakios_mali_map(va,0x880100000ULL));
 injectFault=true;assert(!test_enqueue(va,false));assert(dpf_failed&&dmb_poisoned);
 uint64_t old=*dpf_pte(va,false);auto oldPages=dpf_pages;hakios_mali_unmap(va,4096);
 assert(*dpf_pte(va,false)==old&&dpf_pages==oldPages);
 puts("PASS: production GPU transport mappings, RAM bounds, eight AS restoration, submit/wait, fault retention");
 assert(liveDescriptors.size()==dpf_pages); // Driver intentionally retains all translation-domain storage.
 auto retained=liveDescriptors;for(auto*m:retained)m->release(); // Mock GPU is stopped at test end only.
 assert(liveDescriptors.empty());dream_panfrost_start();return 0;
}
