#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
struct Memory{virtual~Memory(){};uint64_t length;uint64_t getLength(){return length;}};
struct IOBufferMemoryDescriptor:Memory{void*bytes;void*getBytesNoCopy(){return bytes;}};
#define OSDynamicCast(type,p) dynamic_cast<type*>(p)
struct Surface{uintptr_t address;uint64_t allocSize;uint64_t deferredInvalidateSeq=0;uintptr_t kernelAddress(){return address;}};
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
 IOBufferMemoryDescriptor mem;mem.length=128ull<<20;mem.bytes=(void*)0xfffffff100001000ULL;
 mali_buffer b{&mem,nullptr,mem.length,true,false,false};
 flushSubmit(&b);assert(events.size()==1&&events[0].clean&&events[0].length==mem.length&&b.cachePrimed);
 for(int n=0;n<100;n++){flushComplete(&b);flushSubmit(&b);}
 assert(events.size()==1&&dpf_heap_skips==200);
 ready=false;assert(!dpf_begin_cpu_mapping(&b)&&!b.cpuMapped&&events.size()==1);ready=true;
 assert(dpf_begin_cpu_mapping(&b)&&b.cpuMapped&&events.size()==2&&!events.back().clean);
 flushSubmit(&b);flushComplete(&b);assert(events.size()==4&&events[2].clean&&!events[3].clean);
 assert(dpf_begin_cpu_mapping(&b)&&events.size()==4); // Subsequent mappings cannot skip sync again.
 mali_buffer early{&mem,nullptr,4096,true,false,false};assert(dpf_begin_cpu_mapping(&early));
 assert(events.size()==4);flushSubmit(&early);flushComplete(&early);assert(events.size()==6);
 mali_buffer normal{&mem,nullptr,8192};normal.cpuMapped=true;flushSubmit(&normal);flushComplete(&normal);assert(events.size()==8);
 Surface surface{0xfffffff200001000ULL,4096};normal.surface=&surface;normal.gpuOnlyHeap=true;normal.cachePrimed=true;
 flushSubmit(&normal);flushComplete(&normal);assert(events.size()==10&&events[8].address==(void*)surface.address);
 normal.surface=nullptr;normal.gpuOnlyHeap=false;normal.size=mem.length+4096;flushSubmit(&normal);assert(events.size()==10);
 mali_buffer invalid{&mem,nullptr,mem.length+4096,true,false,false};flushSubmit(&invalid);assert(!invalid.cachePrimed);
 assert(!dpf_begin_cpu_mapping(nullptr));
 puts("PASS: heap cleaned once across100jobs; normal/shared buffers fully synced; first CPU mapping invalidates and permanently restores sync; fault blocks mapping; bounds preserve unprimed heap");
}
