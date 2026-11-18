/* Four-lane, byte-exact BGRA unpacking for little-endian userland. */
#ifndef DREAM_B3A8_UNPACK_VECTOR_H
#define DREAM_B3A8_UNPACK_VECTOR_H
#include "dream_b3a8.h"
#include <string.h>

#if (defined(__aarch64__) || defined(DREAM_B3A8_VECTOR_TEST)) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
typedef uint32_t dream_unpack_u32x4 __attribute__((vector_size(16)));
typedef uint8_t dream_unpack_u8x4 __attribute__((vector_size(4)));

static inline dream_unpack_u32x4
dream_unpack_sdr(dream_unpack_u32x4 value)
{
    const dream_unpack_u32x4 black_round = {383, 383, 383, 383};
    const dream_unpack_u32x4 white = {255, 255, 255, 255};
    // Saturated subtraction followed by rounding exactly matches dream_b3a8_sdr.
    dream_unpack_u32x4 light = ((value - black_round) & (value > black_round)) >> 1;
    const dream_unpack_u32x4 overflow = light > white;
    return (white & overflow) | (light & ~overflow);
}

static __attribute__((noinline)) int
dream_b3a8_unpack_vector(uint8_t *bgra, size_t nb, size_t rb,
    const uint8_t *rgb, size_t nr, size_t rr, const uint8_t *alpha, size_t na, size_t ra,
    uint32_t w, uint32_t h, int invert)
{
    if (!bgra || !rgb || !alpha || !dream_b3a8_span(nb, rb, w, h, 4) ||
        !dream_b3a8_span(nr, rr, w, h, 4) || !dream_b3a8_span(na, ra, w, h, 1)) return 0;

    const dream_unpack_u32x4 code_mask = {1023, 1023, 1023, 1023};
    for (uint32_t y = 0; y < h; ++y) {
        const size_t sy = invert ? h - 1 - y : y;
        const uint8_t *r = rgb + sy * rr, *a = alpha + sy * ra;
        uint8_t *b = bgra + (size_t)y * rb;
        uint32_t x = 0;
        for (; w - x >= 4; x += 4) {
            dream_unpack_u32x4 packed;
            dream_unpack_u8x4 opacity;
            memcpy(&packed, r + 4 * x, sizeof(packed));
            memcpy(&opacity, a + x, sizeof(opacity));
            const dream_unpack_u32x4 pixels =
                dream_unpack_sdr(packed & code_mask) |
                (dream_unpack_sdr((packed >> 10) & code_mask) << 8) |
                (dream_unpack_sdr((packed >> 20) & code_mask) << 16) |
                (__builtin_convertvector(opacity, dream_unpack_u32x4) << 24);
            memcpy(b + 4 * x, &pixels, sizeof(pixels));
        }
        // All supported widths can leave up to three pixels at the end of a row.
        if (x < w) {
            const size_t offset = 4 * x;
            if (!dream_b3a8_unpack(b + offset, rb - offset, rb, r + offset, rr - offset, rr,
                    a + x, ra - x, ra, w - x, 1, 0)) return 0;
        }
    }
    return 1;
}
#else
#define dream_b3a8_unpack_vector dream_b3a8_unpack
#endif
#endif
