/* Userland-only SIMD SDR packing. The canonical scalar/kernel contract is
 * dream_b3a8.h; same bounds, byte ordering, orientation and disjoint-storage
 * preconditions. Never include SIMD in the kernel display/unpack path.
 */
#ifndef DREAM_B3A8_VECTOR_H
#define DREAM_B3A8_VECTOR_H
#include "dream_b3a8.h"
#include <string.h>
#if (defined(__aarch64__) || defined(DREAM_B3A8_VECTOR_TEST)) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define DREAM_B3A8_PACK_NAME "simd4"
typedef uint32_t dream_b3a8_u32x4 __attribute__((vector_size(16)));
typedef uint8_t dream_b3a8_u8x4 __attribute__((vector_size(4)));
static __attribute__((noinline)) int
dream_b3a8_pack_vector(uint8_t *rgb,size_t nr,size_t rr,uint8_t *alpha,size_t na,size_t ra,
    const uint8_t *bgra,size_t nb,size_t rb,uint32_t w,uint32_t h,int invert)
{
    if (!bgra||!rgb||!alpha||!dream_b3a8_span(nb,rb,w,h,4)||
        !dream_b3a8_span(nr,rr,w,h,4)||!dream_b3a8_span(na,ra,w,h,1)) return 0;
    const dream_b3a8_u32x4 mask={255,255,255,255},bias={384,384,384,384};
    for(uint32_t y=0;y<h;++y) {
        size_t dy=invert?h-1-y:y;
        uint8_t *r=rgb+dy*rr,*a=alpha+dy*ra;
        const uint8_t *b=bgra+(size_t)y*rb;
        uint32_t x=0;
        for(;w-x>=4;x+=4) {
            dream_b3a8_u32x4 v;
            memcpy(&v,b+4*x,sizeof(v)); // Unaligned row bases are valid; no overread into padding.
            dream_b3a8_u32x4 packed=(bias+((v&mask)<<1))|
                ((bias+(((v>>8)&mask)<<1))<<10)|((bias+(((v>>16)&mask)<<1))<<20);
            dream_b3a8_u8x4 alphas=__builtin_convertvector(v>>24,dream_b3a8_u8x4);
            memcpy(r+4*x,&packed,sizeof(packed));memcpy(a+x,&alphas,sizeof(alphas));
        }
        for(;x<w;++x) {
            uint32_t v=(384u+2u*b[4*x])|(384u+2u*b[4*x+1])<<10|(384u+2u*b[4*x+2])<<20;
            r[4*x]=(uint8_t)v;r[4*x+1]=(uint8_t)(v>>8);
            r[4*x+2]=(uint8_t)(v>>16);r[4*x+3]=(uint8_t)(v>>24);a[x]=b[4*x+3];
        }
    }
    return 1;
}
#else
#define DREAM_B3A8_PACK_NAME "scalar"
#define dream_b3a8_pack_vector dream_b3a8_pack
#endif
#endif
