#ifndef HYPERDRM_SPATIAL_IMAGE_H
#define HYPERDRM_SPATIAL_IMAGE_H
#include <stdlib.h>
#include <stdio.h>
#include "layout.h"
#include "interpolation.h"
#include "edge_feed.h"
enum { SPATIAL_WIDTH = EDGE_WIDTH, SPATIAL_HEIGHT = EDGE_HEIGHT, SPATIAL_PIXELS = EDGE_PIXELS };
/* Synthetic plane built from histogram samples, never captured video pixels.
 * HyperHDR 22 treats JSON color as a solid color; image input retains space.
 * The receiver samples this plane using its configured LED rectangles, so one
 * plane serves every LED hardware instance.
 *
 * The pixels come from edge_feed.h: with a measured rectangle profile each one
 * is a copy of a measured region colour, never a blend and never a solved cell. */
/* Raw RGB plane for the FlatBuffer transport (no base64, no JSON). */
static inline void spatial_dimensions(unsigned *width, unsigned *height) {
    *width = SPATIAL_WIDTH;
    *height = SPATIAL_HEIGHT;
}
static void spatial_plane(const unsigned char regions[16][3], unsigned char *out) {
    edge_plane(regions, out, SPATIAL_WIDTH, SPATIAL_HEIGHT);
}
static inline char *spatial_image_command_duration(const unsigned char regions[16][3], unsigned priority,
                                            unsigned duration) {
    static unsigned char plane[SPATIAL_PIXELS * 3];
    unsigned width, height;
    spatial_dimensions(&width, &height);
    size_t pixels = (size_t)width * height;
    spatial_plane(regions, plane);
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t capacity = pixels * 4 + 256;
    char *line = malloc(capacity);
    if (!line)
        return NULL;
    size_t used = snprintf(line, capacity,
                           "{\"command\":\"image\",\"priority\":%u,\"duration\":%u,\"origin\":"
                           "\"HyperDRM\",\"imagewidth\":%u,\"imageheight\":%u,\"format\":\"rgb\","
                           "\"name\":\"Histogram colors\",\"imagedata\":\"",
                           priority, duration, width, height);
    for (size_t i = 0; i < pixels; i++) {
        const unsigned char *color = plane + i * 3;
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
#endif
