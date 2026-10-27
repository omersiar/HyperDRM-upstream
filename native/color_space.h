#ifndef HYPERDRM_COLOR_SPACE_H
#define HYPERDRM_COLOR_SPACE_H
#include <math.h>
/* Light, not encoded values.
 *
 * LG reports each region as the mean of encoded 8-bit samples, and a plane
 * pixel that mixes two region samples was also blended as encoded values. Both
 * under-report light: red and green average to a dim olive (125,110,35) instead
 * of the bright mix (169,147,38) the picture actually contains, so regions that
 * straddle two colours look dimmer than their surroundings on the LEDs.
 *
 * Histogram means and plane blends are therefore evaluated in linear light and
 * encoded once for output. Conversions use lookup tables, so the render loop
 * only does arithmetic; the tables are built on first use (few pow() calls). */
#define COLOR_ENCODE_SIZE 4096
static inline const double *color_light_table(void) {
    static double table[256];
    static int ready;
    if (!ready) {
        for (unsigned value = 0; value < 256; value++) {
            double c = value / 255.0;
            table[value] = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
        }
        ready = 1;
    }
    return table;
}
static inline const unsigned char *color_encode_table(void) {
    static unsigned char table[COLOR_ENCODE_SIZE + 1];
    static int ready;
    if (!ready) {
        for (unsigned i = 0; i <= COLOR_ENCODE_SIZE; i++) {
            double c = (double)i / COLOR_ENCODE_SIZE;
            double encoded = c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
            table[i] = (unsigned char)(encoded * 255 + 0.5);
        }
        /* Preserve every 8-bit level exactly across light -> encoded. */
        for (unsigned value = 0; value < 256; value++) {
            unsigned at = (unsigned)(color_light_table()[value] * COLOR_ENCODE_SIZE + 0.5);
            table[at] = (unsigned char)value;
        }
        for (unsigned i = 1; i <= COLOR_ENCODE_SIZE; i++)
            if (table[i] < table[i - 1])
                table[i] = table[i - 1];
        ready = 1;
    }
    return table;
}
/* Relative light of an encoded 8-bit level (IEC 61966-2-1). */
static inline double color_light(unsigned char value) {
    return color_light_table()[value];
}
/* Encode relative light back to an 8-bit level. */
static inline unsigned char color_encode(double light) {
    if (!(light > 0))
        return 0;
    if (light >= 1)
        return 255;
    return color_encode_table()[(unsigned)(light * COLOR_ENCODE_SIZE + 0.5)];
}
#endif
