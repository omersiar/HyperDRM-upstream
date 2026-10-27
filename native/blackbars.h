#ifndef BLACKBARS_H
#define BLACKBARS_H
/* Calibrated preset-0 edge probes on this G4. Return -1 for an ambiguous
 * or dark frame so darkness alone cannot switch the active capture mode. */
static inline int letterbox_candidate(const unsigned black[16], const unsigned char color[16][3]) {
    const unsigned interior[] = {2, 6, 10, 15};
    unsigned lit = 0, interior_black = 0, interior_peak = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned r = interior[i], peak = color[r][0];
        for (unsigned c = 1; c < 3; c++)
            if (color[r][c] > peak)
                peak = color[r][c];
        if (black[r] < 20 && peak > 35) {
            lit++;
            interior_black += black[r];
            interior_peak += peak;
        }
    }
    if (lit < 2)
        return -1;
    unsigned top = (black[3] + black[11] + black[12]) / 3;
    unsigned bottom = (black[1] + black[5] + black[9]) / 3;
    if (top >= 55 && bottom >= 25)
        return 1;
    /* Thin bars occupy only part of an edge probe. Require a broad upper
     * dark edge, lower-edge support and contrast against lit interior probes.
     * This remains a histogram heuristic, not a pixel-accurate crop. */
    const unsigned top_regions[] = {3, 11, 12};
    const unsigned bottom_regions[] = {1, 5, 9};
    unsigned top_support = 0, bottom_support = 0, top_peak = 0;
    for (unsigned i = 0; i < 3; i++) {
        unsigned r = top_regions[i], peak = color[r][0];
        for (unsigned c = 1; c < 3; c++)
            if (color[r][c] > peak)
                peak = color[r][c];
        top_peak += peak;
        top_support += black[r] >= 12;
        bottom_support += black[bottom_regions[i]] >= 8;
    }
    unsigned baseline = interior_black / lit;
    if (top_support == 3 && bottom_support >= 2 && top >= 12 && bottom >= 10 &&
        top >= baseline + 8 && bottom >= baseline + 8 &&
        (unsigned long)top_peak * lit * 100 <= (unsigned long)interior_peak * 3 * 60)
        return 1;
    if (top < 20 && bottom < 20)
        return 0;
    return -1;
}
#endif
