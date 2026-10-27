#ifndef HYPERDRM_CONTINUITY_H
#define HYPERDRM_CONTINUITY_H
/* Zero population is missing data, while populated bin zero is real black. */
static inline bool histogram_populated(const Histogram hist[16]) {
    for (unsigned r = 0; r < 16; r++)
        for (unsigned c = 0; c < 3; c++) {
            uint64_t total = 0;
            for (unsigned b = 0; b < 256; b++)
                total += hist[r].bins[c][b];
            if (!total)
                return false;
        }
    return true;
}
typedef struct {
    unsigned char last[16][3], from[16][3];
    uint64_t valid_at, fade_at, missing_at;
    bool available, fading, holding;
} Continuity;
static inline void continuity_seed(Continuity *s, const unsigned char colors[16][3], uint64_t now) {
    memcpy(s->last, colors, sizeof(s->last));
    memcpy(s->from, colors, sizeof(s->from));
    s->valid_at = now;
    s->available = true;
    s->fading = true;
    s->fade_at = 0;
    s->missing_at = 0;
    s->holding = false;
}
static inline void continuity_mix(const unsigned char from[16][3], const unsigned char to[16][3],
                                  unsigned elapsed, unsigned char out[16][3]) {
    if (elapsed > 500)
        elapsed = 500;
    for (unsigned r = 0; r < 16; r++)
        for (unsigned c = 0; c < 3; c++)
            out[r][c] = (from[r][c] * (500 - elapsed) + to[r][c] * elapsed + 250) / 500;
}
static inline bool continuity_frame(Continuity *s, bool valid, const unsigned char input[16][3],
                                    uint64_t now, unsigned char out[16][3]) {
    if (!valid) {
        if (!s->missing_at)
            s->missing_at = now;
        s->holding = s->available && now - s->missing_at < 1500;
        if (s->holding)
            memcpy(out, s->last, sizeof(s->last));
        return s->holding;
    }
    if (s->holding) {
        memcpy(s->from, s->last, sizeof(s->from));
        s->fading = true;
        s->fade_at = 0;
    }
    s->holding = false;
    s->missing_at = 0;
    /* Near-black codec rounding is valid darkness, not a lost measurement. */
    bool black = true;
    for (unsigned r = 0; r < 16; r++)
        for (unsigned c = 0; c < 3; c++)
            if (input[r][c] > 3)
                black = false;
    if (black)
        s->fading = false;
    if (s->fading) {
        if (!s->fade_at)
            s->fade_at = now;
        continuity_mix(s->from, input, (unsigned)(now - s->fade_at), out);
        if (now - s->fade_at >= 500)
            s->fading = false;
    } else
        memcpy(out, input, sizeof(s->last));
    memcpy(s->last, out, sizeof(s->last));
    s->available = true;
    s->valid_at = now;
    return true;
}
#endif
