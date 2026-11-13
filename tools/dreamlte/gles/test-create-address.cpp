#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using IOReturn=int;using task_t=uintptr_t;using SInt64=int64_t;
constexpr int kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNoMemory=2,kIOReturnNoResources=3,kIOReturnUnsupported=4;
constexpr int kIODirectionInOut=3,kIOMemoryKernelUserShared=4,kIOMapDefaultCache=0;
constexpr unsigned SWEET_SURFACE_MAX_PROPS=65536,SWEET_SURFACE_MAX_ALLOC=256u<<20,page_size=4096;
constexpr uint64_t SWEET_SURFACE_WIRED_MAX=1536ull<<20;constexpr task_t kernel_task=1;
static uint64_t sweet_surface_wired;static unsigned sweet_surface_creates_logged;
static int prepares,completes,external_frees,buffer_allocations,publishes;
static bool fail_prepare,fail_factory,fail_publish;static task_t actual_owner;
static uint64_t actual_address,actual_length;
static int64_t OSAddAtomic64(int64_t n,volatile int64_t*p){int64_t old=*p;*p=old+n;return old;}
struct OSObject{virtual ~OSObject(){};virtual void release(){};void free(){}};
struct OSNumber:OSObject{uint64_t value;OSNumber(uint64_t v):value(v){};uint64_t unsigned64BitValue(){return value;}};
struct OSDictionary:OSObject{
 std::map<std::string,OSObject*>values;OSObject*getObject(const char*k){auto i=values.find(k);return i==values.end()?nullptr:i->second;}
 unsigned getCount(){return values.size();}
};
#define OSDynamicCast(type,p) dynamic_cast<type*>(p)
#define OSSafeReleaseNULL(p) do{if(p){(p)->release();p=nullptr;}}while(0)
struct IOMemoryDescriptor:OSObject{
 uint64_t address=0,length=0;bool external=false;
 static IOMemoryDescriptor*withAddressRange(uint64_t a,uint64_t n,int dir,task_t owner){
  assert(dir==kIODirectionInOut);actual_owner=owner;actual_address=a;actual_length=n;
  if(fail_factory)return nullptr;
  auto*m=new IOMemoryDescriptor;m->address=a;m->length=n;m->external=true;return m;
 }
 int prepare(){++prepares;return fail_prepare?kIOReturnNoMemory:0;}
 void complete(){++completes;}
 uint64_t getLength(){return length;}
 size_t readBytes(unsigned off,void*out,unsigned n){memcpy(out,(void*)(uintptr_t)(address+off),n);return n;}
 void release()override{external_frees+=external;delete this;}
};
struct IOBufferMemoryDescriptor:IOMemoryDescriptor{
 std::vector<uint8_t>bytes;
 static IOBufferMemoryDescriptor*inTaskWithOptions(task_t owner,int,unsigned n,unsigned){
  assert(owner==kernel_task);++buffer_allocations;auto*m=new IOBufferMemoryDescriptor;m->bytes.resize(n);m->length=n;return m;
 }
 void*getBytesNoCopy(){return bytes.data();}
};
struct Map:OSObject{void release()override{delete this;}};
struct Plane{unsigned offset,size;};
struct SweetSurface:OSObject{
 uint32_t parentID=0,width=0,height=0,bytesPerRow=0,allocSize=0,offset=0,planeCount=0,pixelFormat=0,id=0;
 unsigned elementHeight=1,elementWidth=1,bytesPerElement=4,mapCache=99;bool userMemoryPrepared=false;
 IOMemoryDescriptor*memory=nullptr;IOBufferMemoryDescriptor*state=nullptr;Map*kernelMap=nullptr;Plane planes[20]{};
 unsigned refs=1;void retain(){++refs;}void free();void release()override{if(!--refs){free();delete this;}}
};
#include "CreateAddressFree.production.inc"
static SweetSurface*sweet_surface_alloc(){return new SweetSurface;}
static bool sweet_surface_publish(SweetSurface*s){++publishes;if(fail_publish)return false;s->id=42;return true;}
static IOReturn sweet_surface_layout(SweetSurface*s,OSDictionary*){
 s->width=180;s->height=180;s->bytesPerRow=720;s->allocSize=131072;s->pixelFormat=0;return 0;
}
static OSDictionary*incoming;static OSObject*OSUnserializeXML(const char*,unsigned){return incoming;}
static void*IOMallocData(unsigned n){return new uint8_t[n];}static void IOFreeData(void*p,unsigned){delete[](uint8_t*)p;}
static void IOLog(const char*,...){}static void proc_selfname(char*p,unsigned n){strncpy(p,"backboardd",n);}
struct IOExternalMethodArguments{const void*structureInput=nullptr;unsigned structureInputSize=0;IOMemoryDescriptor*structureInputDescriptor=nullptr;unsigned scalarInputCount=0;uint64_t scalarInput[1]{};};
#include "DreamSurfaceAddress.inc"
static SweetSurface*returned;struct IOSurfaceRootUserClient{
 task_t fTask=0x55;
 IOReturn create(IOExternalMethodArguments*);
 IOReturn returnSurface(SweetSurface*s,IOExternalMethodArguments*){s->retain();returned=s;return 0;}
};
#include "CreateAddress.production.inc"
int main(int argc,char**){
 auto helper=&dream_surface_create_address;(void)helper;
 std::vector<uint8_t>pixels(180*180*4);
 for(size_t i=0;i<pixels.size();i++)pixels[i]=(i*7+31)&255;
 const auto original=pixels;
 OSNumber address((uintptr_t)pixels.data()),size(pixels.size()),zero(0),short_size(1),large(UINT64_MAX),offset(1);OSObject wrong;
 OSDictionary props;props.values={{"IOSurfaceAddress",&address},{"IOSurfaceAllocSize",&size}};incoming=&props;
 IOExternalMethodArguments args;args.structureInput="serialized";args.structureInputSize=10;
 IOSurfaceRootUserClient client;
 assert(client.create(&args)==0&&returned&&publishes==1);
 if(argc>1){
  assert(buffer_allocations==1&&!returned->userMemoryPrepared);
  auto*buffer=dynamic_cast<IOBufferMemoryDescriptor*>(returned->memory);assert(buffer);
  for(auto b:buffer->bytes)assert(b==0);
  assert(pixels==original);returned->release();returned=nullptr;assert(sweet_surface_wired==0);
  puts("PASS: pre-fix production create ignores nonzero caller icon pixels and substitutes zeroed allocation");return 0;
 }
 assert(buffer_allocations==0);
 assert(actual_owner==0x55&&actual_address==(uintptr_t)pixels.data()&&actual_length==pixels.size());
 assert(returned->userMemoryPrepared&&returned->allocSize==pixels.size()&&sweet_surface_wired==pixels.size());
 assert(pixels==original);
 std::vector<uint8_t>read(pixels.size());returned->memory->readBytes(0,read.data(),read.size());assert(read==pixels);
 pixels[81]=0xef;returned->memory->readBytes(81,read.data(),1);assert(read[0]==0xef);
 returned->release();returned=nullptr;assert(sweet_surface_wired==0&&completes==1&&external_frees==1);
 // Missing address uses the original fresh-allocation path, still published once.
 props.values.erase("IOSurfaceAddress");int pub=publishes;assert(client.create(&args)==0&&buffer_allocations==1&&publishes==pub+1);
 assert(!returned->userMemoryPrepared&&returned->allocSize==131072);returned->release();returned=nullptr;assert(sweet_surface_wired==0);
 auto bad=[&](OSObject*a,OSObject*n){props.values={{"IOSurfaceAddress",a},{"IOSurfaceAllocSize",n}};int before=buffer_allocations;
  assert(client.create(&args)!=0&&!returned&&buffer_allocations==before&&sweet_surface_wired==0);};
 bad(&zero,&size);bad(&wrong,&size);bad(&address,&wrong);bad(&address,&short_size);bad(&large,&size);bad(&address,&large);
 props.values={{"IOSurfaceAddress",&address},{"IOSurfaceAllocSize",&size}};
 props.values["IOSurfaceAddressOffset"]=&offset;assert(client.create(&args)==kIOReturnUnsupported&&!returned);props.values.erase("IOSurfaceAddressOffset");
 props.values["IOSurfaceAddressRanges"]=&wrong;assert(client.create(&args)==kIOReturnUnsupported&&!returned);props.values.erase("IOSurfaceAddressRanges");
 fail_factory=true;assert(client.create(&args)==kIOReturnNoMemory&&!returned&&sweet_surface_wired==0);fail_factory=false;
 fail_prepare=true;int n=completes;assert(client.create(&args)==kIOReturnNoMemory&&!returned&&sweet_surface_wired==0&&completes==n);fail_prepare=false;
 sweet_surface_wired=SWEET_SURFACE_WIRED_MAX-1;n=completes;assert(client.create(&args)==kIOReturnNoMemory&&!returned&&completes==n+1&&sweet_surface_wired==SWEET_SURFACE_WIRED_MAX-1);sweet_surface_wired=0;
 fail_publish=true;n=completes;assert(client.create(&args)==kIOReturnNoResources&&!returned&&completes==n+1&&sweet_surface_wired==0);fail_publish=false;
 assert(buffer_allocations==1);
 puts("PASS: production create/address/free; actual icon bytes shared and never zeroed; caller task; exact external span; fresh allocation preserved; publication and mapping/prepare/quota/type/range failures");
}
