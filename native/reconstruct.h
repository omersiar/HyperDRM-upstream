#ifndef HYPERDRM_RECONSTRUCT_H
#define HYPERDRM_RECONSTRUCT_H

#include <stdlib.h>
#include <string.h>

#include "layout.h"
#include "reconstruct_table.h"

/* Reconstructed renderer.
 *
 * The region renderer paints each point with the colour of the window that
 * contains it, so a boundary between two windows becomes a visible seam even
 * when the picture has no edge there. The sixteen measurements are area means
 * of known rectangles, so they also constrain a smooth field: solving
 *
 *     min |A x - m|^2 + lambda |L x|^2
 *
 * for a 32x18 grid of cell colours and interpolating that grid gives one
 * surface that still averages to the sixteen measurements. A is the rectangle
 * coverage, L the grid Laplacian, and the solution is linear in m, so it is a
 * fixed 576x16 matrix baked into reconstruct_table.h at build time.
 *
 * Measured against known frames with tools/histogram_reconstruct.py, this is
 * better than picking the containing window on smooth content (2.0% versus
 * 20.5% step error on a gradient) and equal on hard edges, where any isotropic
 * field has to round a corner. It is also softer than the panel really is at
 * region boundaries that do fall on real edges, which is the trade the user
 * accepts by choosing this geometry.
 *
 * Regions 14 and 15 are left out of the fit: both are panel-anchored probes
 * whose rectangles are barely a tenth of the panel tall or wide, their means
 * sit apart from the areas around them, and because the fit is global each one
 * pulled its own neighbourhood towards itself. Their columns in the table are
 * zero, so they reach the grid only through the field the other windows imply.
 * The bar is unaffected: r14 still paints the bar rows directly. */

#define RECONSTRUCT_CELLS (RECONSTRUCT_GX * RECONSTRUCT_GY)

/* One axis of the bilinear lookup: the two grid cells a point falls between,
 * and the weight of the second in 1/65536. */
typedef struct {
    unsigned short first, second, weight;
} ReconstructTap;

static ReconstructTap *reconstruct_x;
static ReconstructTap *reconstruct_y;
static unsigned char *reconstruct_bar;
static unsigned reconstruct_width, reconstruct_height, reconstruct_bar_rows;
static unsigned reconstruct_generation;
static unsigned reconstruct_builds;
/* Counted so a test can prove the lookup is built per geometry change, not per
 * frame; the same mistake cost the edge feed a 12 Hz ceiling once. */
static inline unsigned reconstruct_build_count(void) {
    return reconstruct_builds;
}

/* Baked weights map regions to cells; see reconstruct_table.h. Rounding is
 * done by hand so negative sums do not depend on how the compiler shifts. */
static inline int reconstruct_round(int sum) {
    const int half = 1 << (RECONSTRUCT_SHIFT - 1);
    return sum >= 0 ? (sum + half) >> RECONSTRUCT_SHIFT : -((-sum + half) >> RECONSTRUCT_SHIFT);
}

static void reconstruct_grid(const unsigned char regions[16][3],
                             unsigned char grid[RECONSTRUCT_CELLS][3]) {
    for (unsigned cell = 0; cell < RECONSTRUCT_CELLS; cell++) {
        const short *weights = reconstruct_m[cell];
        int sum[3] = {0, 0, 0};
        for (unsigned region = 0; region < 16; region++) {
            int weight = weights[region];
            if (!weight)
                continue;
            for (unsigned channel = 0; channel < 3; channel++)
                sum[channel] += weight * regions[region][channel];
        }
        for (unsigned channel = 0; channel < 3; channel++) {
            int value = reconstruct_round(sum[channel]);
            grid[cell][channel] = (unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value);
        }
    }
}

static bool reconstruct_taps(unsigned width, unsigned height) {
    unsigned bar_rows = layout_picture_top > 0.0 ? (unsigned)(layout_picture_top * height + 0.5) : 0;
    if (reconstruct_x && reconstruct_width == width && reconstruct_height == height &&
        reconstruct_bar_rows == bar_rows && reconstruct_generation == calibration_generation)
        return true;
    ReconstructTap *fresh_x = malloc((size_t)width * sizeof(*fresh_x));
    ReconstructTap *fresh_y = malloc((size_t)height * sizeof(*fresh_y));
    unsigned char *fresh_bar = malloc((size_t)height);
    if (!fresh_x || !fresh_y || !fresh_bar) {
        free(fresh_x);
        free(fresh_y);
        free(fresh_bar);
        return false;
    }
    for (unsigned x = 0; x < width; x++) {
        /* Cell i covers [i/GX, (i+1)/GX], the same tiling the weights were
         * solved against, so its centre sits at (i + 0.5) / GX. Sampling cell
         * centres instead would stretch the field by 31/32 and shift it by
         * half a cell, which a two-cell-tall window cannot absorb. */
        double point = ((x + 0.5) / width) * RECONSTRUCT_GX - 0.5;
        point = point < 0.0 ? 0.0 : point > RECONSTRUCT_GX - 1 ? RECONSTRUCT_GX - 1 : point;
        unsigned first = (unsigned)point;
        if (first >= RECONSTRUCT_GX - 1)
            first = RECONSTRUCT_GX - 2;
        fresh_x[x] = (ReconstructTap){(unsigned short)first, (unsigned short)(first + 1),
                                      (unsigned short)((point - first) * 65535.0 + 0.5)};
    }
    for (unsigned y = 0; y < height; y++) {
        bool bar = bar_rows && (y < bar_rows || y >= height - bar_rows);
        fresh_bar[y] = (unsigned char)bar;
        double point = (y + 0.5) / height;
        if (layout_picture_top > 0.0) {
            point = (point - layout_picture_top) / layout_picture_height;
            point = point < 0.0 ? 0.0 : point > 1.0 ? 1.0 : point;
        }
        point *= RECONSTRUCT_GY;
        point -= 0.5;
        point = point < 0.0 ? 0.0 : point > RECONSTRUCT_GY - 1 ? RECONSTRUCT_GY - 1 : point;
        unsigned first = (unsigned)point;
        if (first >= RECONSTRUCT_GY - 1)
            first = RECONSTRUCT_GY - 2;
        fresh_y[y] = (ReconstructTap){(unsigned short)first, (unsigned short)(first + 1),
                                      (unsigned short)((point - first) * 65535.0 + 0.5)};
    }
    free(reconstruct_x);
    free(reconstruct_y);
    free(reconstruct_bar);
    reconstruct_x = fresh_x;
    reconstruct_y = fresh_y;
    reconstruct_bar = fresh_bar;
    reconstruct_width = width;
    reconstruct_height = height;
    reconstruct_bar_rows = bar_rows;
    reconstruct_generation = calibration_generation;
    reconstruct_builds++;
    return true;
}

/* Interpolated plane, with the measured bar left to the bar probe exactly as
 * the region renderer leaves it: the receiver expects to find its own bars. */
static bool reconstruct_plane(const unsigned char regions[16][3], unsigned char *out,
                              unsigned width, unsigned height) {
    if (width < 2 || height < 2 || !reconstruct_taps(width, height))
        return false;
    unsigned char grid[RECONSTRUCT_CELLS][3];
    reconstruct_grid(regions, grid);
    for (unsigned y = 0; y < height; y++) {
        unsigned char *row = out + (size_t)y * width * 3;
        if (reconstruct_bar[y]) {
            for (unsigned x = 0; x < width; x++, row += 3)
                memcpy(row, regions[LAYOUT_BAR_REGION], 3);
            continue;
        }
        const ReconstructTap *tap_y = &reconstruct_y[y];
        const unsigned char *upper = grid[tap_y->first * RECONSTRUCT_GX];
        const unsigned char *lower = grid[tap_y->second * RECONSTRUCT_GX];
        int weight_y = tap_y->weight;
        for (unsigned x = 0; x < width; x++, row += 3) {
            const ReconstructTap *tap_x = &reconstruct_x[x];
            const unsigned char *a = upper + tap_x->first * 3;
            const unsigned char *b = upper + tap_x->second * 3;
            const unsigned char *c = lower + tap_x->first * 3;
            const unsigned char *d = lower + tap_x->second * 3;
            int weight_x = tap_x->weight;
            for (unsigned channel = 0; channel < 3; channel++) {
                int top = (a[channel] * (65535 - weight_x) + b[channel] * weight_x + 32768) >> 16;
                int bottom =
                    (c[channel] * (65535 - weight_x) + d[channel] * weight_x + 32768) >> 16;
                row[channel] =
                    (unsigned char)((top * (65535 - weight_y) + bottom * weight_y + 32768) >> 16);
            }
        }
    }
    return true;
}

#endif
