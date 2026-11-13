#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include "dream_b3a8.h"
constexpr unsigned SWEET_SURFACE_RESULT_SIZE=0xa68,SWEET_SURFACE_TIMESTAMPS=0x800;
struct sweet_surface_plane{uint32_t width,height,bytesPerRow,offset,size;uint16_t bytesPerElement;uint8_t elementWidth,elementHeight;};
struct SweetSurface{
 uint32_t id=42,allocSize=13881344,width=1125,height=2436,bytesPerRow=4544,offset=0,pixelFormat=0x62336138,
 planeCount=2,cacheMode=0,parentID=0;uint16_t bytesPerElement=4;uint8_t elementWidth=1,elementHeight=1;
 sweet_surface_plane planes[2]={{1125,2436,4544,0,11071488,4,1,1},{1125,2436,1152,11071488,2809856,1,1,1}};
};
struct Map{uint64_t address;uint64_t getAddress(){return address;}};
struct SweetSurfaceRef{SweetSurface*surface;Map *data,*state;};
static void put32(uint8_t*p,uint32_t o,uint32_t v){memcpy(p+o,&v,4);}
static void put64(uint8_t*p,uint32_t o,uint64_t v){memcpy(p+o,&v,8);}
#include "PlaneResult.production.inc"
// Numeric field locations independently decoded from this iOS16 framework's
// exported client getters. They are relative to IOSurfaceClient, not result.
// Only these ABI facts are retained; no framework code or bytes are distributed.
struct Client{uint8_t data[104+SWEET_SURFACE_RESULT_SIZE]{};};
using IOSurfaceRef=Client*;
static uint32_t rd32(Client*c,size_t at){uint32_t v;memcpy(&v,c->data+at,4);return v;}
static uint64_t rd64(Client*c,size_t at){uint64_t v;memcpy(&v,c->data+at,8);return v;}
static uintptr_t IOSurfaceGetBaseAddress(Client*c){return rd64(c,104)+rd32(c,148);}
static uintptr_t IOSurfaceGetBaseAddressOfPlane(Client*c,unsigned n){return n<rd32(c,160)?rd64(c,104)+rd32(c,220+128*n):0;}
static size_t IOSurfaceGetAllocSize(Client*c){return rd32(c,132);}
static unsigned IOSurfaceGetPixelFormat(Client*c){return rd32(c,152);}
static unsigned IOSurfaceGetPlaneCount(Client*c){return rd32(c,160);}
static size_t IOSurfaceGetBytesPerRowOfPlane(Client*c,unsigned n){return n<rd32(c,160)?rd32(c,224+128*n):0;}
static size_t IOSurfaceGetWidthOfPlane(Client*c,unsigned n){return n<rd32(c,160)?rd32(c,208+128*n):0;}
static size_t IOSurfaceGetHeightOfPlane(Client*c,unsigned n){return n<rd32(c,160)?rd32(c,212+128*n):0;}
#include "B3a8Planes.production.inc"
int main(int argc,char**){
 SweetSurface s;std::vector<uint8_t> pixels(s.allocSize,0);
 Map data{(uintptr_t)pixels.data()},state{0x180010000ULL};SweetSurfaceRef ref{&s,&data,&state};
 uint8_t result[SWEET_SURFACE_RESULT_SIZE];sweet_surface_fill_result(&s,&ref,result);
 Client c;memcpy(c.data+104,result,sizeof(result));
 uint8_t *rgb=nullptr,*alpha=nullptr;size_t nr=0,rr=0,na=0,ra=0;
 bool accepted=b3a8_planes(&c,&rgb,&nr,&rr,&alpha,&na,&ra,s.width,s.height);
 if(argc>1){assert(!accepted&&IOSurfaceGetBytesPerRowOfPlane(&c,0)==0);puts("PASS: pre-fix production reply reproduces zero plane stride and attachment rejection");return 0;}
 assert(accepted&&rr==4544&&ra==1152&&rgb==pixels.data()&&alpha==pixels.data()+11071488);
 for(unsigned n=0;n<2;n++){
  assert(IOSurfaceGetWidthOfPlane(&c,n)==1125&&IOSurfaceGetHeightOfPlane(&c,n)==2436);
  uint16_t bpe;memcpy(&bpe,c.data+232+128*n,2);assert(bpe==(n?1:4));
  assert(c.data[234+128*n]==1&&c.data[235+128*n]==1);
  assert(rd32(&c,228+128*n)==s.planes[n].size&&c.data[292+128*n]==0);
 }
 assert(IOSurfaceGetBaseAddressOfPlane(&c,2)==0&&IOSurfaceGetBytesPerRowOfPlane(&c,2)==0);
 std::vector<uint8_t> bgra((size_t)s.width*s.height*4);
 for(size_t at=0;at<bgra.size();at+=4){bgra[at]=11;bgra[at+1]=89;bgra[at+2]=230;bgra[at+3]=127;}
 assert(dream_b3a8_pack(rgb,nr,rr,alpha,na,ra,bgra.data(),bgra.size(),s.width*4,s.width,s.height,0));
 std::vector<uint8_t> output(bgra.size());
 assert(dream_b3a8_unpack(output.data(),output.size(),s.width*4,rgb,nr,rr,alpha,na,ra,s.width,s.height,0));
 assert(output==bgra);
 // Non-planar wire fields are independent of the corrected planar stride.
 s.planeCount=0;sweet_surface_fill_result(&s,&ref,result);memcpy(c.data+104,result,sizeof(result));
 assert(IOSurfaceGetPlaneCount(&c)==0&&rd32(&c,144)==4544&&rd64(&c,112)==state.address);
 puts("PASS: production IOSurface reply -> independently decoded client getters -> real b3a8 attachment -> full1125x2436 color/alpha roundtrip; BGRA/state preserved");
}
