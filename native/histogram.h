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
static inline double histogram_linear_mean(const uint16_t bins[256]) {
    const double *light = color_light_table();
    uint64_t count = 0;
    double total = 0;
    for (unsigned b = 0; b < 256; b++) {
        count += bins[b];
        total += (double)bins[b] * light[b];
    }
    return count ? total / (double)count : 0;
}
static inline unsigned char histogram_mean(const uint16_t bins[256]) {
    return color_encode(histogram_linear_mean(bins));
}
/* Mean of the pixels above a black threshold. Renormalising the remaining
 * counts removes black-background dilution without changing the channel
 * ratios of the active pixels. Falls back to the full mean when a region has
 * no pixel above the threshold.
 *
 * In a mostly-black region the denominator is tiny, so a handful of codec
 * dither pixels crossing the threshold can swing the mean between full black
 * and the lit object. That is visible as flicker on the LEDs. Codec/
 * near-black dither lives at or below the same 12-level noise floor used for
 * blackPercent, so use that floor even when the measured black reference is
 * 0/1. A region with any real pixel above the floor still keeps the full
 * active mean, so a lit patch is not dimmed. */
#define HISTOGRAM_ACTIVE_NOISE_FLOOR 12
static inline double histogram_active_linear_mean(const uint16_t bins[256], unsigned threshold) {
    const double *light = color_light_table();
    if (threshold < HISTOGRAM_ACTIVE_NOISE_FLOOR)
        threshold = HISTOGRAM_ACTIVE_NOISE_FLOOR;
    uint64_t count = 0;
    double total = 0;
    for (unsigned b = threshold < 255 ? threshold + 1 : 256; b < 256; b++) {
        count += bins[b];
        total += (double)bins[b] * light[b];
    }
    return count ? total / (double)count : histogram_linear_mean(bins);
}
/* Percentile of the pixel population, in linear light. The bins are the exact
 * distribution, so a percentile keeps the bright end that a mean washes out:
 * half a screen of black beside half of white has exactly the same mean as flat
 * mid-grey, and only the distribution can tell them apart.
 *
 * permille is thousandths of the population; 1000 selects the brightest
 * non-empty bin. */
static inline double histogram_linear_percentile(const uint16_t bins[256], unsigned permille) {
    const double *light = color_light_table();
    uint64_t total = 0, seen = 0;
    for (unsigned b = 0; b < 256; b++)
        total += bins[b];
    if (!total)
        return 0;
    if (permille > 1000)
        permille = 1000;
    uint64_t target = (total * permille + 999) / 1000;
    if (!target)
        target = 1;
    for (unsigned b = 0; b < 256; b++) {
        seen += bins[b];
        if (seen >= target)
            return light[b];
    }
    return light[255];
}
/* Colour of every region as the plain mean, for decisions rather than display.
 *
 * The letterbox probe and the r14 bar meter were calibrated against the mean and
 * must stay on it: a p50 profile reads 0 in a dark-but-lit interior, which hides
 * every letterbox, and a peak reading reports one bright pixel in the bar strip
 * as "no bar". Keeping this separate is what lets the display statistic change
 * without changing what the service decides. */
static inline void histogram_detection_colors(const Histogram hist[16],
                                              unsigned char out[16][3]) {
    for (unsigned i = 0; i < 16; i++)
        for (unsigned c = 0; c < 3; c++)
            out[i][c] = color_encode(histogram_linear_mean(hist[i].bins[c]));
}
#endif
