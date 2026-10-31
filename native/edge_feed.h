#ifndef HYPERDRM_EDGE_FEED_H
#define HYPERDRM_EDGE_FEED_H
/* Edge feed: send the measurements, not a picture.
 *
 * An LED strip only needs colour where its LEDs are, and the sixteen windows
 * already describe the edge better than any raster we could solve for. So this
 * feed does one thing per output pixel: ask the geometry which measurement owns
 * that point and copy it. There is no averaging between windows, no inverse
 * solve and no resampling of an already resampled image.
 *
 * That is the whole design, and it has a testable consequence: with a measured
 * rectangle profile every byte that reaches the receiver is one of the sixteen
 * measured colours, unchanged. A blended or reconstructed pixel would be a
 * colour the TV never measured, which is exactly what a first implementation
 * should refuse to send.
 *
 * 160x90 is 12x12 screen pixels per cell at 1920x1080 and four times finer than
 * the narrowest window's short side, so quantising a window boundary to a cell
 * moves an LED by a fraction of its own width. Going finer buys nothing: the
 * receiver samples this plane with its own LED rectangles anyway.
 *
 * Rim profiles have no measured rectangles, so there the feed keeps the
 * documented blend along the perimeter. The exact-copy rule applies wherever we
 * actually know where a window is.
 *
 * A custom profile may instead ask for the reconstructed renderer (geometry
 * "smooth", see reconstruct.h). Then the same measured windows are used to
 * solve for a smooth field and that field is interpolated here. It is opt-in
 * and off by default, because it deliberately sends interpolated colours. */
#include "layout.h"
#include "reconstruct.h"
#define EDGE_WIDTH 160
#define EDGE_HEIGHT 90
#define EDGE_PIXELS (EDGE_WIDTH * EDGE_HEIGHT)
/* Which measurement owns each cell, resolved once per geometry instead of once
 * per pixel per frame. The rectangle search is a dozen double comparisons per
 * candidate, and this build is soft-float: measuring the plane showed the lookup
 * costing 60-110 ms of every 80-120 ms tick, which capped the whole feed at
 * about 12 Hz no matter what capture interval was configured. The answer only
 * changes when the profile changes or the bar moves a whole cell, so it is
 * cached on exactly those two things. */
typedef struct {
    unsigned short a, b, weight;
} EdgeCell;
/* Counted so a test can prove the table is built per geometry change and not
 * per frame; rebuilding it every frame is exactly the regression that made the
 * feed run at 12 Hz. */
static unsigned edge_cell_builds;
static inline unsigned edge_cell_build_count(void) {
    return edge_cell_builds;
}
static const EdgeCell *edge_cells(unsigned width, unsigned height) {
    static EdgeCell *cells;
    static unsigned cached_width, cached_height, cached_generation;
    static unsigned cached_bar_rows = (unsigned)-1;
    static bool cached;
    /* Quantising the bar to whole cells keeps a one-level jitter in r14 from
     * rebuilding the table on every frame; the error is one cell row, which is
     * finer than the bar measurement itself. */
    unsigned bar_rows = layout_picture_top > 0.0
                            ? (unsigned)(layout_picture_top * height + 0.5)
                            : 0;
    if (cached && cached_width == width && cached_height == height &&
        cached_generation == calibration_generation && cached_bar_rows == bar_rows)
        return cells;
    size_t count = (size_t)width * height;
    EdgeCell *fresh = malloc(count * sizeof(*fresh));
    if (!fresh)
        return NULL;
    for (unsigned y = 0; y < height; y++) {
        double point_y = (y + 0.5) / height;
        for (unsigned x = 0; x < width; x++) {
            LedBlend owner = layout_point_blend((x + 0.5) / width, point_y);
            fresh[(size_t)y * width + x] =
                (EdgeCell){owner.a, owner.b, owner.weight};
        }
    }
    free(cells);
    cells = fresh;
    cached_width = width;
    cached_height = height;
    cached_generation = calibration_generation;
    cached_bar_rows = bar_rows;
    cached = true;
    edge_cell_builds++;
    return cells;
}
static void edge_plane_slow(const unsigned char regions[16][3], unsigned char *out,
                            unsigned width, unsigned height) {
    for (unsigned y = 0; y < height; y++) {
        double point_y = (y + 0.5) / height;
        for (unsigned x = 0; x < width; x++) {
            LedBlend owner = layout_point_blend((x + 0.5) / width, point_y);
            unsigned char *pixel = out + ((size_t)y * width + x) * 3;
            if (owner.a == owner.b) {
                memcpy(pixel, regions[owner.a & 15], 3);
            } else {
                /* Rim profile: two anchors, no measured extent between them. */
                blend_color(regions[owner.a & 15], regions[owner.b & 15], owner.weight, pixel);
            }
        }
    }
}
static void edge_plane(const unsigned char regions[16][3], unsigned char *out, unsigned width,
                       unsigned height) {
    if (calibration_smooth() && reconstruct_plane(regions, out, width, height))
        return;
    const EdgeCell *cells = edge_cells(width, height);
    if (!cells) {
        edge_plane_slow(regions, out, width, height);
        return;
    }
    const EdgeCell *cell = cells;
    unsigned char *pixel = out;
    for (size_t i = 0, count = (size_t)width * height; i < count; i++, cell++, pixel += 3) {
        if (cell->a == cell->b) {
            memcpy(pixel, regions[cell->a & 15], 3);
        } else {
            blend_color(regions[cell->a & 15], regions[cell->b & 15], cell->weight, pixel);
        }
    }
}
/* How many cells each measurement owns. A window that owns none is redundant
 * with a smaller one; reporting it keeps that visible instead of silent. */
static inline void edge_ownership(unsigned width, unsigned height, unsigned counts[16]) {
    for (unsigned i = 0; i < 16; i++)
        counts[i] = 0;
    const EdgeCell *cells = edge_cells(width, height);
    for (size_t i = 0, count = (size_t)width * height; i < count; i++)
        counts[cells[i].a & 15]++;
}
/* Cached for the status reply: the mapping changes with the profile and with
 * the measured bar, so both are part of the key. */
static inline const unsigned *edge_ownership_cached(void) {
    static unsigned counts[16];
    static unsigned generation;
    static unsigned cached_bar_rows = (unsigned)-1;
    static bool cached;
    unsigned bar_rows =
        layout_picture_top > 0.0 ? (unsigned)(layout_picture_top * EDGE_HEIGHT + 0.5) : 0;
    if (!cached || generation != calibration_generation || cached_bar_rows != bar_rows) {
        edge_ownership(EDGE_WIDTH, EDGE_HEIGHT, counts);
        generation = calibration_generation;
        cached_bar_rows = bar_rows;
        cached = true;
    }
    return counts;
}
#endif
