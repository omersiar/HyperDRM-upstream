#ifndef SPATIAL_INTERPOLATION_H
#define SPATIAL_INTERPOLATION_H
#include "color_space.h"
/* Mix in linear light so a transition between two lit regions stays as bright
 * as its neighbours instead of darkening towards an encoded average. */
static inline void blend_color(const unsigned char a[3], const unsigned char b[3], unsigned weight,
                               unsigned char out[3]) {
    for (unsigned c = 0; c < 3; c++) {
        if (a[c] == b[c]) {
            out[c] = a[c];
            continue;
        }
        double light = (color_light(a[c]) * (65535.0 - (double)weight) +
                        color_light(b[c]) * (double)weight) /
                       65535.0;
        out[c] = color_encode(light);
    }
}
#endif
