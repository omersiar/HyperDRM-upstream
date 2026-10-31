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

    /* A one-frame darkening is the shape a decoder artefact takes, and it does
     * not reach the output: release needs two samples. A one-frame brightening
     * does pass, which is the price of an attack that never lags. */
    memset(input, 15, sizeof(input));
    for (unsigned i = 0; i < 3; i++)
        stabilize_push(&filter, input, output);
    assert(output[0][0] == 15);
    for (unsigned i = 0; i < 60; i++) {
        memset(input, (i % 3 == 0) ? 0 : 15, sizeof(input));
        stabilize_push(&filter, input, output);
        assert(output[0][0] == 15);
    }

    /* A real step is out on the very first sample. It is real light, and the
     * median's second sample was pure latency. */
    stabilize_reset(&filter);
    memset(input, 0, sizeof(input));
    for (unsigned i = 0; i < 3; i++)
        stabilize_push(&filter, input, output);
    memset(input, 100, sizeof(input));
    stabilize_push(&filter, input, output);
    assert(output[0][0] == 100);

    /* A mid-sized change -- above the dither band, below a cut -- goes out on
     * the first sample when it is brighter and on the second when it is darker.
     * Lifting the LEDs must not lag; a one-frame darker artefact is what the
     * confirmation is for. */
    stabilize_reset(&filter);
    memset(input, 0, sizeof(input));
    for (unsigned i = 0; i < 3; i++)
        stabilize_push(&filter, input, output);
    memset(input, 20, sizeof(input));
    stabilize_push(&filter, input, output);
    assert(output[0][0] == 20);
    memset(input, 0, sizeof(input));
    stabilize_push(&filter, input, output);
    assert(output[0][0] == 20);
    stabilize_push(&filter, input, output);
    assert(output[0][0] == 0);

    /* A scene cut is adopted whole, without a seed window: the picture it would
     * be confirmed against is gone, and the median of two different shots is a
     * colour neither of them had. */
    stabilize_reset(&filter);
    memset(input, 5, sizeof(input));
    for (unsigned i = 0; i < 3; i++)
        stabilize_push(&filter, input, output);
    unsigned char cut[16][3];
    memset(cut, 180, sizeof(cut));
    stabilize_snap(&filter, cut);
    assert(filter.display[0][0] == 180);
    memset(input, 180, sizeof(input));
    stabilize_push(&filter, input, output);
    assert(output[0][0] == 180);
    /* After a cut the ordinary rules resume: a 30-code drop is confirmed once,
     * a step beyond the jump band is real light and goes out at once. */
    memset(input, 150, sizeof(input));
    stabilize_push(&filter, input, output);
    assert(output[0][0] == 180);
    stabilize_push(&filter, input, output);
    assert(output[0][0] == 150);
    memset(input, 60, sizeof(input));
    stabilize_push(&filter, input, output);
    assert(output[0][0] == 60);

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
