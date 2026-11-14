#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#define DREAM_PANFROST_WAIT_TEST 1
using IOLock=int;
static IOLock lockValue;
static uint64_t now,readyAt,busyNs,sleepNs;
static unsigned slotValue=1,sleeps,fallbacks;
static bool mmuFault,jobFault,waitHeld,guardHeld=true;
static unsigned dmb_gpu;
static constexpr unsigned THREAD_UNINT=0;
static constexpr uint64_t NSEC_PER_MSEC=1000000,NSEC_PER_USEC=1000;
static uint64_t mach_absolute_time(){assert(guardHeld);return now;}
static void clock_interval_to_deadline(unsigned n,uint64_t u,uint64_t*out){*out=now+n*u;}
static uint32_t dm_read(unsigned,unsigned reg){
 assert(guardHeld);
 if(now<readyAt)return reg==0x1000?(1u<<(1-slotValue)):0; // Other slot completion cannot complete this wait.
 return reg==0x2000?(mmuFault?1:0):(1u<<slotValue)|(jobFault?(1u<<(slotValue+16)):0);
}
static void IODelay(unsigned n){assert(guardHeld&&!waitHeld);now+=n*1000;busyNs+=n*1000;}
static void IOSleep(unsigned n){assert(guardHeld&&!waitHeld);now+=n*1000000;sleepNs+=n*1000000;++fallbacks;}
static void IOLockLock(IOLock*p){assert(p==&lockValue&&!waitHeld&&guardHeld);waitHeld=true;}
static void IOLockUnlock(IOLock*p){assert(p==&lockValue&&waitHeld&&guardHeld);waitHeld=false;}
static int IOLockSleepDeadline(IOLock*p,void*event,uint64_t end,unsigned mode){
 assert(p==&lockValue&&event&&waitHeld&&guardHeld&&mode==THREAD_UNINT&&end>now&&end-now<=50000);
 waitHeld=false;sleepNs+=end-now;now=end;waitHeld=true;++sleeps;return 0;
}
#include "DreamPanfrostWait.inc"
static void reset(uint64_t done){now=busyNs=sleepNs=0;readyAt=done;sleeps=fallbacks=0;mmuFault=jobFault=waitHeld=false;dpf_wait_lock=&lockValue;}
int main(){uint32_t irq=0,mmu=0;unsigned checks=0,naps=0;
 for(unsigned sl=0;sl<2;sl++) {slotValue=sl;
  reset(0);assert(dpf_wait_job(sl,&irq,&mmu,&checks,&naps)&&checks==1&&!naps&&!now);
  reset(50000);assert(dpf_wait_job(sl,&irq,&mmu,&checks,&naps)&&now==50000&&!naps&&busyNs==50000);
  reset(200000);assert(dpf_wait_job(sl,&irq,&mmu,&checks,&naps)&&now==200000&&!naps);
  reset(251000);assert(dpf_wait_job(sl,&irq,&mmu,&checks,&naps)&&now==300000&&naps==2&&busyNs==200000);
  reset(2500000);assert(dpf_wait_job(sl,&irq,&mmu,&checks,&naps)&&now==2500000&&busyNs==200000&&naps==46);
  reset(130000);mmuFault=true;assert(!dpf_wait_job(sl,&irq,&mmu,&checks,&naps)&&mmu&&now==130000&&!naps);
  reset(0);jobFault=true;assert(!dpf_wait_job(sl,&irq,&mmu,&checks,&naps)&&!mmu&&!naps);
  reset(std::numeric_limits<uint64_t>::max());assert(!dpf_wait_job(sl,&irq,&mmu,&checks,&naps)&&now==1000000000&&busyNs==200000&&naps==19996);
  reset(400000);dpf_wait_lock=nullptr;assert(dpf_wait_job(sl,&irq,&mmu,&checks,&naps)&&fallbacks==1&&now==1200000);
  assert(!waitHeld&&guardHeld);
 }
 puts("PASS: actual GPU wait: both slots, immediate/50us/200us completion, 50us naps, 200us busy bound, 1s timeout, other-slot IRQ ignored, MMU/job faults, fallback; hardware guard never released");
}
