#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DREAM_B3A8_VECTOR_TEST 1
#include "dream_b3a8_vector.h"
static uint32_t rng=0x98572;
static void randomize(uint8_t*p,size_t n){while(n--){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;*p++=(uint8_t)rng;}}
static uint64_t pixels;
static void check(unsigned w,unsigned h,unsigned off,int invert){
 size_t rb=w*4+7,rr=w*4+13,ra=w+9,nb=rb*h+64,nr=rr*h+64,na=ra*h+64;
 uint8_t *b=malloc(nb),*saved=malloc(nb),*r1=malloc(nr),*r2=malloc(nr),*a1=malloc(na),*a2=malloc(na);
 assert(b&&saved&&r1&&r2&&a1&&a2);randomize(b,nb);memcpy(saved,b,nb);
 memset(r1,0x69,nr);memset(r2,0x69,nr);memset(a1,0x97,na);memset(a2,0x97,na);
 assert(dream_b3a8_pack(r1+off,nr-off,rr,a1+off,na-off,ra,b+off,nb-off,rb,w,h,invert));
 assert(dream_b3a8_pack_vector(r2+off,nr-off,rr,a2+off,na-off,ra,b+off,nb-off,rb,w,h,invert));
 assert(!memcmp(r1,r2,nr)&&!memcmp(a1,a2,na)&&!memcmp(b,saved,nb));pixels+=(uint64_t)w*h;
 // Each validation failure leaves BOTH destination planes and their padding untouched.
 size_t needR=(h-1)*rr+w*4,needA=(h-1)*ra+w,needB=(h-1)*rb+w*4;
 assert(!dream_b3a8_pack_vector(r2+off,needR-1,rr,a2+off,na-off,ra,b+off,nb-off,rb,w,h,invert));
 assert(!dream_b3a8_pack_vector(r2+off,nr-off,rr,a2+off,needA-1,ra,b+off,nb-off,rb,w,h,invert));
 assert(!dream_b3a8_pack_vector(r2+off,nr-off,rr,a2+off,na-off,ra,b+off,needB-1,rb,w,h,invert));
 assert(!memcmp(r1,r2,nr)&&!memcmp(a1,a2,na));free(b);free(saved);free(r1);free(r2);free(a1);free(a2);
}
int main(void){
 for(unsigned off=0;off<16;off++)for(unsigned w=1;w<=129;w++)for(int inv=0;inv<2;inv++)check(w,(w%7)+1,off,inv);
 for(int inv=0;inv<2;inv++){check(1125,2436,3,inv);check(16384,3,7,inv);check(33,17,1,inv);}
 uint8_t b[8]={0},r[8]={0},a[8]={0};
 assert(!dream_b3a8_pack_vector(r,8,4,a,8,1,b,8,4,0,1,0));
 assert(!dream_b3a8_pack_vector(r,8,4,a,8,1,b,8,4,1,0,0));
 assert(!dream_b3a8_pack_vector(NULL,8,4,a,8,1,b,8,4,1,1,0));
 assert(!dream_b3a8_pack_vector(r,8,4,NULL,8,1,b,8,4,1,1,0));
 assert(!dream_b3a8_pack_vector(r,8,4,a,8,1,NULL,8,4,1,1,0));
 assert(!dream_b3a8_pack_vector(r,SIZE_MAX,SIZE_MAX,a,SIZE_MAX,SIZE_MAX,b,SIZE_MAX,SIZE_MAX,1,2,0));
 printf("PASS: production vector/scalar exact equality on %llu pixels; random BGRA+alpha/full1125x2436/maxwidth/16unaligned bases/oddstrides/tails/inversion/padding/inputimmutability/shortplane/no-write errors\n",(unsigned long long)pixels);
}
