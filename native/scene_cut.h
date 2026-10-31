#ifndef HYPERDRM_SCENE_CUT_H
#define HYPERDRM_SCENE_CUT_H
#include <stdbool.h>
#include <string.h>
#include "histogram.h"
/* A cut is the one change the temporal filter must not smooth: it is the frame
 * where the picture became a different picture, and easing into it would smear
 * the first frame of every shot.
 *
 * The region means cannot see a cut -- a cut to a differently arranged scene
 * with a similar palette leaves every mean in place -- but the distributions can,
 * and they are already in hand. The distance is the normalised L1 between this
 * frame's counts and the last:
 *
 *   d = sum |h - h'| / sum (h + h')      in permille
 *
 * 0 is an identical frame, 1000 is no common mass at all. Camera motion moves
 * the marginals a little, a cut moves them a lot, and a fade moves them in one
 * direction only, slowly. */
#define SCENE_CUT_DISTANCE 400
typedef struct {
    uint16_t previous[16][3][256];
    bool ready;
} SceneCut;
static inline unsigned scene_cut_distance(const SceneCut *state, const Histogram hist[16]) {
    uint64_t difference = 0, total = 0;
    for (unsigned r = 0; r < 16; r++)
        for (unsigned c = 0; c < 3; c++)
            for (unsigned b = 0; b < 256; b++) {
                uint32_t now = hist[r].bins[c][b], before = state->previous[r][c][b];
                difference += now > before ? now - before : before - now;
                total += (uint64_t)now + before;
            }
    return total ? (unsigned)(1000 * difference / total) : 0;
}
static inline void scene_cut_store(SceneCut *state, const Histogram hist[16]) {
    for (unsigned r = 0; r < 16; r++)
        memcpy(state->previous[r], hist[r].bins, sizeof(state->previous[r]));
    state->ready = true;
}
/* True on the frame where the picture changed wholesale. distance receives the
 * measurement either way, so the threshold can be judged against real content
 * instead of guessed. */
static inline bool scene_cut_frame(SceneCut *state, const Histogram hist[16],
                                   unsigned *distance) {
    if (!state->ready) {
        scene_cut_store(state, hist);
        *distance = 0;
        return false;
    }
    unsigned value = scene_cut_distance(state, hist);
    *distance = value;
    scene_cut_store(state, hist);
    return value >= SCENE_CUT_DISTANCE;
}
#endif
