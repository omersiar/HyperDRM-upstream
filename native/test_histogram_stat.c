#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "histogram.h"
int main(void) {
    uint16_t bins[256];
    /* Half black, half white. The mean is mid-grey; the percentiles are not.
     * That difference is the whole reason to keep the distribution. */
    memset(bins, 0, sizeof(bins));
    bins[0] = 500;
    bins[255] = 500;
    unsigned mean = histogram_mean(bins);
    assert(mean >= 185 && mean <= 191);
    assert(histogram_linear_percentile(bins, 500) == color_light_table()[0]);
    assert(histogram_linear_percentile(bins, 900) == color_light_table()[255]);
    /* A uniform region makes every statistic agree. */
    memset(bins, 0, sizeof(bins));
    bins[64] = 1000;
    assert(histogram_linear_percentile(bins, 500) == color_light_table()[64]);
    assert(histogram_linear_percentile(bins, 1000) == color_light_table()[64]);
    /* An empty histogram must not divide by zero. */
    memset(bins, 0, sizeof(bins));
    assert(histogram_linear_percentile(bins, 900) == 0.0);
    /* One bright pixel among black: the peak finds it, the median does not. */
    bins[0] = 999;
    bins[200] = 1;
    assert(histogram_linear_percentile(bins, 1000) == color_light_table()[200]);
    assert(histogram_linear_percentile(bins, 500) == color_light_table()[0]);
    /* A percentile above the population saturates at the brightest bin. */
    memset(bins, 0, sizeof(bins));
    bins[10] = 7;
    bins[240] = 3;
    assert(histogram_linear_percentile(bins, 1000) == color_light_table()[240]);
    assert(histogram_linear_percentile(bins, 5000) == color_light_table()[240]);
    assert(histogram_linear_percentile(bins, 1) == color_light_table()[10]);
    /* The `active` statistic ignores the black background: a small bright object
     * on black reads as the object, where the mean reads as a dim mix and the
     * percentiles see only the background. */
    memset(bins, 0, sizeof(bins));
    bins[0] = 900;
    bins[230] = 100;
    unsigned logo_mean = histogram_mean(bins);
    unsigned logo_active = color_encode(histogram_active_linear_mean(bins, 16));
    assert(logo_mean < 90);
    assert(logo_active == 230);
    assert(histogram_linear_percentile(bins, 500) == color_light_table()[0]);
    /* Nothing above the threshold falls back to the mean, so a dark region is
     * never forced to black by the option. */
    memset(bins, 0, sizeof(bins));
    bins[3] = 600;
    bins[5] = 300;
    bins[8] = 100;
    assert(color_encode(histogram_active_linear_mean(bins, 16)) == histogram_mean(bins));
    puts("PASS: percentile selection keeps the bright end of the distribution");
    return 0;
}
