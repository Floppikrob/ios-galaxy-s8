#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dream_b3a8.h"
int main(void) {
    /* Independent known vectors: RGB10 black=384, white=894; alpha is separate. */
    const uint8_t rgb[] = {0x80,0x01,0x06,0x18, 0x7e,0xfb,0xed,0x37, 0x80,0x01,0xe6,0x37};
    const uint8_t a[] = {0,255,127};
    uint8_t out[12];
    assert(dream_b3a8_unpack(out,sizeof out,12,rgb,sizeof rgb,12,a,sizeof a,3,3,1,0));
    assert(!memcmp(out,(uint8_t[]){0,0,0,0,255,255,255,255,0,0,255,127},12));
    /* All SDR component/alpha values, unequal strides, untouched row padding. */
    enum {W=256,H=3,RB=W*4+7,RR=W*4+13,RA=W+9};
    uint8_t bgra[RB*H], decoded[RB*H], rgbp[RR*H], alphap[RA*H];
    memset(bgra,0xa5,sizeof bgra);memset(decoded,0x69,sizeof decoded);
    memset(rgbp,0xb7,sizeof rgbp);memset(alphap,0xc8,sizeof alphap);
    for(unsigned y=0;y<H;y++) for(unsigned x=0;x<W;x++) {
        uint8_t *p=bgra+y*RB+4*x;
        p[0]=x;p[1]=255-x;p[2]=(x+y*47)&255;p[3]=(x*7+y)&255;
    }
    for(int flip=0;flip<2;flip++) {
        assert(dream_b3a8_pack(rgbp,sizeof rgbp,RR,alphap,sizeof alphap,RA,bgra,sizeof bgra,RB,W,H,flip));
        assert(dream_b3a8_unpack(decoded,sizeof decoded,RB,rgbp,sizeof rgbp,RR,alphap,sizeof alphap,RA,W,H,flip));
        for(unsigned y=0;y<H;y++) {
            assert(!memcmp(bgra+y*RB,decoded+y*RB,W*4));
            for(unsigned n=W*4;n<RB;n++) assert(decoded[y*RB+n]==0x69);
            for(unsigned n=W*4;n<RR;n++) assert(rgbp[y*RR+n]==0xb7);
            for(unsigned n=W;n<RA;n++) assert(alphap[y*RA+n]==0xc8);
        }
    }
    assert(dream_b3a8_sdr(0)==0 && dream_b3a8_sdr(383)==0 && dream_b3a8_sdr(1023)==255);
    uint8_t untouched[12];memset(untouched,0xda,sizeof untouched);
    assert(!dream_b3a8_unpack(untouched,11,12,rgb,12,12,a,3,3,3,1,0));
    assert(!dream_b3a8_unpack(untouched,12,12,rgb,11,12,a,3,3,3,1,0));
    assert(!dream_b3a8_unpack(untouched,12,12,rgb,12,12,a,2,3,3,1,0));
    for(unsigned i=0;i<12;i++) assert(untouched[i]==0xda);
    assert(!dream_b3a8_span(SIZE_MAX,SIZE_MAX,1,16384,4));
    assert(!dream_b3a8_span(16,3,1,2,4));
    assert(!dream_b3a8_span(16,4,0,1,4));
    assert(!dream_b3a8_span(16,4,1,0,4));
    assert(!dream_b3a8_span(SIZE_MAX,65536,16385,1,4));
    puts("PASS: independent RGB10/A8 vectors; 256-value colors/alpha; inversion; unequal strides; padding; bounds/overflow");
}
