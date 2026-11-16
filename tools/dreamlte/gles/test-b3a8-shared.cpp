#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <map>
#include <vector>
#include <algorithm>
#define DREAM_B3A8_VECTOR_TEST 1
#include "dream_b3a8_vector.h"
using GLint=int;using GLuint=unsigned;using GLenum=unsigned;using GLsizei=int;using GLboolean=bool;
constexpr unsigned GL_TEXTURE_BINDING_2D=1,GL_TEXTURE_2D=2,GL_READ_FRAMEBUFFER_BINDING=3,
 GL_DRAW_FRAMEBUFFER_BINDING=4,GL_READ_FRAMEBUFFER=5,GL_DRAW_FRAMEBUFFER=6,GL_COLOR_ATTACHMENT0=7,
 GL_FRAMEBUFFER_COMPLETE=8,GL_COLOR_BUFFER_BIT=9,GL_NEAREST=10,GL_UNPACK_ROW_LENGTH=11,
 GL_UNPACK_ALIGNMENT=12,GL_RGBA=13,GL_UNSIGNED_BYTE=14,GL_BGRA=15,kIOSurfaceLockReadOnly=1;
struct Object{virtual ~Object(){};};
struct Surface:Object{std::vector<uint8_t>bytes;size_t row[2]{},offset[2]{};unsigned w=0,h=0,planes=0,fmt='BGRA',id=0;bool locked=false,fail_lock=false;};
using IOSurfaceRef=Surface*;using CFNumberRef=Object*;using CFDictionaryRef=Object*;
struct Number:Object{int64_t value;Number(int64_t n):value(n){}};
struct Dict:Object{int64_t values[5];};
#define CFSTR(s) (s)
constexpr int kCFNumberSInt64Type=1,kCFTypeDictionaryKeyCallBacks=1,kCFTypeDictionaryValueCallBacks=1;
static Object*CFNumberCreate(void*,int,const int64_t*n){return new Number(*n);}
static Object*CFDictionaryCreate(void*,const void**,const void**values,int,const int*,const int*){
 auto*d=new Dict;for(int i=0;i<5;i++)d->values[i]=((const Number*)values[i])->value;return d;
}
static std::map<unsigned,Surface*>surfaces;static unsigned next_id=50;static int alive;
static Surface*IOSurfaceCreate(Object*o){auto*d=(Dict*)o;auto*s=new Surface;s->w=d->values[0];s->h=d->values[1];s->row[0]=d->values[3];s->fmt=d->values[4];s->bytes.resize(s->row[0]*s->h,0xa5);s->id=++next_id;surfaces[s->id]=s;alive++;return s;}
static void CFRelease(Object*o){if(auto*s=dynamic_cast<Surface*>(o)){assert(!s->locked);surfaces.erase(s->id);alive--;}delete o;}
static unsigned IOSurfaceGetID(Surface*s){return s->id;}
static void*IOSurfaceGetBaseAddress(Surface*s){return s->bytes.data();}
static void*IOSurfaceGetBaseAddressOfPlane(Surface*s,unsigned p){return s->bytes.data()+s->offset[p];}
static size_t IOSurfaceGetAllocSize(Surface*s){return s->bytes.size();}
static size_t IOSurfaceGetBytesPerRow(Surface*s){return s->row[0];}
static size_t IOSurfaceGetBytesPerRowOfPlane(Surface*s,unsigned p){return s->row[p];}
static size_t IOSurfaceGetWidth(Surface*s){return s->w;}
static size_t IOSurfaceGetHeight(Surface*s){return s->h;}
static size_t IOSurfaceGetWidthOfPlane(Surface*s,unsigned){return s->w;}
static size_t IOSurfaceGetHeightOfPlane(Surface*s,unsigned){return s->h;}
static unsigned IOSurfaceGetPlaneCount(Surface*s){return s->planes;}
static unsigned IOSurfaceGetPixelFormat(Surface*s){return s->fmt;}
static int IOSurfaceLock(Surface*s,unsigned,void*){if(s->fail_lock)return 1;assert(!s->locked);s->locked=true;return 0;}
static int IOSurfaceUnlock(Surface*s,unsigned,void*){assert(s->locked);s->locked=false;return 0;}
struct Texture{Surface*surface=nullptr;std::vector<uint8_t>rgba;int w=0,h=0;};
static std::map<unsigned,Texture>textures;static std::map<unsigned,unsigned>fbos;
static unsigned bound=21,read_fbo=22,draw_fbo=23,next_gl=100;
static unsigned unpack_buffer=77;
static int row_state=17,align_state=8;static bool scissor=true,import_fail,corrupt,blit_fail,fbo_fail,finish_seen;
static unsigned gl_error,blits,finishes,reads;static std::vector<uint8_t>pending;static Surface*pending_surface;
static void glGetIntegerv(unsigned p,int*v){*v=p==GL_TEXTURE_BINDING_2D?bound:p==GL_READ_FRAMEBUFFER_BINDING?read_fbo:p==GL_DRAW_FRAMEBUFFER_BINDING?draw_fbo:p==GL_UNPACK_ROW_LENGTH?row_state:p==0x88ef?unpack_buffer:align_state;}
static void glGenTextures(int n,unsigned*v){for(int i=0;i<n;i++){v[i]=++next_gl;textures[v[i]]={};}}
static void glBindTexture(unsigned,unsigned v){bound=v;}
static void glDeleteTextures(int n,const unsigned*v){for(int i=0;i<n;i++)textures.erase(v[i]);}
static void glPixelStorei(unsigned p,int v){if(p==GL_UNPACK_ROW_LENGTH)row_state=v;else align_state=v;}
static void glBindBuffer(unsigned,unsigned v){unpack_buffer=v;}
static void glTexImage2D(unsigned,int,int,int w,int h,int,unsigned,unsigned,const void*p){assert(row_state==0&&align_state==1&&unpack_buffer==0);auto&t=textures[bound];t.w=w;t.h=h;t.rgba.assign((const uint8_t*)p,(const uint8_t*)p+w*h*4);}
static void glGenFramebuffers(int n,unsigned*v){for(int i=0;i<n;i++)v[i]=++next_gl;}
static void glBindFramebuffer(unsigned p,unsigned v){if(p==GL_READ_FRAMEBUFFER)read_fbo=v;else draw_fbo=v;}
static void glFramebufferTexture2D(unsigned p,unsigned,unsigned,unsigned t,int){fbos[p==GL_READ_FRAMEBUFFER?read_fbo:draw_fbo]=t;}
static void glDeleteFramebuffers(int n,const unsigned*v){for(int i=0;i<n;i++)fbos.erase(v[i]);}
static unsigned glCheckFramebufferStatus(unsigned){return fbo_fail?0:GL_FRAMEBUFFER_COMPLETE;}
static bool glIsEnabled(unsigned){return scissor;}
static void glDisable(unsigned){scissor=false;}
static void glEnable(unsigned){scissor=true;}
static unsigned glGetError(){unsigned e=gl_error;gl_error=0;return e;}
static void glBlitFramebuffer(int,int,int w,int h,int,int y0,int,int y1,unsigned,unsigned){
 assert(!scissor);blits++;finish_seen=false;
 if(blit_fail){gl_error=0x502;return;}
 auto&src=textures[fbos[read_fbo]];Surface*dst=textures[fbos[draw_fbo]].surface;assert(dst&&src.w==w&&src.h==h);
 pending=dst->bytes;pending_surface=dst;
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  const uint8_t*p=src.rgba.data()+((y0>y1?h-1-y:y)*w+x)*4;uint8_t*q=pending.data()+y*dst->row[0]+x*4;
  q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=p[3];
 }
 if(corrupt)pending[0]^=1;
}
static void glFinish(){finishes++;finish_seen=true;if(pending_surface){pending_surface->bytes=pending;pending_surface=nullptr;}}
static bool hakios_gl_surface_texture(void*,unsigned id){if(import_fail)return false;auto&t=textures[bound];t.surface=surfaces.at(id);return true;}
static void say(const char*,...){}
static uint64_t now_us(){static uint64_t n;return n+=100;}
static int blit_targets=1,share_surfaces=1;static uint64_t image_bytes_most=512ull<<20;
struct eagl{void*gl=nullptr;int b3a8_probe=0;uint64_t image_bytes=0;};
struct image{Surface*surface;Surface*mirror_surface=nullptr;bool target=true,alpha_first=false;unsigned plane=0;int width,height;unsigned format=GL_BGRA,type=GL_UNSIGNED_BYTE,mirror=0,texture=0;size_t bytes=0;bool invert=false;};
#include "b3a8-shared.inc"
#include "B3a8Planes.production.inc"
static unsigned offers;
static void dream_offer_display_mirror(IOSurfaceRef s,IOSurfaceRef m,unsigned w,unsigned h){assert(!s->locked&&!m->locked&&finish_seen&&w==s->w&&h==s->h);++offers;}
#include "B3a8Mirror.production.inc"
static void state(){assert(bound==21&&read_fbo==22&&draw_fbo==23&&scissor&&row_state==17&&align_state==8&&unpack_buffer==77);assert(fbos.empty());}
static Surface make_original(){Surface s;s.w=3;s.h=2;s.planes=2;s.fmt='b3a8';s.bytes.resize(96,0x5a);s.offset[0]=8;s.offset[1]=64;s.row[0]=16;s.row[1]=8;return s;}
int main(){
 eagl e;
 assert(b3a8_shared_probe(&e)&&e.b3a8_probe==1&&blits==2&&finishes==2&&alive==0&&textures.empty());state();
 unsigned b=blits;assert(b3a8_shared_probe(&e)&&blits==b);
 for(int fault=0;fault<4;fault++){
  eagl broken;import_fail=fault==0;corrupt=fault==1;blit_fail=fault==2;fbo_fail=fault==3;
  assert(!b3a8_shared_probe(&broken)&&broken.b3a8_probe==-1);state();assert(alive==0&&textures.empty());
  b=blits;assert(!b3a8_shared_probe(&broken)&&blits==b);
 }
 import_fail=corrupt=blit_fail=fbo_fail=false;
 for(int flip=0;flip<2;flip++){
  Surface orig=make_original();image i{&orig};i.width=3;i.height=2;i.bytes=orig.bytes.size();i.invert=flip;e.image_bytes=i.bytes;
  assert(b3a8_make_mirror(&e,&i)&&alive==1&&e.image_bytes==i.bytes);state();
  glGenTextures(1,&i.texture);auto&t=textures[i.texture];t.w=3;t.h=2;t.rgba={1,2,3,4,11,22,33,44,5,6,7,8,9,10,11,12,55,66,77,88,13,14,15,16};
  assert(b3a8_mirror_output(&e,&i)&&finish_seen&&!orig.locked&&!i.mirror_surface->locked);state();
  uint8_t decoded[24];assert(dream_b3a8_unpack(decoded,24,12,orig.bytes.data()+8,56,16,orig.bytes.data()+64,32,8,3,2,flip));
  for(unsigned p=0;p<6;p++){assert(decoded[p*4]==t.rgba[p*4+2]&&decoded[p*4+1]==t.rgba[p*4+1]&&decoded[p*4+2]==t.rgba[p*4]&&decoded[p*4+3]==t.rgba[p*4+3]);}
  assert(offers==(unsigned)(flip+1));auto saved=orig.bytes;
  for(int fault=0;fault<4;fault++){
   blit_fail=fault==0;orig.fail_lock=fault==1;i.mirror_surface->fail_lock=fault==2;orig.offset[1]=fault==3?12:64;
   assert(!b3a8_mirror_output(&e,&i)&&offers==(unsigned)(flip+1)&&orig.bytes==saved&&!orig.locked&&!i.mirror_surface->locked);state();
  }
  blit_fail=false;glDeleteTextures(1,&i.mirror);glDeleteTextures(1,&i.texture);CFRelease(i.mirror_surface);assert(alive==0&&textures.empty());
 }
 Surface orig=make_original();image i{&orig};i.width=3;i.height=2;e.image_bytes=image_bytes_most;
 assert(!b3a8_make_mirror(&e,&i)&&!i.mirror&&!i.mirror_surface&&alive==0);
 assert(reads==0);puts("PASS: actual shared probe/blit/encode; BGRA and alpha; odd padded stride; both orientations; completion before CPU; state restoration; import/color/FBO/GL/lock/bounds failures; quota and cleanup; no readpixels");
}
