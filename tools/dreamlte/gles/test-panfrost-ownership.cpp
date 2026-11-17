#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using IOReturn=int;using user_addr_t=uint64_t;
constexpr int kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNoMemory=2,kIOReturnIOError=3;
constexpr uint64_t NSEC_PER_SEC=1000000000;
constexpr unsigned PANFROST_JD_REQ_FS=1;
static bool allocationFail,copyFail,ready=true,unmapOK=true,finishOnWait=false;
static unsigned allocations,frees,cleanCalls,invalidateCalls,unmaps,completes,releases;
template<class T>static T* allocate(unsigned n){if(allocationFail)return nullptr;++allocations;return new T[n];}
#define IONew(type,n) allocate<type>(n)
#define IODelete(p,type,n) do{(void)(n);++frees;delete[] p;}while(false)
static int copyin(user_addr_t from,void*to,size_t n){if(copyFail)return 1;memcpy(to,(void*)from,n);return 0;}
#define IOLog(...) ((void)0)
struct IOMemoryDescriptor{uint64_t getLength(){return 4096;}void complete(){++completes;}void release(){++releases;}};
struct IOBufferMemoryDescriptor:IOMemoryDescriptor{void*getBytesNoCopy(){return this;}};
#define OSDynamicCast(type,p) ((type*)(p))
struct Surface{uint64_t maliSeq=0,deferredInvalidateSeq=0,allocSize=4096;uintptr_t kernelAddress(){return (uintptr_t)this;}void release(){++releases;}};
struct mali_buffer{IOMemoryDescriptor*memory=nullptr;Surface*surface=nullptr;uint64_t address=0,size=4096,last_seq=0;bool gpuOnlyHeap=false,cachePrimed=false,cpuMapped=false,writeCombined=false,prepared=true;};
static bool hakios_mali_ready(){return ready;}
static uint64_t done,nextSeq;
static bool seenAsync;static mali_buffer*waitBuffer;
static bool hakios_mali_wait(uint64_t seq,uint64_t){
 if(waitBuffer)assert(waitBuffer->last_seq==seq); // Last-use fence published BEFORE waiting.
 if(finishOnWait)done=seq;return ready&&done>=seq;
}
static bool hakios_mali_unmap(uint64_t,uint64_t){++unmaps;return unmapOK;}
static void dm_clean(void*,uint64_t){++cleanCalls;}
static void dm_invalidate(void*,uint64_t){++invalidateCalls;}
#include "DreamPanfrostSnapshot.inc"
static bool dream_surface_gpu_readonly(Surface*){return false;}
#include "DreamPanfrostCache.inc"
static uint64_t mach_absolute_time(){static uint64_t t;return ++t;}
static void absolutetime_to_nanoseconds(uint64_t n,uint64_t*out){*out=n;}
static uint64_t dpf_enqueue_chain(uint64_t,bool,bool asynchronous){seenAsync=asynchronous;return ++nextSeq;}
static unsigned gMaliFlush=1,gMaliSubmits;
static uint64_t dpf_mirror_cpu_syncs=0;
static bool dpf_queue_started=true;
struct HakiosMaliUserClient{
 mali_buffer buffers[4];unsigned fSubmits=0,fFailed=0;uint64_t returnedSeq=0;
 mali_buffer*buffer(unsigned h){return h>=1&&h<=4&&buffers[h-1].memory?&buffers[h-1]:nullptr;}
 void freeBuffer(mali_buffer*);
 IOReturn submit(uint64_t chain,const std::vector<uint32_t>&handles){
  struct {struct {uint64_t jc;unsigned requirements;}submit;}r{{chain,0}};
  dpf_submit_snapshot submitHandles;assert(submitHandles.load((uintptr_t)handles.data(),handles.size())==0);
  mali_buffer*b=nullptr;IOReturn ret=0;
  do{
#include "Submit.production.inc"
  }while(false);
  returnedSeq=r.submit.jc;return ret;
 }
};
#include "Free.production.inc"
int main(){
 uint32_t user[]={1,2};
 {dpf_submit_snapshot s;assert(s.load((uintptr_t)user,2)==0);user[0]=9;assert(s.values[0]==1&&s.count==2);}
 assert(allocations==frees);
 {dpf_submit_snapshot s;allocationFail=true;assert(s.load((uintptr_t)user,2)==kIOReturnNoMemory);allocationFail=false;}
 {dpf_submit_snapshot s;copyFail=true;assert(s.load((uintptr_t)user,2)==kIOReturnBadArgument);copyFail=false;}
 {dpf_submit_snapshot s;assert(s.load(0,4097)==kIOReturnBadArgument&&s.load(0,0)==0);}
 assert(allocations==frees);
 IOBufferMemoryDescriptor memory[4];Surface surface;
 HakiosMaliUserClient c;
 for(unsigned i=0;i<4;i++){c.buffers[i].memory=&memory[i];c.buffers[i].address=0x2000000+i*4096;}
 c.buffers[0].writeCombined=true;c.buffers[0].cpuMapped=true;
 c.buffers[1].gpuOnlyHeap=true; // cachePrimed is established by production pre-submit clean.
 c.buffers[2].surface=&surface;
 c.buffers[3].cpuMapped=true; // Cached allocation fallback: MUST remain synchronous.
 assert(!dpf_async_buffer(&c.buffers[1]));
 assert(c.submit(c.buffers[0].address,{1,2})==0&&seenAsync&&c.returnedSeq==1);
 assert(c.buffers[0].last_seq==1&&c.buffers[1].last_seq==1&&done==0&&invalidateCalls==0);
 assert(!dpf_begin_cpu_mapping(&c.buffers[1])&&!c.buffers[1].cpuMapped&&!invalidateCalls);
 done=1;assert(dpf_begin_cpu_mapping(&c.buffers[1])&&c.buffers[1].cpuMapped&&invalidateCalls==1);
 assert(!dpf_async_buffer(&c.buffers[1]));
 finishOnWait=true;waitBuffer=&c.buffers[0];
 auto invalidBefore=invalidateCalls;
 assert(c.submit(c.buffers[0].address,{1,3})==0&&!seenAsync&&surface.maliSeq==2&&invalidateCalls==invalidBefore+1);
 assert(c.submit(c.buffers[0].address,{1,4})==0&&!seenAsync);
 waitBuffer=nullptr;dpf_queue_started=false;
 assert(c.submit(c.buffers[0].address,{1})==0&&!seenAsync);dpf_queue_started=true;
 assert(c.submit(0x10000000,{1})==0&&!seenAsync); // Chain not owned by snapshot: synchronous.
 auto queued=nextSeq;assert(c.submit(c.buffers[0].address,{8})==kIOReturnBadArgument&&nextSeq==queued);
 mali_buffer b=c.buffers[0];b.last_seq=7;finishOnWait=false;done=6;
 auto beforeR=releases,beforeC=completes,beforeU=unmaps;
 c.freeBuffer(&b);assert(!b.memory&&releases==beforeR&&completes==beforeC&&unmaps==beforeU); // Timed out: pin DMA storage.
 b=c.buffers[0];b.last_seq=7;done=7;unmapOK=false;
 c.freeBuffer(&b);assert(!b.memory&&releases==beforeR&&completes==beforeC);
 b=c.buffers[0];b.last_seq=7;unmapOK=true;
 c.freeBuffer(&b);assert(!b.memory&&releases==beforeR+1&&completes==beforeC+1);
 b=c.buffers[0];ready=false;beforeU=unmaps;beforeR=releases;
 c.freeBuffer(&b);assert(!b.memory&&unmaps==beforeU&&releases==beforeR);
 assert(allocations==frees);
 puts("PASS actual SUBMIT/cache/free/snapshot: immutable handles, safe eligibility, queue-time fences, map waits, synchronous cached-surface invalidation, fault/timeout DMA retention");
}
