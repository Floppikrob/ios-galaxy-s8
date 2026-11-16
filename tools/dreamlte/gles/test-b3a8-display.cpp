#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include "dream_b3a8.h"
struct Memory{std::vector<uint8_t> data;uint64_t getLength(){return data.size();}};
struct sweet_surface_plane{unsigned width,height,bytesPerRow,offset,size;};
struct SweetSurface{
 unsigned pixelFormat=0,planeCount=0,width=0,height=0,bytesPerRow=0,id=42,refs=1;
 Memory *memory=nullptr;sweet_surface_plane planes[2]{};
 uintptr_t kernelAddress(){return memory?(uintptr_t)memory->data.data():0;}
 void retain(){++refs;}
 void release(){assert(refs);--refs;}
};
static int poison;
static int dream_gpu_frame_copy(SweetSurface*,SweetSurface*,unsigned,unsigned,unsigned,unsigned){return poison?-1:0;}
static void IOLog(const char*,...){}
static constexpr unsigned SWEET_FB_FORMAT_BGRA=0x42475241;
static SweetSurface* sweet_surface_create_buffer(unsigned w,unsigned h,unsigned row,unsigned fmt){
 auto*s=new SweetSurface;s->memory=new Memory;s->memory->data.resize(row*h);s->width=w;s->height=h;s->bytesPerRow=row;s->pixelFormat=fmt;return s;
}
static SweetSurface *dream_display_mirror_copy(SweetSurface*,unsigned,unsigned){return nullptr;}
#include "DreamBgraStage.h"
static uint64_t mach_absolute_time(){static uint64_t t;return ++t;}
static void absolutetime_to_nanoseconds(uint64_t n,uint64_t*out){*out=n;}
#include "B3a8Display.production.inc"
int main(){
 Memory m;m.data.resize(128,0xac);SweetSurface s;s.memory=&m;s.pixelFormat=0x62336138;s.width=s.height=2;s.planeCount=2;
 s.planes[0]={2,2,16,8,32};s.planes[1]={2,2,8,80,16};
 const uint8_t colors[]={0,0,255,255,255,0,0,31,0,255,0,127,255,255,255,255};
 assert(dream_b3a8_pack(m.data.data()+8,32,16,m.data.data()+80,16,8,colors,16,8,2,2,0));
 auto*stage=dream_b3a8_for_display(&s,2,2);assert(stage&&stage!=&s&&stage->refs==2&&stage->pixelFormat==SWEET_FB_FORMAT_BGRA);
 assert(!memcmp(stage->memory->data.data(),colors,8));assert(!memcmp(stage->memory->data.data()+stage->bytesPerRow,colors+8,8));
 auto saved=stage->memory->data;
 for(int fault=0;fault<7;fault++){
  auto invalid=s;poison=0;
  if(fault==0)poison=1;
  if(fault==1)invalid.planes[1].offset=20;
  if(fault==2)invalid.planes[0].size=15;
  if(fault==3)invalid.planes[1].offset=124;
  if(fault==4)invalid.width=3;
  if(fault==5)invalid.planeCount=1;
  if(fault==6)invalid.memory=nullptr;
  assert(!dream_b3a8_for_display(&invalid,2,2));assert(stage->memory->data==saved&&stage->refs==2);
 }
 poison=0;s.pixelFormat=SWEET_FB_FORMAT_BGRA;assert(dream_b3a8_for_display(&s,2,2)==&s&&s.refs==1);
 delete stage->memory;delete stage;dream_b3a8_stage=nullptr;
 puts("PASS: production display bridge; RGB10+A8 -> BGRA; retained stage; poison guard; malformed plane/size/overlap rejection; BGRA unchanged");
}
