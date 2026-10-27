#ifndef LG_HISTOGRAM_H
#define LG_HISTOGRAM_H
#include <stdint.h>
#include <stddef.h>
/* LG ABI: region ID followed by three planar channels, 256 uint16 bins
 * per channel. Bin index is the 8-bit intensity (0..255). */
typedef struct {
    uint32_t id;
    uint16_t bins[3][256];
} Histogram;
_Static_assert(sizeof(Histogram) == 0x604, "LG ABI record size");
_Static_assert(offsetof(Histogram, bins) == 4, "LG ABI channel offset");
static inline unsigned char histogram_mean(const uint16_t bins[256]) {
    uint64_t count = 0, weight = 0;
    for (unsigned b = 0; b < 256; b++) {
        count += bins[b];
        weight += (uint64_t)bins[b] * b;
    }
    return count ? (unsigned char)((weight + count / 2) / count) : 0;
}
#endif
