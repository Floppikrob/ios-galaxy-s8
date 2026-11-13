#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <cstddef>
using IOReturn=int;using task_t=uintptr_t;using SInt64=int64_t;using IOOptionBits=unsigned;
constexpr int kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNoMemory=2,kIODirectionInOut=3,kIOMapDefaultCache=0;
constexpr unsigned SWEET_SURFACE_MAX_DIM=16384,SWEET_SURFACE_MAX_ALLOC=256u<<20,SWEET_SURFACE_RESULT_SIZE=0xa68,SWEET_SURFACE_TIMESTAMPS=0x800;
constexpr uint64_t SWEET_SURFACE_WIRED_MAX=1536ull<<20;
static uint64_t sweet_surface_wired;static int prepares,completes,data_frees;static bool fail_factory,fail_prepare,fail_surface;
static uint64_t factory_address,factory_length;static task_t factory_owner;static std::vector<int> order;
static int64_t OSAddAtomic64(int64_t n,volatile int64_t *p){int64_t old=*p;*p=old+n;return old;}
struct IOMemoryDescriptor {
 bool data;uint64_t length;virtual ~IOMemoryDescriptor(){};
 IOMemoryDescriptor(bool d=false,uint64_t n=0):data(d),length(n){}
 static IOMemoryDescriptor *withAddressRange(uint64_t a,uint64_t n,int dir,task_t owner){assert(dir==kIODirectionInOut);factory_address=a;factory_length=n;factory_owner=owner;return fail_factory?nullptr:new IOMemoryDescriptor(true,n);}
 int prepare(){prepares++;return fail_prepare?kIOReturnNoMemory:0;}
 void complete(){assert(data);completes++;order.push_back(2);}
 uint64_t getLength(){return length;}
 void release(){if(data){data_frees++;order.push_back(3);}delete this;}
};
struct IOBufferMemoryDescriptor:IOMemoryDescriptor{uint8_t bytes[4096]{};IOBufferMemoryDescriptor():IOMemoryDescriptor(false,4096){};void *getBytesNoCopy(){return bytes;}};
#define OSDynamicCast(type,p) dynamic_cast<type *>(p)
#define OSSafeReleaseNULL(p) do{if(p){(p)->release();(p)=nullptr;}}while(0)
struct Map {uint64_t address;uint64_t getAddress(){return address;}void release(){order.push_back(1);delete this;}};
struct OSObject {void free(){}};
struct sweet_surface_plane{uint32_t width,height,bytesPerRow,offset,size;uint16_t bytesPerElement;uint8_t elementWidth,elementHeight;};
struct SweetSurface:OSObject {
 bool userMemoryPrepared=false;uint32_t id=0,width=0,height=0,bytesPerRow=0,offset=0,pixelFormat=0,allocSize=0;
 uint16_t bytesPerElement=0;uint8_t elementWidth=0,elementHeight=0;uint32_t cacheMode=0,planeCount=0,parentID=0; sweet_surface_plane planes[20]{};
 IOMemoryDescriptor *memory=nullptr;IOOptionBits mapCache=99;IOBufferMemoryDescriptor *state=new IOBufferMemoryDescriptor;Map *kernelMap=nullptr;
 void free();void release(){free();delete this;}
};
#include "SurfaceFree.production.inc"
static SweetSurface *sweet_surface_alloc(){return fail_surface?nullptr:new SweetSurface;}
#include "DreamSurfaceWrap.inc"
struct SweetSurfaceRef{SweetSurface *surface;Map *data,*state;};
static void put32(uint8_t *p,uint32_t o,uint32_t v){memcpy(p+o,&v,4);}static void put64(uint8_t *p,uint32_t o,uint64_t v){memcpy(p+o,&v,8);}
#include "SurfaceResult.production.inc"
static uint64_t u64(const uint8_t *p){uint64_t n;memcpy(&n,p,8);return n;}
int main(){
 static_assert(offsetof(DreamWrapImageRequest,address)==0&&offsetof(DreamWrapImageRequest,width)==8&&offsetof(DreamWrapImageRequest,pixelFormat)==16&&offsetof(DreamWrapImageRequest,bytesPerRow)==24,"decoded target ABI offsets");
 DreamWrapImageRequest q{0x160000123ULL,32,20,0x42475241,4,160,0};SweetSurface *s=nullptr;
 assert(!dream_surface_wrap(0x55,&q,32,&s)&&s&&factory_owner==0x55&&factory_address==q.address&&factory_length==3200);
 assert(s->memory->getLength()==3200&&s->allocSize==3200&&s->mapCache==kIOMapDefaultCache&&s->userMemoryPrepared);
 assert(s->width==32&&s->height==20&&s->bytesPerRow==160&&s->planeCount==0&&sweet_surface_wired==3200);
 s->id=7;Map data{0x180000123ULL},state{0x180010000ULL};SweetSurfaceRef ref{s,&data,&state};uint8_t result[SWEET_SURFACE_RESULT_SIZE];sweet_surface_fill_result(s,&ref,result);
 uint8_t client[104+sizeof(result)]{};memcpy(client+104,result,sizeof(result));
 assert(u64(client+112)==state.address&&u64(client+104)==data.address); // Actual IOSurfaceClientGetBulkAttachments ABI.
 assert(48+112<=s->state->getLength());uint8_t attachments[112];memcpy(attachments,(uint8_t *)s->state->getBytesNoCopy()+48,112);
 uint32_t denominator;memcpy(&denominator,attachments+96,4);assert(denominator==0); // EDR consumer uses documented observed default factor1.
 s->kernelMap=new Map{0xdead};order.clear();s->release();assert(sweet_surface_wired==0&&completes==1&&data_frees==1);assert(order.size()>=3&&order[0]==1&&order[1]==2&&order[2]==3);
 auto bad=[&](DreamWrapImageRequest a,size_t n=32){SweetSurface *p=(SweetSurface *)1;int before=prepares;assert(dream_surface_wrap(0x55,&a,n,&p)==kIOReturnBadArgument&&p==nullptr&&prepares==before);};
 auto a=q;a.width=0;bad(a);a=q;a.height=16385;bad(a);a=q;a.bytesPerRow=127;bad(a);a=q;a.bytesPerElement=1;bad(a);a=q;a.bytesPerElement=3;bad(a);a=q;a.address=0;bad(a);a=q;a.address=UINT64_MAX-10;bad(a);a=q;a.bytesPerRow=UINT32_MAX;bad(a);bad(q,31);bad(q,33);
 fail_factory=true;assert(dream_surface_wrap(0x55,&q,32,&s)==kIOReturnNoMemory&&!s&&sweet_surface_wired==0);fail_factory=false;
 int old_completes=completes;fail_prepare=true;assert(dream_surface_wrap(0x55,&q,32,&s)==kIOReturnNoMemory&&!s&&sweet_surface_wired==0&&completes==old_completes);fail_prepare=false;
 sweet_surface_wired=SWEET_SURFACE_WIRED_MAX-1;old_completes=completes;assert(dream_surface_wrap(0x55,&q,32,&s)==kIOReturnNoMemory&&!s&&sweet_surface_wired==SWEET_SURFACE_WIRED_MAX-1&&completes==old_completes+1);sweet_surface_wired=0;
 fail_surface=true;assert(dream_surface_wrap(0x55,&q,32,&s)==kIOReturnNoMemory&&!s);fail_surface=false;
 puts("PASS: target32B ABI, real bulk-state result, caller task/external pixels, prepare/complete/unmap ownership, geometry/overflow/quota and failure cleanup");
}
