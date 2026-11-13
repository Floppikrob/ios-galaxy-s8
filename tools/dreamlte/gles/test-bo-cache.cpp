#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <vector>
struct Memory {virtual ~Memory(){};uint64_t length;virtual uint64_t getLength(){return length;}};
struct IOBufferMemoryDescriptor:Memory{void *bytes;void *getBytesNoCopy(){return bytes;}};
#define OSDynamicCast(type,object) dynamic_cast<type *>(object)
struct Surface{uintptr_t address;uint64_t allocSize;uintptr_t kernelAddress(){return address;}};
struct mali_buffer{Memory *memory;Surface *surface;uint64_t size;};
struct Event {void *address;uint64_t length;bool clean;};
static std::vector<Event> events;
static void dm_clean(void *p,uint64_t n){events.push_back({p,n,true});}
static void dm_invalidate(void *p,uint64_t n){events.push_back({p,n,false});}
#include "DreamPanfrostCache.inc"
int main(){
 IOBufferMemoryDescriptor memory;memory.length=8192;memory.bytes=(void *)0xfffffff100001000ULL;
 mali_buffer b{&memory,nullptr,4096};flushSubmit(&b);flushComplete(&b);
 assert(events.size()==2&&events[0].clean&&!events[1].clean);
 assert(events[0].address==memory.bytes&&events[1].address==memory.bytes&&events[1].length==4096);
 Surface surface{0xfffffff200001000ULL,8192};b.surface=&surface;flushSubmit(&b);flushComplete(&b);
 assert(events.size()==4&&events[2].address==(void *)surface.address&&events[3].length==8192);
 b.size=4096;surface.allocSize=12288;flushSubmit(&b);assert(events.size()==4); // No out-of-bounds cache op.
 b.surface=nullptr;b.size=12288;flushComplete(&b);assert(events.size()==4);
 b.size=0;flushSubmit(&b);flushSubmit(nullptr);b.memory=nullptr;flushComplete(&b);assert(events.size()==4);
 Memory unknown;unknown.length=4096;b={&unknown,nullptr,4096};flushSubmit(&b);assert(events.size()==4);
 puts("PASS: full-width kernel pointers, ordinary BO and surface clean/invalidate sequence, bounds and invalid backing guards");
}
