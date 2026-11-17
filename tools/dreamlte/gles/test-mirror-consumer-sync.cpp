#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
struct Memory{std::vector<uint8_t>data;uint64_t getLength(){return data.size();}};
struct SweetSurface{
 uint64_t deferredInvalidateSeq=0,maliSeq=0,allocSize=64;bool displayMirrorReadOnly=false;
 unsigned id=4;Memory*memory;uintptr_t kernelAddress(){return memory?(uintptr_t)memory->data.data():0;}
};
using IOLock=std::mutex;static IOLock mutex;static IOLock*gMaliLock=&mutex;
static thread_local bool held;
static void IOLockLock(IOLock*p){assert(!held);p->lock();held=true;}
static void IOLockUnlock(IOLock*p){assert(held);held=false;p->unlock();}
static std::atomic<bool>ready{true};static std::atomic<uint64_t>completed{10};
static bool hakios_mali_ready(){return ready;}
static bool hakios_mali_wait(uint64_t seq,uint64_t timeout){assert(!timeout);return ready&&seq<=completed;}
static unsigned invalidates;static void dm_invalidate(void*,uint64_t bytes){assert(held&&bytes==64);++invalidates;}
#define IOLog(...) ((void)0)
#include "DreamSurfaceSync.inc"

constexpr int kHakiosMaliWait=1,kPanfrostWaitBO=2,kIOReturnSuccess=0,kIOReturnTimeout=1,kIOReturnNotFound=2,kIOReturnNotReady=3;
struct BO{SweetSurface*surface;uint64_t last_seq;};static BO bo;
static BO*buffer(unsigned h){return h==1?&bo:nullptr;}
static int wait_bo(){
 const int selector=kPanfrostWaitBO;int ret=-1;BO*b=nullptr;
 union{struct{uint64_t seq;int64_t timeout_ns;}waitseq;struct{uint32_t handle;uint32_t padding;int64_t timeout_ns;}wait;}r{};
 r.wait.handle=1;
 struct Args{void*structureOutput=nullptr;size_t structureOutputSize=0;}storage,*args=&storage;
 size_t size=sizeof(r);
 #include "WaitBO.production.inc"
 return -2;
}

int main(){
 Memory memory;memory.data.resize(64);SweetSurface s;s.memory=&memory;s.displayMirrorReadOnly=true;s.maliSeq=10;
 IOLockLock(gMaliLock);std::atomic<bool>finished{};bool accepted=false;
 std::thread cpu([&]{accepted=dream_surface_sync_cpu(&s);if(accepted)assert(memory.data[0]==0x5a);finished=true;});
 std::this_thread::sleep_for(std::chrono::milliseconds(10));
 assert(!finished); // Owned+dirty0 must STILL synchronize with an in-flight producer holding main lock.
 memory.data[0]=0x5a;completed=11;
 __atomic_store_n(&s.maliSeq,11ULL,__ATOMIC_RELEASE);__atomic_store_n(&s.deferredInvalidateSeq,11ULL,__ATOMIC_RELEASE);
 IOLockUnlock(gMaliLock);cpu.join();
 assert(accepted&&invalidates==1&&!s.deferredInvalidateSeq&&dpf_mirror_cpu_syncs==1);
 assert(dream_surface_sync_cpu(&s)&&invalidates==1); // No redundant second invalidate.
 s.displayMirrorReadOnly=false;s.maliSeq=12;s.deferredInvalidateSeq=12;completed=12;
 assert(dream_surface_sync_cpu(&s)&&invalidates==2&&!s.deferredInvalidateSeq); // Disqualification cannot hide outstanding data.
 s.displayMirrorReadOnly=true;s.maliSeq=13;s.deferredInvalidateSeq=13;
 assert(!dream_surface_sync_cpu(&s)&&s.deferredInvalidateSeq==13&&invalidates==2);
 completed=13;ready=false;assert(!dream_surface_sync_cpu(&s)&&s.deferredInvalidateSeq==13);ready=true;
 s.maliSeq=~0ULL;assert(!dream_surface_sync_cpu(&s)&&s.deferredInvalidateSeq==13);s.maliSeq=13;
 auto*m=s.memory;s.memory=nullptr;assert(!dream_surface_sync_cpu(&s)&&s.deferredInvalidateSeq==13);s.memory=m;
 s.allocSize=65;assert(!dream_surface_sync_cpu(&s)&&s.deferredInvalidateSeq==13);s.allocSize=64;
 gMaliLock=nullptr;assert(!dream_surface_sync_cpu(&s));gMaliLock=&mutex;
 assert(dream_surface_sync_cpu(&s)&&invalidates==3&&!s.deferredInvalidateSeq);
 ready=false;assert(!dream_surface_sync_cpu(&s));s.displayMirrorReadOnly=false;assert(dream_surface_sync_cpu(&s));ready=true;

 // A newly imported handle has last_seq0 although another client produced dirty pixels.
 bo={&s,0};s.displayMirrorReadOnly=true;s.maliSeq=14;s.deferredInvalidateSeq=14;completed=14;
 assert(wait_bo()==kIOReturnSuccess&&invalidates==4&&!s.deferredInvalidateSeq);
 assert(wait_bo()==kIOReturnSuccess&&invalidates==4);
 s.maliSeq=15;s.deferredInvalidateSeq=15;
 assert(wait_bo()==kIOReturnNotReady&&s.deferredInvalidateSeq==15&&invalidates==4);
 completed=15;ready=false;assert(wait_bo()==kIOReturnNotReady&&s.deferredInvalidateSeq==15);ready=true;
 assert(wait_bo()==kIOReturnSuccess&&invalidates==5&&!s.deferredInvalidateSeq);
 s.displayMirrorReadOnly=false;bo.last_seq=0;
 assert(wait_bo()==kIOReturnSuccess&&invalidates==5); // Legacy seq0 stays no-op.
 bo.last_seq=15;assert(wait_bo()==kIOReturnSuccess&&invalidates==6); // Ordinary surface keeps full invalidation.

 puts("PASS actual consumer sync + production WAIT_BO including seq0 import: in-flight producer blocks CPU even dirty0; completed writes invalidated once; revoked ownership still syncs; pending/fault/bounds/no-lock failures expose nothing");
}
