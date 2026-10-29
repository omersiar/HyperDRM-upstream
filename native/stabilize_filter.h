#ifndef HYPERDRM_STABILIZE_FILTER_H
#define HYPERDRM_STABILIZE_FILTER_H
/* LG's region means alternate between two states on a static picture. The
 * filter must remove that without adding hundreds of milliseconds of lag.
 *
 * Startup: collect three samples and start from their median, so the first
 * dither state cannot lock the output high or low.
 * Running: a three-sample median removes short pulses, then small drifts are
 * tracked and a large jump must persist for three samples unless it is far
 * larger than the dither, in which case it is taken at once. A real scene
 * change arrives in about 50 ms at 25 ms capture. A final three-level deadband
 * holds the remaining one- or two-level residual; a slow leak lets real fades
 * through. */
#include <stdbool.h>
#include <string.h>
#define STABILIZE_THRESHOLD 4
#define STABILIZE_SAMPLES 3
#define STABILIZE_SEED 3
#define STABILIZE_HISTORY 3
#define STABILIZE_DEADBAND 3
#define STABILIZE_LEAK 12
/* A scene change is orders of magnitude larger than the one-code dither this
 * filter removes, so it needs none of the confirm window. */
#define STABILIZE_JUMP 32
typedef struct {
    unsigned char output[16][3];
    unsigned char display[16][3];
    unsigned char candidate[16][3];
    unsigned char count[16][3];
    unsigned char stuck[16][3];
    unsigned char direction[16][3];
    unsigned char seed[STABILIZE_SEED][16][3];
    unsigned char history[STABILIZE_HISTORY][16][3];
    unsigned seed_count;
    unsigned history_count;
    bool ready;
} StabilizeFilter;
static inline void stabilize_reset(StabilizeFilter *filter) {
    memset(filter, 0, sizeof(*filter));
}
static inline unsigned char stabilize_median(const unsigned char *values, unsigned count) {
    unsigned char sorted[STABILIZE_SEED];
    memcpy(sorted, values, count);
    for (unsigned i = 1; i < count; i++) {
        unsigned char value = sorted[i];
        unsigned j = i;
        while (j && sorted[j - 1] > value) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = value;
    }
    return sorted[(count - 1) / 2];
}
static inline void stabilize_push(StabilizeFilter *filter, const unsigned char input[16][3],
                                  unsigned char output[16][3]) {
    if (!filter->ready) {
        memcpy(filter->seed[filter->seed_count++], input, sizeof(filter->seed[0]));
        for (unsigned r = 0; r < 16; r++)
            for (unsigned c = 0; c < 3; c++) {
                unsigned char values[STABILIZE_SEED];
                for (unsigned i = 0; i < filter->seed_count; i++)
                    values[i] = filter->seed[i][r][c];
                filter->output[r][c] = stabilize_median(values, filter->seed_count);
            }
        if (filter->seed_count == STABILIZE_SEED) {
            memcpy(filter->history, filter->seed, sizeof(filter->history));
            filter->history_count = STABILIZE_HISTORY;
            filter->ready = true;
        }
        memcpy(filter->display, filter->output, sizeof(filter->display));
        memcpy(output, filter->display, sizeof(filter->display));
        return;
    }
    memcpy(filter->history[filter->history_count % STABILIZE_HISTORY], input,
           sizeof(filter->history[0]));
    filter->history_count++;
    for (unsigned r = 0; r < 16; r++)
        for (unsigned c = 0; c < 3; c++) {
            unsigned char values[STABILIZE_HISTORY];
            for (unsigned i = 0; i < STABILIZE_HISTORY; i++)
                values[i] = filter->history[i][r][c];
            int current = filter->output[r][c];
            int value = stabilize_median(values, STABILIZE_HISTORY);
            int difference = value - current;
            if (difference >= STABILIZE_JUMP || difference <= -STABILIZE_JUMP) {
                /* A cut or a flash, not dither: take it without waiting for the
                 * confirm window. The median still has to move first, so this
                 * lands on the second sample instead of the fourth. */
                filter->output[r][c] = (unsigned char)value;
                filter->candidate[r][c] = (unsigned char)value;
                filter->count[r][c] = 0;
            } else if (difference >= -STABILIZE_THRESHOLD &&
                       difference <= STABILIZE_THRESHOLD) {
                if (difference)
                    filter->output[r][c] =
                        (unsigned char)(current + (difference > 0 ? (difference + 1) / 2
                                                                  : (difference - 1) / 2));
                filter->count[r][c] = 0;
            } else {
                int candidate = filter->candidate[r][c];
                bool same_direction =
                    filter->count[r][c] && ((value > current) == (candidate > current));
                if (!same_direction) {
                    filter->candidate[r][c] = (unsigned char)value;
                    filter->count[r][c] = 1;
                } else if (++filter->count[r][c] >= STABILIZE_SAMPLES) {
                    filter->output[r][c] = (unsigned char)value;
                    filter->count[r][c] = 0;
                }
            }
            int shown = filter->display[r][c];
            int next = filter->output[r][c];
            int step = next - shown;
            if (step > STABILIZE_DEADBAND || step < -STABILIZE_DEADBAND) {
                shown = next;
                filter->stuck[r][c] = 0;
                filter->direction[r][c] = 0;
            } else if (step) {
                unsigned char direction = step > 0 ? 1 : 2;
                if (filter->direction[r][c] != direction) {
                    filter->direction[r][c] = direction;
                    filter->stuck[r][c] = 0;
                }
                if (++filter->stuck[r][c] >= STABILIZE_LEAK) {
                    shown += step > 0 ? 1 : -1;
                    filter->stuck[r][c] = 0;
                }
            } else {
                filter->stuck[r][c] = 0;
                filter->direction[r][c] = 0;
            }
            filter->display[r][c] = (unsigned char)shown;
            output[r][c] = filter->display[r][c];
        }
}
#endif
