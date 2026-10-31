#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "spatial_image.h"

/* The measured rectangles of the 65QNED87A6B, copied from
 * profiles/65QNED87A6B.BEUFLJP.json so the test uses the geometry the operator
 * actually runs, not a toy one. */
static const RegionRect g4_rects[16] = {
    {0.24, 0.0, 0.75, 0.98, 0},  {0.19, 0.79, 0.8, 0.99, 1},   {0.29, 0.29, 0.7, 0.7, 2},
    {0.09, 0.01, 0.4, 0.18, 3},  {0.0, 0.0, 0.5, 0.98, 4},     {0.0, 0.65, 0.28, 0.99, 5},
    {0.0, 0.22, 0.19, 0.75, 6},  {0.0, 0.0, 0.29, 0.36, 7},    {0.49, 0.01, 0.99, 0.98, 8},
    {0.69, 0.65, 0.99, 0.99, 9}, {0.79, 0.22, 0.99, 0.75, 10}, {0.34, 0.01, 0.65, 0.18, 11},
    {0.59, 0.0, 0.89, 0.19, 12}, {0.69, 0.01, 0.98, 0.36, 13}, {0.29, 0.0, 0.7, 0.12, 14},
    {0.01, 0.29, 0.12, 0.7, 15}};

static void measured_profile(bool smooth) {
    memset(&calibration, 0, sizeof(calibration));
    calibration.kind = CAL_CUSTOM;
    calibration.rect_geometry = true;
    calibration.rect_count = 16;
    memcpy(calibration.rects, g4_rects, sizeof(g4_rects));
    calibration.smooth_render = smooth;
    layout_set_letterbox(false);
    calibration_generation++;
}

/* Colour of the plane pixel that covers screen point (x, y). */
static void sample(const unsigned char *plane, unsigned x, unsigned y, unsigned char out[3]) {
    memcpy(out, plane + ((size_t)y * SPATIAL_WIDTH + x) * 3, 3);
}

static unsigned distinct_on_row(const unsigned char *plane, unsigned y) {
    unsigned char seen[SPATIAL_WIDTH][3];
    unsigned count = 0;
    for (unsigned x = 0; x < SPATIAL_WIDTH; x++) {
        unsigned char pixel[3];
        sample(plane, x, y, pixel);
        bool known = false;
        for (unsigned i = 0; i < count && !known; i++)
            known = seen[i][0] == pixel[0] && seen[i][1] == pixel[1] && seen[i][2] == pixel[2];
        if (!known) {
            memcpy(seen[count], pixel, 3);
            count++;
        }
    }
    return count;
}

int main(void) {
    unsigned char regions[16][3], plane[SPATIAL_PIXELS * 3], grid[RECONSTRUCT_CELLS][3];
    (void)layout_from_json;

    /* A constant picture is the one case with an exact answer: every window
     * measures the same colour, so every cell must come back unchanged. This is
     * what proves the baked weights are normalised (each row of M sums to one). */
    measured_profile(true);
    assert(calibration_smooth());
    for (unsigned i = 0; i < 16; i++)
        regions[i][0] = regions[i][1] = regions[i][2] = 77;
    reconstruct_grid(regions, grid);
    for (unsigned cell = 0; cell < RECONSTRUCT_CELLS; cell++)
        for (unsigned channel = 0; channel < 3; channel++)
            assert(abs((int)grid[cell][channel] - 77) <= 1);
    assert(reconstruct_plane(regions, plane, SPATIAL_WIDTH, SPATIAL_HEIGHT));
    for (size_t i = 0; i < SPATIAL_PIXELS * 3; i++)
        assert(abs((int)plane[i] - 77) <= 1);
    puts("PASS: a constant picture reconstructs to itself");

    /* The two panel-anchored probes are excluded from the fit, so only their
     * own columns are zero: whatever they measure cannot reach the grid, which
     * is what stopped them dragging their neighbourhoods around on the wall. */
    regions[14][0] = 250;
    regions[15][2] = 250;
    unsigned char shifted[RECONSTRUCT_CELLS][3];
    reconstruct_grid(regions, shifted);
    assert(!memcmp(grid, shifted, sizeof(grid)));
    puts("PASS: the excluded probes do not reach the grid");

    /* Measured against a known picture: a left-to-right gradient. The fit is
     * regularised, so it smooths rather than interpolating the sixteen means
     * exactly, and what matters is that the plane's own window means still
     * describe the picture the windows saw. */
    for (unsigned i = 0; i < 16; i++) {
        const RegionRect *r = &g4_rects[i];
        double sum[3] = {0, 0, 0};
        unsigned count = 0;
        for (unsigned y = 0; y < 64; y++)
            for (unsigned x = 0; x < 64; x++) {
                double px = r->x0 + (r->x1 - r->x0) * (x + 0.5) / 64.0;
                double py = r->y0 + (r->y1 - r->y0) * (y + 0.5) / 64.0;
                sum[0] += 20 + 200 * px;
                sum[1] += 40 + 120 * py;
                sum[2] += 200 - 160 * px;
                count++;
            }
        for (unsigned c = 0; c < 3; c++)
            regions[i][c] = (unsigned char)(sum[c] / count + 0.5);
    }
    assert(reconstruct_plane(regions, plane, SPATIAL_WIDTH, SPATIAL_HEIGHT));
    int worst = 0;
    for (unsigned i = 0; i < 16; i++) {
        const RegionRect *r = &g4_rects[i];
        long sum[3] = {0, 0, 0};
        unsigned count = 0;
        for (unsigned y = 0; y < SPATIAL_HEIGHT; y++)
            for (unsigned x = 0; x < SPATIAL_WIDTH; x++) {
                double px = (x + 0.5) / SPATIAL_WIDTH, py = (y + 0.5) / SPATIAL_HEIGHT;
                if (px < r->x0 || px > r->x1 || py < r->y0 || py > r->y1)
                    continue;
                unsigned char pixel[3];
                sample(plane, x, y, pixel);
                for (unsigned c = 0; c < 3; c++)
                    sum[c] += pixel[c];
                count++;
            }
        assert(count > 0);
        for (unsigned c = 0; c < 3; c++) {
            int error = abs((int)(sum[c] / (long)count) - regions[i][c]);
            if (error > worst)
                worst = error;
        }
    }
    printf("PASS: gradient window means survive reconstruction (worst channel %d)\n", worst);
    assert(worst <= 8);

    /* The point of the mode: a hard split down the middle becomes a ramp
     * instead of a seam. The region colours are what the TV would report for
     * that picture, i.e. the step averaged over each window. */
    for (unsigned i = 0; i < 16; i++) {
        const RegionRect *r = &g4_rects[i];
        double red = 0.0, blue = 0.0;
        unsigned count = 0;
        for (unsigned y = 0; y < 64; y++)
            for (unsigned x = 0; x < 64; x++) {
                double px = r->x0 + (r->x1 - r->x0) * (x + 0.5) / 64.0;
                red += px < 0.5 ? 240.0 : 0.0;
                blue += px < 0.5 ? 0.0 : 240.0;
                count++;
            }
        regions[i][0] = (unsigned char)(red / count + 0.5);
        regions[i][1] = 0;
        regions[i][2] = (unsigned char)(blue / count + 0.5);
    }
    assert(reconstruct_plane(regions, plane, SPATIAL_WIDTH, SPATIAL_HEIGHT));
    unsigned ramp = distinct_on_row(plane, SPATIAL_HEIGHT / 2);
    unsigned char left[3], middle_pixel[3], right[3];
    sample(plane, 0, SPATIAL_HEIGHT / 2, left);
    sample(plane, SPATIAL_WIDTH / 2, SPATIAL_HEIGHT / 2, middle_pixel);
    sample(plane, SPATIAL_WIDTH - 1, SPATIAL_HEIGHT / 2, right);
    calibration.smooth_render = false;
    edge_plane(regions, plane, SPATIAL_WIDTH, SPATIAL_HEIGHT);
    unsigned flat = distinct_on_row(plane, SPATIAL_HEIGHT / 2);
    printf("PASS: a split scene ramps over %u values, the region renderer over %u\n", ramp, flat);
    assert(ramp > 20 && flat <= 4);

    /* The two halves stay ordered and meet in the middle rather than jumping. */
    assert(left[0] > middle_pixel[0] && middle_pixel[0] > right[0]);
    assert(left[2] < middle_pixel[2] && middle_pixel[2] < right[2]);
    puts("PASS: a split scene stays ordered across the picture");

    /* The bar is the bar probe, exactly as the region renderer leaves it, so the
     * receiver still finds its own letterbox. */
    layout_set_letterbox(true);
    unsigned char bar[3];
    memcpy(bar, regions[LAYOUT_BAR_REGION], 3);
    assert(reconstruct_plane(regions, plane, SPATIAL_WIDTH, SPATIAL_HEIGHT));
    unsigned bar_rows = (unsigned)(layout_picture_top * SPATIAL_HEIGHT + 0.5);
    assert(bar_rows > 0);
    for (unsigned y = 0; y < bar_rows; y++)
        for (unsigned x = 0; x < SPATIAL_WIDTH; x++) {
            unsigned char pixel[3];
            sample(plane, x, y, pixel);
            assert(!memcmp(pixel, bar, 3));
        }
    unsigned char middle[3];
    sample(plane, SPATIAL_WIDTH / 2, SPATIAL_HEIGHT / 2, middle);
    assert(memcmp(middle, bar, 3));
    puts("PASS: the measured bar is painted from the bar probe");
    layout_set_letterbox(false);

    /* The lookup tables are per geometry, not per frame: rebuilding them every
     * frame is the regression that once capped the feed at 12 Hz. */
    assert(reconstruct_plane(regions, plane, SPATIAL_WIDTH, SPATIAL_HEIGHT)); /* settle the bar */
    unsigned before = reconstruct_build_count();
    assert(reconstruct_plane(regions, plane, SPATIAL_WIDTH, SPATIAL_HEIGHT));
    assert(reconstruct_build_count() == before);
    calibration_generation++;
    assert(reconstruct_plane(regions, plane, SPATIAL_WIDTH, SPATIAL_HEIGHT));
    assert(reconstruct_build_count() == before + 1);
    puts("PASS: lookup tables are built once per geometry");

    /* Without measured rectangles the option cannot apply, and the region
     * renderer still takes over rather than solving against nothing. */
    calibration.smooth_render = true;
    calibration.rect_count = 0;
    assert(!calibration_smooth());
    calibration_generation++;
    edge_plane(regions, plane, SPATIAL_WIDTH, SPATIAL_HEIGHT);
    puts("PASS: no rectangles, no reconstruction");
}
