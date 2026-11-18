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
static bool surface_alloc_fail,staging_lock_fail,storage_fail;
static Surface*IOSurfaceCreate(Object*o){if(surface_alloc_fail)return nullptr;auto*d=(Dict*)o;auto*s=new Surface;s->w=d->values[0];s->h=d->values[1];s->row[0]=d->values[3];s->fmt=d->values[4];s->bytes.resize(s->row[0]*s->h,0xa5);s->fail_lock=staging_lock_fail;s->id=++next_id;surfaces[s->id]=s;alive++;return s;}
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
static unsigned pending_texture;
static unsigned gl_error,blits,finishes;static std::vector<uint8_t>pending;
static void glGetIntegerv(unsigned p,int*v){*v=p==GL_TEXTURE_BINDING_2D?bound:p==GL_READ_FRAMEBUFFER_BINDING?read_fbo:p==GL_DRAW_FRAMEBUFFER_BINDING?draw_fbo:p==GL_UNPACK_ROW_LENGTH?row_state:p==0x88ef?unpack_buffer:align_state;}
static void glGenTextures(int n,unsigned*v){for(int i=0;i<n;i++){v[i]=++next_gl;textures[v[i]]={};}}
static void glBindTexture(unsigned,unsigned v){bound=v;}
static void glDeleteTextures(int n,const unsigned*v){for(int i=0;i<n;i++)textures.erase(v[i]);}
static void glPixelStorei(unsigned p,int v){if(p==GL_UNPACK_ROW_LENGTH)row_state=v;else align_state=v;}
static void glBindBuffer(unsigned,unsigned v){unpack_buffer=v;}
static void glTexImage2D(unsigned,int,int,int w,int h,int,unsigned,unsigned,const void*p){
 assert(!p&&row_state==0&&align_state==1&&unpack_buffer==0);
 if(storage_fail){gl_error=0x505;return;}auto&t=textures[bound];t.w=w;t.h=h;t.rgba.assign((size_t)w*h*4,0);
}
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
 auto&src=textures[fbos[read_fbo]];auto&dst=textures[fbos[draw_fbo]];
 assert(src.surface&&!src.surface->locked&&!dst.surface&&src.surface->w==(unsigned)w&&src.surface->h==(unsigned)h);
 assert(y0==0&&y1==h);pending=dst.rgba;pending_texture=fbos[draw_fbo];
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  const uint8_t*p=src.surface->bytes.data()+(size_t)y*src.surface->row[0]+x*4;
  uint8_t*q=pending.data()+((size_t)y*w+x)*4;
  q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=p[3];
 }
 if(corrupt)pending[0]^=1;
}
static void glFinish(){finishes++;finish_seen=true;if(pending_texture){textures[pending_texture].rgba=pending;pending_texture=0;}}
static bool hakios_gl_surface_texture(void*,unsigned id){if(import_fail)return false;auto&t=textures[bound];t.surface=surfaces.at(id);return true;}
static void say(const char*,...){}
static uint64_t now_us(){static uint64_t n;return n+=100;}
static int blit_targets=1,share_surfaces=1;static uint64_t image_bytes_most=512ull<<20;
struct eagl{unsigned long api=3;void*gl=nullptr;int b3a8_probe=0;uint64_t image_bytes=0;};
struct image{unsigned to=GL_TEXTURE_2D,internal=GL_BGRA;Surface*surface=nullptr;Surface*mirror_surface=nullptr;bool target=true,alpha_first=false;unsigned plane=0;int width,height;unsigned format=GL_BGRA,type=GL_UNSIGNED_BYTE,mirror=0,texture=0;size_t bytes=0;bool invert=false;};
#include "b3a8-shared.inc"

static GLenum internal_format(GLenum internal,GLenum format){return format==GL_BGRA&&internal==GL_RGBA?GL_BGRA:internal;}
#include "dream-staged-upload.inc"
static void state(){assert(bound==21&&read_fbo==22&&draw_fbo==23&&scissor&&row_state==17&&align_state==8&&unpack_buffer==0);assert(fbos.empty()&&alive==0&&textures.size()==1&&textures.count(21));}
int main(){
 eagl e;image i;i.texture=21;i.width=257;i.height=257;
 size_t row=(size_t)i.width*4+13;std::vector<uint8_t>src(row*i.height+16,0xa5),expected((size_t)i.width*i.height*4);
 for(unsigned format:{GL_RGBA,GL_BGRA})for(int flip=0;flip<2;flip++){
  for(int y=0;y<i.height;y++)for(int x=0;x<i.width;x++){
   int sy=flip?i.height-1-y:y;auto*p=src.data()+(size_t)y*row+x*4;auto*q=expected.data()+((size_t)y*i.width+x)*4;
   p[0]=(x*7+sy*3)&255;p[1]=(x*2+sy*11)&255;p[2]=(x*5+sy*13)&255;p[3]=(x*3+sy*17+29)&255;
   q[0]=p[format==GL_BGRA?2:0];q[1]=p[1];q[2]=p[format==GL_BGRA?0:2];q[3]=p[3];
  }
  i.format=format;i.internal=format;unpack_buffer=0;bound=21;textures[21]={};auto saved=src;
  assert(dream_staged_upload(&e,&i,src.data(),row,nullptr)&&finish_seen);state();assert(textures[21].rgba==expected&&src==saved);
 }
 for(int fault=0;fault<5;fault++){
  surface_alloc_fail=fault==0;staging_lock_fail=fault==1;import_fail=fault==2;storage_fail=fault==3;blit_fail=fault==4;
  assert(!dream_staged_upload(&e,&i,src.data(),row,nullptr));state();
 }
 surface_alloc_fail=staging_lock_fail=import_fail=storage_fail=blit_fail=false;
 unsigned before=blits;
 e.api=2;assert(!dream_staged_upload(&e,&i,src.data(),row,nullptr));e.api=3;
 i.width=2;i.height=2;assert(!dream_staged_upload(&e,&i,src.data(),row,nullptr));i.width=i.height=257;
 assert(!dream_staged_upload(&e,&i,src.data(),1,nullptr));
 i.type=0;assert(!dream_staged_upload(&e,&i,src.data(),row,nullptr));i.type=GL_UNSIGNED_BYTE;
 i.format=0;assert(!dream_staged_upload(&e,&i,src.data(),row,nullptr));i.format=GL_BGRA;
 i.internal=0;assert(!dream_staged_upload(&e,&i,src.data(),row,nullptr));i.internal=GL_BGRA;
 e.image_bytes=image_bytes_most;assert(!dream_staged_upload(&e,&i,src.data(),row,nullptr));e.image_bytes=0;
 share_surfaces=0;assert(!dream_staged_upload(&e,&i,src.data(),row,nullptr));share_surfaces=1;
 unpack_buffer=77;assert(!dream_staged_upload(&e,&i,src.data(),row,nullptr));unpack_buffer=0;
 bound=99;assert(!dream_staged_upload(&e,&i,src.data(),row,nullptr));bound=21;
 assert(!dream_staged_upload(&e,&i,nullptr,row,nullptr)&&blits==before);state();
 {
  const size_t rgb_row = i.width * 4 + 13, alpha_row = i.width + 5;
  std::vector<uint8_t> rgb(rgb_row * i.height, 0x1f), alpha(alpha_row * i.height, 0x78);
  for (size_t y = 0; y < (size_t)i.height; ++y) {
   for (size_t x = 0; x < (size_t)i.width; ++x) {
    const uint32_t code = ((x * 11 + y) & 1023) | (((x + y * 7) & 1023) << 10)
        | (((x + y * 21) & 1023) << 20);
    for (unsigned byte = 0; byte < 4; ++byte) rgb[y * rgb_row + 4 * x + byte] = code >> (8 * byte);
    alpha[y * alpha_row + x] = x + y * 13;
   }
  }
  const auto old_rgb = rgb, old_alpha = alpha;
  for (bool invert : {false, true}) {
   const dream_upload_planes planes = {rgb.data(), alpha.data(), rgb.size(), rgb_row,
       alpha.size(), alpha_row, invert};
   assert(dream_b3a8_unpack(expected.data(), expected.size(), i.width * 4,
       rgb.data(), rgb.size(), rgb_row, alpha.data(), alpha.size(), alpha_row,
       i.width, i.height, invert));
   for (size_t at = 0; at < expected.size(); at += 4) std::swap(expected[at], expected[at + 2]);
   assert(dream_staged_upload(&e, &i, nullptr, 0, &planes) && finish_seen);
   assert(textures[21].rgba == expected && rgb == old_rgb && alpha == old_alpha);
   state();
   auto truncated = planes;
   truncated.alpha_size = alpha_row;
   const auto previous = textures[21].rgba;
   const unsigned blits_before = blits;
   assert(!dream_staged_upload(&e, &i, nullptr, 0, &truncated));
   assert(blits == blits_before && textures[21].rgba == previous);
   state();
  }
 }
 textures.clear();puts("PASS: production staged upload: RGBA/BGRA/alpha/oddrows/normalized flips/input unchanged, NULL destination allocation, GPU completion before release, texture identity/unpack/FBO/scissor restored; allocation/lock/import/storage/blit errors + format/quota/PBO gates preserve CPU fallback");
}
