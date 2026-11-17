#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
struct Memory{virtual~Memory(){};uint64_t length;uint64_t getLength(){return length;}};
struct IOBufferMemoryDescriptor:Memory{void*bytes;void*getBytesNoCopy(){return bytes;}};
#define OSDynamicCast(type,p) dynamic_cast<type*>(p)
struct Surface{uintptr_t address;uint64_t allocSize;uintptr_t kernelAddress(){return address;}};
struct mali_buffer{Memory*memory;Surface*surface;uint64_t size;bool gpuOnlyHeap=false,cachePrimed=false,cpuMapped=false,writeCombined=false;uint64_t last_seq=0;};
struct Event{void*address;uint64_t length;bool clean;};static std::vector<Event>events;
static bool ready=true;static bool hakios_mali_ready(){return ready;}
static void dm_clean(void*p,uint64_t n){events.push_back({p,n,true});}
static void dm_invalidate(void*p,uint64_t n){events.push_back({p,n,false});}
static constexpr uint64_t NSEC_PER_SEC=1000000000;
static bool hakios_mali_wait(uint64_t,uint64_t){return true;}
static bool dream_surface_gpu_readonly(Surface*){return false;}
#include "DreamPanfrostCache.inc"
int main(){
 IOBufferMemoryDescriptor mem;mem.length=32ull<<20;mem.bytes=(void*)0xfffffff100001000ULL;
 mali_buffer target{&mem,nullptr,mem.length};
 assert(!target.cpuMapped&&!target.cachePrimed&&!target.gpuOnlyHeap);
 flushSubmit(&target);
 assert(events.size()==1&&events[0].clean&&events[0].length==mem.length&&target.cachePrimed);
 for(unsigned i=0;i<1000;i++){flushComplete(&target);flushSubmit(&target);}
 assert(events.size()==1&&dpf_unmapped_skips==2000&&dpf_unmapped_skip_bytes==2000*mem.length);
 ready=false;assert(!dpf_begin_cpu_mapping(&target)&&!target.cpuMapped&&events.size()==1);
 ready=true;assert(dpf_begin_cpu_mapping(&target)&&target.cpuMapped&&events.size()==2&&!events.back().clean);
 assert(events.back().address==mem.bytes&&events.back().length==mem.length);
 assert(dpf_begin_cpu_mapping(&target)&&events.size()==2);
 for(unsigned i=0;i<5;i++){flushSubmit(&target);flushComplete(&target);}
 assert(events.size()==12); // CPU exposure permanently disables skip, including later cache reuse.
 for(unsigned i=2;i<events.size();i++)assert(events[i].clean==(i%2==0));
 mali_buffer early{&mem,nullptr,8192};
 assert(dpf_begin_cpu_mapping(&early)&&early.cpuMapped&&events.size()==12);
 flushSubmit(&early);flushComplete(&early);assert(events.size()==14&&!early.cachePrimed);
 Surface surface{0xfffffff200001000ULL,4096};
 mali_buffer imported{&mem,&surface,4096,true,true,false};
 flushSubmit(&imported);flushComplete(&imported);flushSubmit(&imported);flushComplete(&imported);
 assert(events.size()==18);
 for(unsigned i=14;i<18;i++)assert(events[i].address==(void*)surface.address&&events[i].length==4096);
 mali_buffer heap{&mem,nullptr,mem.length,true,false,false};
 flushSubmit(&heap);flushComplete(&heap);flushSubmit(&heap);
 assert(events.size()==19&&heap.cachePrimed&&dpf_heap_skips==2);
 assert(dpf_begin_cpu_mapping(&heap)&&events.size()==20&&!events.back().clean);
 mali_buffer invalid{&mem,nullptr,mem.length+1};flushSubmit(&invalid);assert(!invalid.cachePrimed&&events.size()==20);
 mem.bytes=nullptr;invalid.size=4096;flushSubmit(&invalid);assert(!invalid.cachePrimed&&events.size()==20);
 mali_buffer absent{nullptr,nullptr,4096};flushSubmit(&absent);assert(!absent.cachePrimed);
 assert(!dpf_begin_cpu_mapping(nullptr));
 mali_buffer reused{&mem,nullptr,4096};mem.bytes=(void*)0xfffffff100001000ULL;
 flushSubmit(&reused);assert(reused.cachePrimed&&events.size()==21&&events.back().clean);
 puts("PASS: production cache ownership: 32MiB unmapped target across1000jobs uses one clean; 2000 redundant passes skipped; first CPU mapping invalidates; subsequent jobs fully sync; imported surfaces/heaps/errors/reused handles/fault guard retained");
}
