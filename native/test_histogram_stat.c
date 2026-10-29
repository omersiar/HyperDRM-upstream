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
    puts("PASS: percentile selection keeps the bright end of the distribution");
    return 0;
}
