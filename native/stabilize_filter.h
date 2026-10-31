#ifndef HYPERDRM_STABILIZE_FILTER_H
#define HYPERDRM_STABILIZE_FILTER_H
/* LG's region means wobble by a few codes from one frame to the next on a still
 * picture. The LEDs must not flicker, but they must not lag the picture either:
 * every sample this filter holds back is one capture period of delay (33 ms at
 * 30 fps), and that is exactly what a receiver latency measurement reports.
 *
 * Startup: three samples, started from their median, so the first dither state
 * cannot lock the output high or low.
 * Running: a difference of four codes or less is dither. It is taken halfway at
 * once, so it neither flickers nor waits. Anything larger is believed on the
 * first frame when it is brighter and on the second when it is darker: a shot
 * that brightens must not lag, and a one-frame darker artefact is what the
 * confirmation is for. A change larger than 32 codes is real light -- a flash,
 * a lamp being switched -- and goes out in the same frame either way.
 * A scene cut skips all of it: scene_cut.h detects it from the histograms and
 * stabilize_snap adopts the new picture whole.
 * Finally a three-level deadband holds the last one or two codes of residual,
 * with a small leak so a real fade still gets through. */
#include <stdbool.h>
#include <string.h>
#define STABILIZE_THRESHOLD 4
/* Attack is immediate, release is confirmed. The LEDs must not lag the moment a
 * shot gets brighter -- that is the change a viewer notices -- while a one-frame
 * darker artefact is exactly what the confirmation exists to reject. Two
 * samples of release is 50 ms at 40 fps: eased, not sluggish. */
#define STABILIZE_ATTACK 1
#define STABILIZE_RELEASE 2
#define STABILIZE_SEED 3
#define STABILIZE_DEADBAND 3
/* Small steps creep towards the measured value instead of jumping, which keeps
 * one-code wobble off the LEDs. Six samples still hides it and halves the fade
 * lag the old twelve produced. */
#define STABILIZE_LEAK 6
/* A cut or a lamp being switched is orders of magnitude larger than dither, so
 * it is taken from the raw sample on the first frame. */
#define STABILIZE_JUMP 32
typedef struct {
    unsigned char output[16][3];
    unsigned char display[16][3];
    unsigned char candidate[16][3];
    unsigned char count[16][3];
    unsigned char stuck[16][3];
    unsigned char direction[16][3];
    unsigned char seed[STABILIZE_SEED][16][3];
    unsigned seed_count;
    bool ready;
} StabilizeFilter;
static inline void stabilize_reset(StabilizeFilter *filter) {
    memset(filter, 0, sizeof(*filter));
}
/* Scene cut: adopt the new picture whole. There is nothing to confirm, because
 * the picture it would be confirmed against is gone, and the seed window would
 * otherwise spend two frames on the median of the old shot and the new one. */
static inline void stabilize_snap(StabilizeFilter *filter, const unsigned char input[16][3]) {
    memcpy(filter->output, input, sizeof(filter->output));
    memcpy(filter->display, input, sizeof(filter->display));
    memset(filter->count, 0, sizeof(filter->count));
    memset(filter->stuck, 0, sizeof(filter->stuck));
    memset(filter->direction, 0, sizeof(filter->direction));
    filter->seed_count = STABILIZE_SEED;
    filter->ready = true;
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
        if (filter->seed_count == STABILIZE_SEED)
            filter->ready = true;
        memcpy(filter->display, filter->output, sizeof(filter->display));
        memcpy(output, filter->display, sizeof(filter->display));
        return;
    }
    for (unsigned r = 0; r < 16; r++)
        for (unsigned c = 0; c < 3; c++) {
            int current = filter->output[r][c];
            int value = input[r][c];
            int difference = value - current;
            if (difference >= STABILIZE_JUMP || difference <= -STABILIZE_JUMP) {
                /* Real light, not dither: this frame, no confirmation. */
                filter->output[r][c] = (unsigned char)value;
                filter->count[r][c] = 0;
            } else if (difference >= -STABILIZE_THRESHOLD &&
                       difference <= STABILIZE_THRESHOLD) {
                /* Dither: follow it halfway so it never flickers, and never
                 * make it wait. */
                if (difference)
                    filter->output[r][c] =
                        (unsigned char)(current + (difference > 0 ? (difference + 1) / 2
                                                                  : (difference - 1) / 2));
                filter->count[r][c] = 0;
            } else {
                bool same_direction = filter->count[r][c] &&
                                      ((value > current) == (filter->candidate[r][c] > current));
                if (!same_direction) {
                    filter->candidate[r][c] = (unsigned char)value;
                    filter->count[r][c] = 1;
                } else if (filter->count[r][c] < 255) {
                    filter->count[r][c]++;
                }
                unsigned needed = value > current ? STABILIZE_ATTACK : STABILIZE_RELEASE;
                if (filter->count[r][c] >= needed) {
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
