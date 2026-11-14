#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
using Clock=std::chrono::steady_clock;
static constexpr uint64_t NSEC_PER_SEC=1000000000,NSEC_PER_MSEC=1000000;
static constexpr int THREAD_UNINT=0,KERN_SUCCESS=0;
using wait_result_t=int;using thread_t=int;
struct IOLock{std::mutex mutex;std::condition_variable cv;};
static bool allocationFail,startFail;static std::atomic<bool> stop{},failed{};
static IOLock* IOLockAlloc(){return allocationFail?nullptr:new IOLock;}
static void IOLockLock(IOLock*p){p->mutex.lock();}
static void IOLockUnlock(IOLock*p){p->mutex.unlock();}
struct Stop{};
static uint64_t mach_absolute_time(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();}
static void clock_interval_to_deadline(unsigned n,uint64_t unit,uint64_t*out){*out=mach_absolute_time()+(n==2000?100*NSEC_PER_MSEC:n*unit);}
static void nanoseconds_to_absolutetime(uint64_t n,uint64_t*out){*out=n;}
static void absolutetime_to_nanoseconds(uint64_t n,uint64_t*out){*out=n;}
static void IOLockSleep(IOLock*p,void*,int){
 std::unique_lock<std::mutex> lock(p->mutex,std::adopt_lock);
 if(stop)throw Stop{};
 p->cv.wait(lock);
 if(stop)throw Stop{};
 lock.release();
}
static void IOLockSleepDeadline(IOLock*p,void*,uint64_t deadline,int){
 std::unique_lock<std::mutex> lock(p->mutex,std::adopt_lock);
 p->cv.wait_until(lock,Clock::time_point(std::chrono::nanoseconds(deadline)));lock.release();
}
static void IOLockWakeup(IOLock*p,void*,bool){p->cv.notify_all();}
static std::thread worker;
static int kernel_thread_start(void(*fn)(void*,int),void*arg,thread_t*out){
 if(startFail)return 1;*out=1;
 worker=std::thread([=]{try{fn(arg,0);}catch(const Stop&){}});
 return 0;
}
static void thread_deallocate(thread_t){}
static std::atomic<unsigned> binds{};
static void dream_bind_gpu_worker_cpu(){++binds;}
static bool hakios_mali_ready(){return !failed;}
static uint64_t dpf_seq,dpf_done;
static std::mutex gpuMutex;
static std::condition_variable gpuCV;
static bool permit=false;
static uint64_t began=0,failSeq=0;
static std::vector<uint64_t> order;
static bool dpf_execute_chain(uint64_t chain,bool fragment,uint64_t seq){
 assert(chain==seq*8&&fragment==bool(seq%2));
 std::unique_lock<std::mutex> lock(gpuMutex);began=seq;gpuCV.notify_all();
 gpuCV.wait(lock,[]{return permit;});
 order.push_back(seq);
 if(seq==failSeq){failed=true;return false;}return true;
}
static void dpf_poison_queue(){failed=true;}
#define IOLog(...) ((void)0)
#define DREAM_PANFROST_QUEUE_TEST 1
#include "DreamPanfrostQueue.inc"
static void gate(bool value){std::lock_guard<std::mutex> l(gpuMutex);permit=value;gpuCV.notify_all();}
static void begun(uint64_t seq){std::unique_lock<std::mutex>l(gpuMutex);assert(gpuCV.wait_for(l,std::chrono::seconds(1),[=]{return began>=seq;}));}
int main(int argc,char**argv){
 if(argc>1){
  allocationFail=!strcmp(argv[1],"allocfail");startFail=!allocationFail;permit=true;
  dpf_queue_start();assert(!dpf_queue_started);
  assert(dpf_enqueue_chain(8,true,false)==1&&hakios_mali_wait(1,0));
  assert(!dpf_enqueue_chain(0,false,true)&&!dpf_enqueue_chain(9,false,true));
  delete dpf_queue_lock;puts("PASS queue start/allocation failure retains synchronous completion");return 0;
 }
 dpf_queue_start();assert(dpf_queue_started);
 assert(dpf_enqueue_chain(8,true,true)==1);begun(1);
 assert(!hakios_mali_wait(1,0)&&!hakios_mali_wait(1,1000000)); // Accepted is not retired.
 for(uint64_t i=2;i<=64;i++)assert(dpf_enqueue_chain(i*8,i%2,true)==i);
 assert(!dpf_enqueue_chain(65*8,true,true)); // Full queue timeout does not assign a sequence.
 assert(dpf_seq==64&&dpf_done==0);
 std::atomic<uint64_t> accepted{};
 std::thread producer([&]{accepted=dpf_enqueue_chain(65*8,true,true);});
 std::this_thread::sleep_for(std::chrono::milliseconds(5));assert(!accepted);
 gate(true);producer.join();assert(accepted==65);
 assert(hakios_mali_wait(65,NSEC_PER_SEC)&&hakios_mali_wait(65,UINT64_MAX));
 assert(order.size()==65&&dpf_queue_peak==64&&binds==1);
 for(unsigned i=0;i<65;i++)assert(order[i]==i+1);
 gate(false);failSeq=66;assert(dpf_enqueue_chain(66*8,false,true)==66);begun(66);
 assert(dpf_enqueue_chain(67*8,true,false)==67);gate(true);
 assert(!hakios_mali_wait(66,NSEC_PER_SEC)&&!hakios_mali_wait(67,0));
 assert(!dpf_enqueue_chain(68*8,false,true));
 IOLockLock(dpf_queue_lock);assert(dpf_done==65&&dpf_queue_count==1);stop=true;
 IOLockWakeup(dpf_queue_lock,nullptr,false);IOLockUnlock(dpf_queue_lock);
 worker.join();assert(order.size()==66);delete dpf_queue_lock;
 puts("PASS actual FIFO: CPU overlap, no early retirement, ordered64-bound queue, full timeout/backpressure, waits, fault wakeup/no subsequent execution");
}
