#ifndef HYPERDRM_SPATIAL_IMAGE_H
#define HYPERDRM_SPATIAL_IMAGE_H
#include <stdlib.h>
#include <stdio.h>
#include "layout.h"
#include "interpolation.h"
#include "color_boost.h"
enum { SPATIAL_WIDTH = 160, SPATIAL_HEIGHT = 90, SPATIAL_PIXELS = SPATIAL_WIDTH * SPATIAL_HEIGHT };
/* Synthetic color plane from histogram samples, never captured video pixels.
 * HyperHDR 22 treats JSON color as a solid color; image input retains space.
 * The receiver samples this plane using its configured LED rectangles. */
static char *spatial_image_command_duration(const unsigned char regions[16][3], unsigned priority,
                                            unsigned duration) {
    static LedBlend blends[SPATIAL_PIXELS];
    static unsigned generation;
    if (generation != calibration_generation) {
        for (unsigned y = 0; y < SPATIAL_HEIGHT; y++)
            for (unsigned x = 0; x < SPATIAL_WIDTH; x++)
                blends[y * SPATIAL_WIDTH + x] =
                    layout_blend((x + 0.5) / SPATIAL_WIDTH, (y + 0.5) / SPATIAL_HEIGHT);
        generation = calibration_generation;
    }
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t capacity = SPATIAL_PIXELS * 4 + 256;
    char *line = malloc(capacity);
    if (!line)
        return NULL;
    size_t used = snprintf(line, capacity,
                           "{\"command\":\"image\",\"priority\":%u,\"duration\":%u,\"origin\":"
                           "\"HyperDRM\",\"imagewidth\":%u,\"imageheight\":%u,\"format\":\"rgb\","
                           "\"name\":\"Histogram colors\",\"imagedata\":\"",
                           priority, duration, SPATIAL_WIDTH, SPATIAL_HEIGHT);
    for (unsigned i = 0; i < SPATIAL_PIXELS; i++) {
        unsigned char rgb[3], color[3];
        LedBlend b = blends[i];
        blend_color(regions[b.a], regions[b.b], b.weight, rgb);
        boost_color(rgb, color);
        line[used++] = alphabet[color[0] >> 2];
        line[used++] = alphabet[((color[0] & 3) << 4) | (color[1] >> 4)];
        line[used++] = alphabet[((color[1] & 15) << 2) | (color[2] >> 6)];
        line[used++] = alphabet[color[2] & 63];
    }
    line[used++] = '"';
    line[used++] = '}';
    line[used++] = '\n';
    line[used] = 0;
    return line;
}
static inline char *spatial_image_command(const unsigned char regions[16][3], unsigned priority) {
    return spatial_image_command_duration(regions, priority, 1500);
}
#endif
