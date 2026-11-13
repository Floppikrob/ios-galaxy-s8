/* Full-buffer XXH3, including the tail that the former eight-byte loop omitted.
 * xxHash v0.8.2 is BSD-2-Clause; retain third_party/xxhash/xxhash.h's license.
 * AArch64 uses the baseline NEON implementation, with no LSE/SVE requirement.
 */
#include "surface_checksum.h"
#define XXH_INLINE_ALL
#define XXH_NO_STREAM
#include "third_party/xxhash/xxhash.h"

uint64_t
hakios_surface_checksum(const void *pixels, size_t bytes)
{
    return XXH3_64bits(pixels, bytes);
}
