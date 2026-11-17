#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
using IOReturn=int;using UInt32=unsigned;using IOOptionBits=unsigned;using vm_size_t=uint64_t;
constexpr int kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNoMemory=2,kIOReturnNotReady=3;
constexpr unsigned kIODirectionInOut=3,kIOMemoryKernelUserShared=4,kIOMapWriteCombineCache=0x4000,kIOMapDefaultCache=0,PAGE_SIZE=4096,PANFROST_BO_HEAP=2;
constexpr uint64_t MALI_BUFFER_MAX=256ull<<20;static unsigned gMaliBuffers;static int kernel_task=1,gMaliLock;
static bool locked,ready=true,failWC,failAll,failNew;static unsigned allocations,releases;
static uint64_t round_page_64(uint64_t n){return (n+4095)&~4095ULL;}
static void IOLockLock(int){assert(!locked);locked=true;}static void IOLockUnlock(int){assert(locked);locked=false;}
static bool hakios_mali_ready(){return ready;}
#define IOLog(...) std::printf(__VA_ARGS__)
struct IOMemoryDescriptor{unsigned refs=1;uint64_t length;virtual~IOMemoryDescriptor(){};uint64_t getLength(){return length;}void retain(){++refs;}void release(){if(!--refs){++releases;delete this;}}};
struct IOBufferMemoryDescriptor:IOMemoryDescriptor{
 unsigned mode;void *getBytesNoCopy(){return (void*)0xfffffff100001000ULL;}
 static IOBufferMemoryDescriptor*inTaskWithOptions(int owner,unsigned options,vm_size_t n,unsigned align){
  assert(owner==kernel_task&&align==PAGE_SIZE);++allocations;if(failAll||(failWC&&(options&kIOMapWriteCombineCache)))return nullptr;
  auto*m=new IOBufferMemoryDescriptor;m->mode=options;m->length=n;return m;
 }
};
#define OSDynamicCast(type,p) dynamic_cast<type*>(p)
struct Surface{uintptr_t address;uint64_t allocSize;unsigned mapCache;uintptr_t kernelAddress(){return address;}uint32_t cpuWriteOffered=0;uint64_t deferredInvalidateSeq=0,maliSeq=0;bool displayMirrorReadOnly=false;IOMemoryDescriptor*memory=nullptr;unsigned id=2;};
struct mali_buffer{IOMemoryDescriptor*memory=nullptr;Surface*surface=nullptr;uint64_t address=0,size=0;bool gpuOnlyHeap=false,cachePrimed=false,cpuMapped=false,writeCombined=false;uint64_t last_seq=0;};
static unsigned cleans,invalidates;
static void dm_clean(void*,uint64_t){++cleans;}static void dm_invalidate(void*,uint64_t){++invalidates;}
static constexpr uint64_t NSEC_PER_SEC=1000000000;
static bool hakios_mali_wait(uint64_t,uint64_t){return true;}
using SweetSurface=Surface;
#include "DreamSurfaceCpu.h"
static bool dream_surface_gpu_readonly(Surface*){return false;}
#include "DreamSurfaceSync.inc"
#include "DreamPanfrostCache.inc"
struct drm_panfrost_create_bo{uint32_t size,flags,handle,pad;uint64_t offset;};
static void hakios_mali_tag(uint64_t,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned){}
class HakiosMaliUserClient{public:mali_buffer b;
 mali_buffer*newBuffer(IOMemoryDescriptor*m,uint64_t,uint64_t n,uint32_t*handle,unsigned flags){if(failNew)return nullptr;b={};b.size=n;b.address=0xa010000;b.gpuOnlyHeap=flags&PANFROST_BO_HEAP;*handle=1;(void)m;return &b;}
 mali_buffer*buffer(unsigned type){return type==1&&b.memory?&b:nullptr;}
 IOReturn createBO(drm_panfrost_create_bo*);
 IOReturn clientMemoryForType(UInt32,IOOptionBits*,IOMemoryDescriptor**);
 void reset(){if(b.memory)b.memory->release();b={};}
};
#include "WCCreate.production.inc"
#include "WCMap.production.inc"
int main(){HakiosMaliUserClient c;drm_panfrost_create_bo req={8192,0,0,0,0};
 assert(c.createBO(&req)==0&&c.b.writeCombined&&req.offset==0xa010000);
 auto*m=dynamic_cast<IOBufferMemoryDescriptor*>(c.b.memory);assert(m&&(m->mode&kIOMapWriteCombineCache));
 IOOptionBits mode=99;IOMemoryDescriptor*out=nullptr;
 assert(c.clientMemoryForType(1,&mode,&out)==0&&mode==kIOMapWriteCombineCache&&c.b.cpuMapped&&out==m&&!locked);out->release();
 unsigned beforeC=cleans,beforeI=invalidates;flushSubmit(&c.b);flushComplete(&c.b);
 assert(cleans==beforeC&&invalidates==beforeI&&dpf_wc_skips==2&&dpf_wc_skip_bytes==16384);
 ready=false;assert(c.clientMemoryForType(1,&mode,&out)==kIOReturnNotReady&&!locked);ready=true;c.reset();
 req.flags=3;assert(c.createBO(&req)==0&&!c.b.writeCombined&&c.b.gpuOnlyHeap);
 assert(!(dynamic_cast<IOBufferMemoryDescriptor*>(c.b.memory)->mode&kIOMapWriteCombineCache));
 flushSubmit(&c.b);flushComplete(&c.b);assert(cleans==beforeC+1&&invalidates==beforeI);
 assert(c.clientMemoryForType(1,&mode,&out)==0&&mode==kIOMapDefaultCache);out->release();c.reset();
 req.flags=1;failWC=true;unsigned beforeA=allocations;assert(c.createBO(&req)==0&&allocations==beforeA+2&&!c.b.writeCombined);
 assert(!(dynamic_cast<IOBufferMemoryDescriptor*>(c.b.memory)->mode&kIOMapWriteCombineCache));
 assert(c.clientMemoryForType(1,&mode,&out)==0&&mode==kIOMapDefaultCache);out->release();beforeC=cleans;beforeI=invalidates;
 flushSubmit(&c.b);flushComplete(&c.b);assert(cleans==beforeC+1&&invalidates==beforeI+1);
 Surface surface{0xfffffff200001000ULL,4096,kIOMapDefaultCache};c.b.surface=&surface;c.b.writeCombined=true;
 beforeC=cleans;beforeI=invalidates;uint64_t wc=dpf_wc_skips;flushSubmit(&c.b);flushComplete(&c.b);
 assert(cleans==beforeC+1&&invalidates==beforeI+1&&dpf_wc_skips==wc);
 surface.memory=c.b.memory;surface.deferredInvalidateSeq=9;surface.maliSeq=9;surface.displayMirrorReadOnly=true;beforeI=invalidates;
 assert(c.clientMemoryForType(1,&mode,&out)==0&&mode==kIOMapDefaultCache&&surface.cpuWriteOffered==1&&surface.deferredInvalidateSeq==0&&invalidates==beforeI+1);out->release();c.reset();
 failWC=false;failNew=true;unsigned released=releases;assert(c.createBO(&req)==kIOReturnNoMemory&&releases==released+1);failNew=false;
 failAll=true;assert(c.createBO(&req)==kIOReturnNoMemory);failAll=false;
 req.flags=4;beforeA=allocations;assert(c.createBO(&req)==kIOReturnBadArgument&&allocations==beforeA);
 req.flags=0;req.size=0;assert(c.createBO(&req)==kIOReturnBadArgument);
 req.size=(uint32_t)MALI_BUFFER_MAX+1;assert(c.createBO(&req)==kIOReturnBadArgument);
 puts("PASS: actual BO create+map+sync: consistent WC kernel/user mappings and ordered barriers; cached heap/imported surface/WC-allocation fallback preserve full cache rules; map fault guard/invalid flags/size/newBuffer cleanup");
}
