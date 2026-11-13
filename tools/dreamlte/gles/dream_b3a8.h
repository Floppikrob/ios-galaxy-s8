/* SDR bridge for uncompressed IOSurface 'b3a8': X2R10G10B10 wide RGB + A8.
 * RGB code 384 is black; an 8-bit SDR step is two codes (white 894).
 * The extra range is clipped on the 8-bit panel. No premultiplication change.
 * Callers own locking, GPU completion, allocation, and disjoint storage.
 */
#ifndef DREAM_B3A8_H
#define DREAM_B3A8_H
#include <stddef.h>
#include <stdint.h>

static inline int
dream_b3a8_span(size_t bytes, size_t row, uint32_t w, uint32_t h, unsigned bpe)
{
    if (!w || !h || w > 16384 || h > 16384 || (bpe != 1 && bpe != 4)) return 0;
    uint64_t used = (uint64_t)w * bpe;
    return row >= used && (uint64_t)row <= UINT64_MAX / h &&
        (uint64_t)row * (h - 1) + used <= bytes;
}

static inline uint8_t dream_b3a8_sdr(unsigned code)
{
    return code <= 384 ? 0 : code >= 894 ? 255 : (uint8_t)((code - 384 + 1) / 2);
}

static inline int
dream_b3a8_unpack(uint8_t *bgra, size_t nb, size_t rb,
    const uint8_t *rgb, size_t nr, size_t rr, const uint8_t *alpha, size_t na, size_t ra,
    uint32_t w, uint32_t h, int invert)
{
    if (!bgra || !rgb || !alpha || !dream_b3a8_span(nb, rb, w, h, 4) ||
        !dream_b3a8_span(nr, rr, w, h, 4) || !dream_b3a8_span(na, ra, w, h, 1)) return 0;
    for (uint32_t y = 0; y < h; ++y) {
        size_t sy = invert ? h - 1 - y : y;
        const uint8_t *r = rgb + sy * rr, *a = alpha + sy * ra;
        uint8_t *b = bgra + (size_t)y * rb;
        for (uint32_t x = 0; x < w; ++x, r += 4, b += 4) {
            uint32_t v = (uint32_t)r[0] | (uint32_t)r[1] << 8 | (uint32_t)r[2] << 16 | (uint32_t)r[3] << 24;
            b[0] = dream_b3a8_sdr(v & 1023);
            b[1] = dream_b3a8_sdr((v >> 10) & 1023);
            b[2] = dream_b3a8_sdr((v >> 20) & 1023);
            b[3] = a[x];
        }
    }
    return 1;
}

static inline int
dream_b3a8_pack(uint8_t *rgb, size_t nr, size_t rr, uint8_t *alpha, size_t na, size_t ra,
    const uint8_t *bgra, size_t nb, size_t rb, uint32_t w, uint32_t h, int invert)
{
    if (!bgra || !rgb || !alpha || !dream_b3a8_span(nb, rb, w, h, 4) ||
        !dream_b3a8_span(nr, rr, w, h, 4) || !dream_b3a8_span(na, ra, w, h, 1)) return 0;
    for (uint32_t y = 0; y < h; ++y) {
        size_t dy = invert ? h - 1 - y : y;
        uint8_t *r = rgb + dy * rr, *a = alpha + dy * ra;
        const uint8_t *b = bgra + (size_t)y * rb;
        for (uint32_t x = 0; x < w; ++x, r += 4, b += 4) {
            uint32_t v = (384u + 2u * b[0]) | (384u + 2u * b[1]) << 10 | (384u + 2u * b[2]) << 20;
            r[0] = (uint8_t)v; r[1] = (uint8_t)(v >> 8); r[2] = (uint8_t)(v >> 16); r[3] = (uint8_t)(v >> 24);
            a[x] = b[3];
        }
    }
    return 1;
}
#endif
