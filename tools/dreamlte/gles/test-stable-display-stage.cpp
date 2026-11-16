#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <vector>
#include "DreamB3a8.h"
#include "DreamBgraStage.h"
struct Memory{std::vector<uint8_t>data;uint64_t getLength(){return data.size();}};
struct sweet_surface_plane{unsigned width,height,bytesPerRow,offset,size;};
struct SweetSurface{
 unsigned pixelFormat=0,planeCount=0,width=0,height=0,bytesPerRow=0,id=0,refs=1;
 Memory*memory=nullptr;sweet_surface_plane planes[2]{};
 uintptr_t kernelAddress(){return memory?(uintptr_t)memory->data.data():0;}
 void retain(){++refs;}void release(){assert(refs);--refs;}
};
constexpr unsigned SWEET_FB_FORMAT_BGRA=0x42475241;
static int poison;
static unsigned stageAllocations;
static SweetSurface*nextMirror;
static int dream_gpu_frame_copy(SweetSurface*,SweetSurface*,unsigned,unsigned,unsigned,unsigned){return poison?-1:0;}
static void IOLog(const char*,...){}
static uint64_t mach_absolute_time(){static uint64_t t;return ++t;}
static void absolutetime_to_nanoseconds(uint64_t n,uint64_t*out){*out=n;}
static SweetSurface*sweet_surface_create_buffer(unsigned w,unsigned h,unsigned row,unsigned fmt){
 ++stageAllocations;auto*s=new SweetSurface;s->memory=new Memory;s->memory->data.resize(row*h,0xad);
 s->width=w;s->height=h;s->bytesPerRow=row;s->pixelFormat=fmt;s->id=999;return s;
}
static SweetSurface*dream_display_mirror_copy(SweetSurface*,unsigned,unsigned){if(nextMirror)nextMirror->retain();return nextMirror;}
#include "Display.production.inc"
int main(){
 const unsigned W=17,H=9,RR=80,AR=32,BR=96,AO=RR*H+64;
 Memory original;original.data.resize(AO+AR*H,0xcd);
 SweetSurface source;source.pixelFormat=0x62336138;source.planeCount=2;source.width=W;source.height=H;source.bytesPerRow=RR;source.memory=&original;
 source.planes[0]={W,H,RR,0,RR*H};source.planes[1]={W,H,AR,AO,AR*H};
 std::vector<std::unique_ptr<Memory>> memories;
 std::vector<std::unique_ptr<SweetSurface>> mirrors;
 std::set<std::pair<uintptr_t,unsigned>> contexts{{1,0},{2,0}}; // Two retained startup/probe contexts.
 std::set<uintptr_t> gpuSources;
 for(unsigned frame=0;frame<80;frame++){
  memories.emplace_back(new Memory);auto&memory=*memories.back();memory.data.resize(BR*H,0xe7);
  mirrors.emplace_back(new SweetSurface);auto&mirror=*mirrors.back();mirror.memory=&memory;mirror.bytesPerRow=BR;mirror.width=W;mirror.height=H;mirror.pixelFormat=SWEET_FB_FORMAT_BGRA;mirror.id=frame+1;
  std::vector<uint8_t> expected(W*H*4);
  for(unsigned i=0;i<expected.size();i++)expected[i]=(i*71+frame*17+i/7)&255;
  for(unsigned y=0;y<H;y++)memcpy(memory.data.data()+y*BR,expected.data()+y*W*4,W*4);
  assert(dream_b3a8_pack(original.data.data(),RR*H,RR,original.data.data()+AO,AR*H,AR,expected.data(),expected.size(),W*4,W,H,0));
  auto authoritative=original.data;
  nextMirror=frame%7?&mirror:nullptr; // Include old CPUdecode fallback among valid mirrors.
  auto*display=dream_b3a8_for_display(&source,W,H);assert(display);
  for(unsigned target=0;target<3;target++){ // boot + two alternating private scanouts: actual cache keys need BOTH ends.
   auto key=std::make_pair((uintptr_t)display->memory,target);
   assert((contexts.count(key)||contexts.size()<8)&&"recreated mirror sources exhausted the eight-AS pool");
   contexts.insert(key);
  }
  gpuSources.insert((uintptr_t)display->memory);
  for(unsigned y=0;y<H;y++)assert(!memcmp(display->memory->data.data()+y*display->bytesPerRow,expected.data()+y*W*4,W*4));
  assert(original.data==authoritative);display->release();assert(mirror.refs==1);
 }
 assert(stageAllocations==1&&gpuSources.size()==1&&contexts.size()==5); // No eviction, remap or increased limit.
 uint8_t from[64],to[64];memset(from,0x31,sizeof(from));memset(to,0xcd,sizeof(to));
 assert(dream_bgra_stage_rows(to,64,16,from,64,20,3,2));
 for(unsigned i=0;i<64;i++)assert(to[i]==((i<12||(i>=16&&i<28))?0x31:0xcd));
 auto saved=std::vector<uint8_t>(to,to+64);
 assert(!dream_bgra_stage_rows(to,27,16,from,64,20,3,2));
 assert(!dream_bgra_stage_rows(to,64,16,from,31,20,3,2));
 assert(!dream_bgra_stage_rows(to,64,8,from,64,20,3,2));
 assert(!dream_bgra_stage_rows(to,64,16,to+4,60,16,3,2)); // No memcpy overlap.
 assert(!dream_bgra_stage_rows(to,64,16,nullptr,64,20,3,2));
 assert(!dream_bgra_stage_rows(to,64,SIZE_MAX,from,64,20,3,2));
 assert(!dream_bgra_stage_rows((uint8_t*)(UINTPTR_MAX-3),64,16,from,64,20,3,2));
 assert(!memcmp(to,saved.data(),64)); // All rejection happens before any output write.
 delete dream_b3a8_stage->memory;delete dream_b3a8_stage;dream_b3a8_stage=nullptr;
 puts("PASS actual bridge:80 recreated mirrors x3 scanout destinations use ONE stable source and5/8 retained contexts; pixels/alpha/original/padding unchanged; row bounds/overflow/overlap rejected");
}
