#ifndef SPATIAL_INTERPOLATION_H
#define SPATIAL_INTERPOLATION_H
static inline void blend_color(const unsigned char a[3], const unsigned char b[3], unsigned weight,
                               unsigned char out[3]) {
    for (unsigned c = 0; c < 3; c++)
        out[c] =
            (unsigned char)(((unsigned)a[c] * (65535 - weight) + (unsigned)b[c] * weight + 32767) /
                            65535);
}
#endif
