#ifndef HYPERDRM_DARK_COLOR_H
#define HYPERDRM_DARK_COLOR_H
/* Suppress near-black histogram residuals without changing channel ratios.
 * Apply only to fresh histogram colors, before continuity and interpolation.
 * The 16..32 ramp is a smoothstep so a region whose peak crosses the threshold
 * while a calibration patch moves cannot jump between zero and full gain. */
static inline void dark_color_filter(unsigned char color[3]) {
    unsigned peak = color[0];
    for (unsigned c = 1; c < 3; c++)
        if (color[c] > peak)
            peak = color[c];
    if (peak >= 32)
        return;
    if (peak <= 16) {
        color[0] = color[1] = color[2] = 0;
        return;
    }
    double t = (peak - 16) / 16.0;
    double gain = t * t * (3.0 - 2.0 * t);
    for (unsigned c = 0; c < 3; c++)
        color[c] = (unsigned char)((double)color[c] * gain + 0.5);
}
#endif
