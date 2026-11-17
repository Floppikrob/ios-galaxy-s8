#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
static uint32_t mmu[1024],decon[1024],idma[1024];
static bool df_failed,stickyFault,freshFault,badControl,failLatch,instantFrame;
static unsigned blockCalls,failBlockAt,started,ptWrites,shadowWrites;
struct Write{uint32_t*reg;unsigned off;uint32_t value;};
static std::vector<Write>writes;
static void df_start_cont(uint64_t){++started;}
static bool PE_parse_boot_argn(const char*,int*v,unsigned){*v=1;return true;}
static uintptr_t ml_io_map(uint64_t pa,unsigned){
 if(pa==0x12910000)return(uintptr_t)mmu;if(pa==0x12860000)return(uintptr_t)decon;
 if(pa==0x128b1000)return(uintptr_t)idma;return 0;
}
static void IOSleep(unsigned){
 if((decon[0x60/4]&0x8000003f)&&!failLatch){decon[0x60/4]=0;decon[0x4c/4]|=1u<<13;}
}
#define IOLog(...) ((void)0)
static void df_test_write(volatile uint32_t*r,unsigned off,uint32_t v){
 auto*p=const_cast<uint32_t*>(r);writes.push_back({p,off,v});
 if(p==mmu&&off==0x64){if(!stickyFault)p[0x60/4]&=~v;return;}
 if(p==decon&&off==0x4c){p[off/4]&=~v;return;}
 if(p==idma&&off==4){p[1]=(p[1]&0xffff0000u&~(v&0xffff0000u))|(v&0xffffu);return;}
 p[off/4]=v;
 if(p==mmu&&off==0){
  if(v==7){++blockCalls;p[2]=blockCalls==failBlockAt?0:1;}
  else p[2]=0;
  if(v==5){if(freshFault&&p[3])p[0x60/4]=2;if(badControl)p[0]=4;}
 }
 if(p==mmu&&off==0xc)++ptWrites;
 if(p==decon&&off==0x60){
  ++shadowWrites;
  if(instantFrame&&!failLatch){p[off/4]=0;p[0x4c/4]|=1u<<13;}
 }
}
#include "DisplayStartup.production.inc"
static void reset(){
 memset(mmu,0,sizeof(mmu));memset(decon,0,sizeof(decon));memset(idma,0,sizeof(idma));
 df_failed=stickyFault=freshFault=badControl=failLatch=instantFrame=false;
 blockCalls=failBlockAt=started=ptWrites=shadowWrites=0;writes.clear();
 mmu[0]=4;mmu[1]=0x20000;mmu[0x34/4]=0x70000011;
 decon[0]=0x113;decon[0x70/4]=0x1281;decon[0x40/4]=0x44;
 idma[0x40/4]=idma[0x44/4]=0xcc000000;idma[0x10/4]=idma[0x18/4]=0xb9005a0;
}
int main(){
 reset();instantFrame=true;dream_display_flip_trial(0x886584000ULL);
 assert(started==1&&!df_failed&&mmu[0]==4&&!mmu[3]&&!mmu[0x60/4]);
 assert(idma[16]==0xcc000000&&decon[0x40/4]==0x44);
 // An immediate framedone must not be cleared AFTER commit (old trial lost it).
 assert(shadowWrites==2);
 reset();mmu[0x60/4]=0x10;dream_display_flip_trial(0x886584000ULL);
 assert(started==1&&!df_failed&&!mmu[0x60/4]);
 auto ack=writes.end(),table=writes.end();
 for(auto i=writes.begin();i!=writes.end();++i){
  if(i->reg==mmu&&i->off==0x64&&ack==writes.end())ack=i;
  if(i->reg==mmu&&i->off==0xc&&table==writes.end())table=i;
 }
 assert(ack<table&&ack->value==0x10); // Clear inherited observed IRQ under block before new domain.
 reset();freshFault=true;dream_display_flip_trial(0x886584000ULL);
 assert(!started&&!df_failed&&mmu[0]==4&&!mmu[3]&&!mmu[0x60/4]&&shadowWrites==1);
 assert(idma[16]==0xcc000000&&idma[17]==0xcc000000&&decon[0x40/4]==0x44);
 reset();badControl=true;dream_display_flip_trial(0x886584000ULL);
 assert(!started&&!df_failed&&mmu[0]==4&&!mmu[3]&&shadowWrites==1);
 reset();freshFault=true;stickyFault=true;dream_display_flip_trial(0x886584000ULL);
 assert(!started&&df_failed&&mmu[0x60/4]&&!shadowWrites);
 reset();freshFault=true;failLatch=true;dream_display_flip_trial(0x886584000ULL);
 assert(!started&&df_failed&&shadowWrites==1&&mmu[0]==4&&!mmu[3]);
 reset();freshFault=true;failBlockAt=2;dream_display_flip_trial(0x886584000ULL);
 assert(!started&&df_failed&&ptWrites==1&&!shadowWrites); // No unsafe PT change when block failed.
 reset();failBlockAt=1;dream_display_flip_trial(0x886584000ULL);
 assert(!started&&!ptWrites&&mmu[0]==4); // Original domain untouched.
 reset();mmu[3]=0x123;dream_display_flip_trial(0x886584000ULL);
 assert(writes.empty()&&!started); // Never replace an existing owner.
 reset();dream_display_flip_trial(0x886584001ULL);assert(writes.empty());
 puts("PASS production display startup: sticky IRQ baseline, fresh-fault rejection, verified bypass+fresh boot frame recovery, immediate frame retention, block/latch/clear failure write suppression, existing-owner/root guards");
}
