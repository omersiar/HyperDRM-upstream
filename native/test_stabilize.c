#include <assert.h>
#include <string.h>
#include "stabilize_filter.h"

int main(void) {
    StabilizeFilter filter;
    unsigned char input[16][3], output[16][3];
    stabilize_reset(&filter);

    /* Seed with a mostly-dark pattern so the first state cannot lock high. */
    unsigned char seed_values[3] = {0, 0, 15};
    for (unsigned i = 0; i < 3; i++) {
        memset(input, seed_values[i], sizeof(input));
        stabilize_push(&filter, input, output);
    }
    assert(output[0][0] == 0);

    /* One-frame dither pulses do not reach the output. */
    for (unsigned i = 0; i < 60; i++) {
        memset(input, (i % 3 == 0) ? 15 : 0, sizeof(input));
        stabilize_push(&filter, input, output);
        assert(output[0][0] == 0);
    }

    /* A real step arrives about two samples after the seed: the median needs one
     * sample to move, and a jump this large skips the confirm window. */
    stabilize_reset(&filter);
    memset(input, 0, sizeof(input));
    for (unsigned i = 0; i < 3; i++)
        stabilize_push(&filter, input, output);
    memset(input, 100, sizeof(input));
    stabilize_push(&filter, input, output);
    assert(output[0][0] == 0);
    stabilize_push(&filter, input, output);
    assert(output[0][0] == 100);

    /* Slow fades are tracked, not frozen. */
    stabilize_reset(&filter);
    memset(input, 0, sizeof(input));
    for (unsigned i = 0; i < 3; i++)
        stabilize_push(&filter, input, output);
    for (unsigned i = 1; i < 12; i++) {
        memset(input, i * 2, sizeof(input));
        stabilize_push(&filter, input, output);
    }
    assert(output[0][0] >= 10);

    /* Channels are independent. */
    stabilize_reset(&filter);
    memset(input, 10, sizeof(input));
    for (unsigned i = 0; i < 3; i++)
        stabilize_push(&filter, input, output);
    for (unsigned i = 0; i < 10; i++) {
        memset(input, 10, sizeof(input));
        input[4][1] = 200;
        stabilize_push(&filter, input, output);
    }
    assert(output[0][0] == 10 && output[4][1] == 200);
    return 0;
}
