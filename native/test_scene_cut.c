#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "scene_cut.h"

/* A realistic frame: every region/channel holds a block of counts over a range
 * of bins, the way a picture spreads its pixels. Single-bin frames are useless
 * here: two of them are either identical or disjoint, and only the second is
 * what the detector is being asked about. */
static void frame(Histogram hist[16], unsigned from, unsigned to, uint16_t count) {
    memset(hist, 0, sizeof(Histogram) * 16);
    for (unsigned r = 0; r < 16; r++)
        for (unsigned c = 0; c < 3; c++)
            for (unsigned b = from; b < to && b < 256; b++)
                hist[r].bins[c][b] = count;
}

int main(void) {
    SceneCut state;
    Histogram hist[16];
    unsigned distance = 0;
    memset(&state, 0, sizeof(state));

    /* The first frame has nothing to compare against, so it cannot be a cut. */
    frame(hist, 60, 140, 100);
    assert(!scene_cut_frame(&state, hist, &distance));
    assert(distance == 0);

    /* An identical frame has no distance at all. */
    assert(!scene_cut_frame(&state, hist, &distance));
    assert(distance == 0);

    /* Camera motion slides the distribution a few bins: measurable, far below a
     * cut. Four bins out of eighty is 50 permille. */
    frame(hist, 64, 144, 100);
    assert(!scene_cut_frame(&state, hist, &distance));
    assert(distance > 0 && distance < 100);

    /* A twenty-bin slide is a big move and still eased: the two ranges overlap,
     * which is what separates a pan from a shot change. Twelve bins a frame is
     * also where a fast pan lives, and it has to stay below the threshold. */
    frame(hist, 84, 164, 100);
    assert(!scene_cut_frame(&state, hist, &distance));
    assert(distance < SCENE_CUT_DISTANCE);

    /* A different picture is a cut even though a mean of the counts can barely
     * move: the mass left one range for another. */
    frame(hist, 160, 240, 100);
    assert(scene_cut_frame(&state, hist, &distance));
    assert(distance >= SCENE_CUT_DISTANCE);

    /* No mass at all in either frame must not divide by zero. */
    memset(&state, 0, sizeof(state));
    memset(hist, 0, sizeof(Histogram) * 16);
    assert(!scene_cut_frame(&state, hist, &distance));
    assert(distance == 0);
    assert(!scene_cut_frame(&state, hist, &distance));
    assert(distance == 0);

    /* The same picture going ten times darker keeps every bin ratio, so only a
     * detector that compares counts can see it -- and it must: a lamp being
     * switched goes out at once, not after a confirmation. */
    memset(&state, 0, sizeof(state));
    frame(hist, 90, 130, 100);
    assert(!scene_cut_frame(&state, hist, &distance));
    frame(hist, 90, 130, 10);
    assert(scene_cut_frame(&state, hist, &distance));
    assert(distance >= SCENE_CUT_DISTANCE);
    printf("PASS: motion eased, a new picture snapped (%.0f permille)\n", (double)distance);
    return 0;
}
