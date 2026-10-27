#ifndef COLOR_BOOST_H
#define COLOR_BOOST_H
/* Preserve the original saturation and brightness. */
static inline void boost_color(const unsigned char in[3], unsigned char out[3]) {
    for (unsigned c = 0; c < 3; c++)
        out[c] = in[c];
}
#endif
