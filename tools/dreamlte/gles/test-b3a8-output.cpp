#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#define DREAM_B3A8_VECTOR_TEST 1
#include "dream_b3a8_vector.h"
using GLint=int;using GLuint=unsigned;using GLenum=unsigned;using GLsizei=int;using GLbitfield=unsigned;
constexpr unsigned GL_PACK_ROW_LENGTH=1, GL_PACK_ALIGNMENT=2, GL_READ_FRAMEBUFFER_BINDING=3,
 GL_DRAW_FRAMEBUFFER_BINDING=4, GL_READ_FRAMEBUFFER=5, GL_DRAW_FRAMEBUFFER=6,
 GL_FRAMEBUFFER=7, GL_COLOR_ATTACHMENT0=8, GL_TEXTURE_2D=9, GL_COLOR_BUFFER_BIT=10,
 GL_NEAREST=11, GL_RGBA=12, GL_BGRA=13, GL_UNSIGNED_BYTE=14, kIOSurfaceLockReadOnly=1;
struct Surface {uint8_t bytes[128];size_t offset[2],row[2],alloc;unsigned w,h;bool locked,fail_lock;};
using IOSurfaceRef=Surface*;
static unsigned format=0x62336138;
static uint8_t *IOSurfaceGetBaseAddress(Surface*s){return s->bytes;}
static void *IOSurfaceGetBaseAddressOfPlane(Surface*s,unsigned p){return s->bytes+s->offset[p];}
static size_t IOSurfaceGetAllocSize(Surface*s){return s->alloc;}
static size_t IOSurfaceGetBytesPerRowOfPlane(Surface*s,unsigned p){return s->row[p];}
static size_t IOSurfaceGetBytesPerRow(Surface*s){return s->row[0];}
static size_t IOSurfaceGetWidthOfPlane(Surface*s,unsigned){return s->w;}
static size_t IOSurfaceGetHeightOfPlane(Surface*s,unsigned){return s->h;}
static unsigned IOSurfaceGetPlaneCount(Surface*){return 2;}
static unsigned IOSurfaceGetPixelFormat(Surface*){return format;}
static unsigned IOSurfaceGetID(Surface*){return 42;}
static int IOSurfaceLock(Surface*s,unsigned,void*){if(s->fail_lock)return 1;assert(!s->locked);s->locked=true;return 0;}
static int IOSurfaceUnlock(Surface*s,unsigned,void*){assert(s->locked);s->locked=false;return 0;}
static int row_state=17,align_state=8,framebuffer=23,reads,errors;
static unsigned fbo_status=0x8cd5,read_error;
static void glGetIntegerv(unsigned p,int*v){*v=p==GL_PACK_ROW_LENGTH?row_state:p==GL_PACK_ALIGNMENT?align_state:framebuffer;}
static void glPixelStorei(unsigned p,int v){if(p==GL_PACK_ROW_LENGTH)row_state=v;else align_state=v;}
static void glGenFramebuffers(int,unsigned*v){*v=19;}
static void glBindFramebuffer(unsigned,unsigned v){framebuffer=(int)v;}
static void glFramebufferTexture2D(unsigned,unsigned,unsigned,unsigned,int){}
static void glBlitFramebuffer(int,int,int,int,int,int,int,int,unsigned,unsigned){}
static unsigned glCheckFramebufferStatus(unsigned){return fbo_status;}
static unsigned glGetError(){return reads?read_error:0;}
static void glReadPixels(int,int,int w,int h,unsigned f,unsigned t,void*buf){
 assert(f==GL_RGBA&&t==GL_UNSIGNED_BYTE&&row_state==0&&align_state==1);++reads;
 uint8_t*p=(uint8_t*)buf;
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  *p++=(uint8_t)(x?255:0);*p++=(uint8_t)(y?255:0);*p++=31;*p++=(uint8_t)(x?128:255);
 }
}
static void say(const char *f,...){if(strstr(f,"failed"))++errors;}
static uint64_t now_us(){static uint64_t n;return n+=100;}
struct eagl{unsigned reader,writer,framebuffer;int b3a8_probe=0;};
struct image{IOSurfaceRef surface;bool drawn,shared,b3a8,invert;unsigned mirror,texture;int width,height;IOSurfaceRef mirror_surface=nullptr;};
static bool b3a8_mirror_output(eagl*,image*){return false;}
#include "B3a8Planes.production.inc"
#define malloc(n) ((uint8_t*)std::malloc(n))
#include "B3a8Target.production.inc"
#undef malloc
static Surface make_surface(){Surface s{};memset(s.bytes,0x5a,sizeof s.bytes);s.alloc=128;s.offset[0]=8;s.offset[1]=80;s.row[0]=16;s.row[1]=8;s.w=2;s.h=2;return s;}
int main(){
 for(int flip=0;flip<2;flip++){
  Surface s=make_surface();eagl e{0,0,23};image i{&s,true,false,true,(bool)flip,0,44,2,2};
  reads=0;read_error=0;fbo_status=0x8cd5;target_to_surface(&e,&i);
  assert(reads==1&&!s.locked&&!i.drawn&&row_state==17&&align_state==8&&framebuffer==23);
  uint8_t decoded[16];assert(dream_b3a8_unpack(decoded,16,8,s.bytes+8,72,16,s.bytes+80,48,8,2,2,flip));
  const uint8_t expected[]={31,0,0,255,31,0,255,128,31,255,0,255,31,255,255,128};
  assert(!memcmp(decoded,expected,16));
  for(unsigned at=0;at<128;at++)if(!((at>=8&&at<16)||(at>=24&&at<32)||(at>=80&&at<82)||(at>=88&&at<90)))assert(s.bytes[at]==0x5a);
 }
 for(int fault=0;fault<5;fault++){
  Surface s=make_surface();eagl e{0,0,23};image i{&s,true,false,true,false,0,44,2,2};
  reads=0;read_error=0;fbo_status=0x8cd5;
  if(fault==0)fbo_status=0x8cd6;
  if(fault==1)read_error=0x502;
  if(fault==2)s.offset[1]=12;
  if(fault==3)s.alloc=88;
  if(fault==4)s.fail_lock=true;
  target_to_surface(&e,&i);
  for(auto b:s.bytes)assert(b==0x5a);
  assert(!s.locked&&row_state==17&&align_state==8&&framebuffer==23);
 }
 assert(errors==2);puts("PASS: production target/readback; both planes; RGBA/BGRA order; inversion; GL state; FBO/read errors; overlap/truncation/lock failures");
}
