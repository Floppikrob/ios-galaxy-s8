#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include "dream_b3a8.h"
constexpr int kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNotPermitted=2,kIOReturnNotFound=3;
constexpr unsigned kIOSurfaceLockReadOnly=1;
constexpr unsigned SWEET_FB_FORMAT_BGRA=0x42475241;
using IOReturn=int;
struct Memory{virtual~Memory(){};std::vector<uint8_t>data;uint64_t getLength(){return data.size();}};
struct IOBufferMemoryDescriptor:Memory{void*getBytesNoCopy(){return data.data();}};
#define OSDynamicCast(type,p) dynamic_cast<type*>(p)
#define OSSafeReleaseNULL(p) do{if(p){(p)->release();(p)=nullptr;}}while(false)
struct sweet_surface_plane{unsigned width,height,bytesPerRow,offset,size;};
struct SweetSurface{
 uint32_t cpuWriteOffered=0,displayMirrorReadOnlySeed=0;bool displayMirrorReadOnly=false,userMemoryPrepared=false;
 uint64_t allocSize=0;
 SweetSurface*displayMirror=nullptr;uint32_t displayMirrorSourceSeed=0,displayMirrorSeed=0;uint64_t displayMirrorSeq=0;
 unsigned pixelFormat=0,planeCount=0,width=0,height=0,bytesPerRow=0,id=0,refs=1,offset=0,bytesPerElement=4,elementWidth=1,elementHeight=1;
 unsigned seed=1;bool state=true;uint64_t maliSeq=0;Memory*memory=nullptr;sweet_surface_plane planes[2]{};
 uintptr_t kernelAddress(){return memory?(uintptr_t)memory->data.data():0;}
 void retain(){assert(refs);++refs;}
 void release(){assert(refs);if(!--refs)OSSafeReleaseNULL(displayMirror);}
};
static std::map<unsigned,SweetSurface*> table;
static SweetSurface*sweet_surface_copy(unsigned id){auto p=table.find(id);if(p==table.end())return nullptr;p->second->retain();return p->second;}
static bool sweet_surface_read_seed(SweetSurface*s,uint32_t*out){if(!s||!s->state)return false;*out=s->seed;return true;}
static int gMaliLock=1;static bool locked,ready=true;static uint64_t done=10;
static void IOLockLock(int){assert(!locked);locked=true;}
static void IOLockUnlock(int){assert(locked);locked=false;}
static bool hakios_mali_ready(){return ready;}
static bool hakios_mali_wait(uint64_t seq,uint64_t timeout){assert(locked&&!timeout);return ready&&seq<=done;}
static const char*name="backboardd",*identity="com.apple.backboardd";
static void proc_selfname(char*dst,size_t n){snprintf(dst,n,"%s",name);}
static void*current_proc(){return nullptr;}
static const char*csproc_get_identity(void*){return identity;}
static void IOLog(const char*,...){}
#include "DreamDisplayMirror.inc"
#include "DreamSurfaceCpu.h"
struct mali_buffer{Memory*memory;SweetSurface*surface;uint64_t size,last_seq=0;bool writeCombined=false,cpuMapped=false,cachePrimed=false,gpuOnlyHeap=false;};
static unsigned cleans,invalidates;
static void dm_clean(void*,uint64_t){++cleans;}static void dm_invalidate(void*,uint64_t){++invalidates;}
static constexpr uint64_t NSEC_PER_SEC=1000000000;
#include "DreamPanfrostCache.inc"
static SweetSurface *sweet_surface_held(int,int,uint32_t id){return sweet_surface_copy(id);}
static IOReturn sweet_surface_seed_out(SweetSurface*s,void*out,bool bump){if(bump)++s->seed;if(out)*(uint32_t*)out=s->seed;return 0;}
class IOSurfaceRootUserClient{public:int fLock=0,fRefs=0;
 IOReturn trapLock(void*,void*,void*,void*,void*,void*);
 IOReturn trapUnlock(void*,void*,void*,void*,void*,void*);
};
#include "CPULocks.production.inc"

static int register_mirror(dream_display_mirror_request q){IOLockLock(gMaliLock);int ret=dream_mirror_register(&q);IOLockUnlock(gMaliLock);return ret;}
static int poison;
static int dream_gpu_frame_copy(SweetSurface*,SweetSurface*,unsigned,unsigned,unsigned,unsigned){return poison?-1:0;}
static SweetSurface*sweet_surface_create_buffer(unsigned w,unsigned h,unsigned row,unsigned fmt){
 auto*s=new SweetSurface;s->memory=new IOBufferMemoryDescriptor;s->memory->data.resize(row*h);
 s->width=w;s->height=h;s->bytesPerRow=row;s->pixelFormat=fmt;return s;
}
#include "DreamBgraStage.h"
static uint64_t mach_absolute_time(){static uint64_t t;return ++t;}
static void absolutetime_to_nanoseconds(uint64_t n,uint64_t*out){*out=n;}
#include "B3a8Display.production.inc"
static void pixels_equal(SweetSurface*got,const std::vector<uint8_t>&expected,unsigned w,unsigned h){
 for(unsigned y=0;y<h;y++)assert(!memcmp(got->memory->data.data()+y*got->bytesPerRow,expected.data()+y*w*4,w*4));
}
int main(){
 const unsigned W=17,H=9,RR=80,AR=32,BR=96,AO=RR*H+64;
 IOBufferMemoryDescriptor originalMemory,mirrorMemory,otherMemory;
 originalMemory.data.resize(AO+AR*H,0xcd);mirrorMemory.data.resize(BR*H,0xe7);otherMemory.data.resize(BR*H,0xe7);
 SweetSurface source,mirror,other;
 source.id=1;source.pixelFormat=0x62336138;source.planeCount=2;source.width=W;source.height=H;source.bytesPerRow=RR;source.memory=&originalMemory;
 source.planes[0]={W,H,RR,0,RR*H};source.planes[1]={W,H,AR,AO,AR*H};
 mirror.id=2;mirror.pixelFormat=SWEET_FB_FORMAT_BGRA;mirror.width=W;mirror.height=H;mirror.bytesPerRow=BR;mirror.memory=&mirrorMemory;mirror.maliSeq=10;mirror.seed=0;mirror.allocSize=BR*H;
 other=mirror;other.id=3;other.memory=&otherMemory;
 table={{1,&source},{2,&mirror},{3,&other}};
 std::vector<uint8_t> expected(W*H*4);
 for(unsigned i=0;i<expected.size();i++)expected[i]=(i*71+i/7)&255;
 for(unsigned y=0;y<H;y++)memcpy(mirrorMemory.data.data()+y*BR,expected.data()+y*W*4,W*4);
 assert(dream_b3a8_pack(originalMemory.data.data(),RR*H,RR,originalMemory.data.data()+AO,AR*H,AR,expected.data(),expected.size(),W*4,W,H,0));
 const auto authoritative=originalMemory.data;
 assert(!dream_surface_gpu_readonly(&mirror)); // Actual BGRA seed0 must NOT qualify without signed registration.
 mali_buffer unregistered{&mirrorMemory,&mirror,BR*H};flushSubmit(&unregistered);flushComplete(&unregistered);
 assert(cleans==1&&invalidates==1&&dpf_mirror_clean_skips==0);
 const unsigned baselineCleans=cleans,baselineInvalidates=invalidates;
 dream_display_mirror_request q{1,2,source.seed,mirror.seed,W,H,0,0};
 assert(register_mirror(q)==0&&source.refs==1&&mirror.refs==2&&!locked);
 assert(dream_surface_gpu_readonly(&mirror));
 mali_buffer bo{&mirrorMemory,&mirror,BR*H};
 auto skipped=dpf_mirror_clean_skips;flushSubmit(&bo);
 assert(cleans==baselineCleans&&dpf_mirror_clean_skips==skipped+1&&dpf_mirror_clean_skip_bytes==BR*H);
 flushComplete(&bo);assert(invalidates==baselineInvalidates+1); // Postinvalidate is still full.
 ++mirror.seed;assert(!dream_surface_gpu_readonly(&mirror));flushSubmit(&bo);assert(cleans==baselineCleans+1);--mirror.seed;
 mirror.userMemoryPrepared=true;assert(!dream_surface_gpu_readonly(&mirror));mirror.userMemoryPrepared=false;
 IOSurfaceRootUserClient cpu;
 assert(cpu.trapLock((void*)2,(void*)1,nullptr,nullptr,nullptr,nullptr)==0&&mirror.cpuWriteOffered==0);
 assert(cpu.trapUnlock((void*)2,(void*)1,nullptr,nullptr,nullptr,nullptr)==0&&dream_surface_gpu_readonly(&mirror));
 assert(cpu.trapLock((void*)2,nullptr,nullptr,nullptr,nullptr,nullptr)==0&&mirror.cpuWriteOffered==1);
 assert(!dream_surface_gpu_readonly(&mirror));
 assert(register_mirror(q)==0&&!mirror.displayMirrorReadOnly); // Write exposure cannot be cleared by re-registration.
 flushSubmit(&bo);assert(cleans==baselineCleans+2);
 assert(cpu.trapUnlock((void*)2,nullptr,nullptr,nullptr,nullptr,nullptr)==0&&mirror.cpuWriteOffered==1);
 q.mirrorSeed=mirror.seed;assert(register_mirror(q)==0&&!dream_surface_gpu_readonly(&mirror));
 auto*got=dream_b3a8_for_display(&source,W,H);assert(got==dream_b3a8_stage&&got!=&mirror&&mirror.refs==2);
 pixels_equal(got,expected,W,H);got->release();assert(originalMemory.data==authoritative);
 for(unsigned reason=0;reason<6;reason++){
  source.seed=q.originalSeed;mirror.seed=q.mirrorSeed;mirror.maliSeq=10;done=10;
  if(reason==0)++source.seed;if(reason==1)++mirror.seed;if(reason==2)mirror.maliSeq=11;
  if(reason==3)mirror.maliSeq=~0ULL;if(reason==4)done=9;if(reason==5)mirror.state=false;
  got=dream_b3a8_for_display(&source,W,H);assert(got&&got!=&mirror);
  pixels_equal(got,expected,W,H);got->release();mirror.state=true;
 }
 source.seed=q.originalSeed;mirror.seed=q.mirrorSeed;mirror.maliSeq=10;done=10;
 for(unsigned reason=0;reason<10;reason++){
  auto request=q;name="backboardd";identity="com.apple.backboardd";
  if(reason==0)name="SpringBoard";if(reason==1)identity=nullptr;if(reason==2)identity="forged.backboardd";
  if(reason==3)request.flags=1;if(reason==4)request.reserved=1;if(reason==5)request.width++;
  if(reason==6)request.originalSeed++;if(reason==7)request.mirrorSeed++;
  if(reason==8)request.original=999;if(reason==9)request.mirror=1;
  assert(register_mirror(request)!=0&&source.displayMirror==&mirror&&mirror.refs==2);
 }
 name="backboardd";identity="com.apple.backboardd";
 mirror.offset=4;assert(register_mirror(q)!=0);mirror.offset=0;
 mirror.maliSeq=0;assert(register_mirror(q)!=0);mirror.maliSeq=~0ULL;assert(register_mirror(q)!=0);mirror.maliSeq=10;
 mirror.bytesPerRow=4;assert(register_mirror(q)!=0);mirror.bytesPerRow=BR;
 ready=false;assert(!dream_display_mirror_copy(&source,W,H));ready=true;
 auto bad=source;bad.planes[1].offset=4;assert(!dream_b3a8_for_display(&bad,W,H));
 poison=1;assert(!dream_b3a8_for_display(&source,W,H));poison=0;
 auto replacement=q;replacement.mirror=3;replacement.mirrorSeed=other.seed;assert(register_mirror(replacement)==0);
 assert(mirror.refs==1&&other.refs==2&&source.displayMirror==&other);
 got=dream_display_mirror_copy(&source,W,H);assert(got==&other&&other.refs==3);got->release();
 source.release();assert(other.refs==1);assert(originalMemory.data==authoritative&&!locked);
 delete dream_b3a8_stage->memory;delete dream_b3a8_stage;dream_b3a8_stage=nullptr;
 puts("PASS actual CPU Lock/Unlock + registered mirror PREclean ownership/postinvalidate; mirror registration/display: pixels+alpha unchanged, original bytes preserved, exact seeds/fence, stale/pending/error fallback, signed gate, layout bounds and retained replacement/free");
}
