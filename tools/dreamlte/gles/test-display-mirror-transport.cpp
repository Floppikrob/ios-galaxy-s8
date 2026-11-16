#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <thread>
#include <vector>
using io_service_t=unsigned;using io_connect_t=unsigned;
struct Surface{uint32_t id,seed;};using IOSurfaceRef=Surface*;
static Surface source{17,23},mirror{31,47};
static bool noService,openFail;static std::atomic<unsigned>lookups{},opens{},releases{},offers{};
static const char*IOServiceMatching(const char*name){assert(!strcmp(name,"HakiosMali"));return name;}
static unsigned IOServiceGetMatchingService(unsigned port,const char*){assert(!port);++lookups;return noService?0:5;}
static unsigned mach_task_self(){return 99;}
static int IOServiceOpen(unsigned service,unsigned task,unsigned type,io_connect_t*out){
 assert(service==5&&task==99&&!type);++opens;if(openFail)return 1;*out=7;return 0;
}
static int IOObjectRelease(unsigned service){assert(service==5);++releases;return 0;}
static uint32_t IOSurfaceGetID(Surface*s){return s->id;}
static uint32_t IOSurfaceGetSeed(Surface*s){return s->seed;}
static int IOConnectCallStructMethod(unsigned connection,unsigned selector,const void*ptr,size_t n,void*,size_t*out){
 assert(connection==7&&selector==0x81&&n==32&&*out==32);
 auto*q=(const uint32_t*)ptr;assert(q[0]==source.id&&q[1]==mirror.id&&q[2]==source.seed&&q[3]==mirror.seed);
 assert(q[4]==1125&&q[5]==2436&&!q[6]&&!q[7]);++offers;return 1; // Optional hint may be rejected; pixels are unaffected.
}
#include "dream-display-mirror.inc"
int main(int argc,char**argv){
 if(argc>1){noService=!strcmp(argv[1],"no-service");openFail=!noService;}
 std::vector<std::thread>threads;
 for(int i=0;i<16;i++)threads.emplace_back([]{dream_offer_display_mirror(&source,&mirror,1125,2436);});
 for(auto&t:threads)t.join();
 assert(lookups==1&&opens==(noService?0:1)&&releases==(noService?0:1));
 assert(offers==((noService||openFail)?0:16));
 ++source.seed;++mirror.seed;dream_offer_display_mirror(&source,&mirror,1125,2436);
 assert(lookups==1&&offers==((noService||openFail)?0:17));
 assert(source.id==17&&mirror.id==31&&source.seed==24&&mirror.seed==48);
 puts("PASS actual optional hint transport: once initialization under concurrent frames, exact32-byte seeds/IDs/dimensions, rejected/unavailable service leaves pixels unchanged");
}
