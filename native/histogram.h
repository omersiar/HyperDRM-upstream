#ifndef LG_HISTOGRAM_H
#define LG_HISTOGRAM_H
#include <stdint.h>
#include <stddef.h>
#include "color_space.h"
/* LG ABI: region ID followed by three planar channels, 256 uint16 bins
 * per channel. Bin index is the 8-bit intensity (0..255). */
typedef struct {
    uint32_t id;
    uint16_t bins[3][256];
} Histogram;
_Static_assert(sizeof(Histogram) == 0x604, "LG ABI record size");
_Static_assert(offsetof(Histogram, bins) == 4, "LG ABI channel offset");
/* Mean in linear light. Averaging the encoded bin index would under-report any
 * region that mixes colours, which is what made mixed regions look dim on the
 * LEDs. */
static inline unsigned char histogram_mean(const uint16_t bins[256]) {
    const double *light = color_light_table();
    uint64_t count = 0;
    double total = 0;
    for (unsigned b = 0; b < 256; b++) {
        count += bins[b];
        total += (double)bins[b] * light[b];
    }
    return count ? color_encode(total / (double)count) : 0;
}
#endif
