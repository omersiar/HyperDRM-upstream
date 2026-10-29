#ifndef HYPERION_LAYOUT_H
#define HYPERION_LAYOUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <math.h>
typedef struct {
    unsigned char a, b;
    unsigned weight;
} LedBlend;
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include "calibration.h"
static double screen_perimeter(double x, double y) {
    double distances[] = {y, 1 - x, 1 - y, x}, positions[] = {x, 1 + y, 3 - x, 4 - y};
    unsigned edge = 0;
    for (unsigned i = 1; i < 4; i++)
        if (distances[i] < distances[edge])
            edge = i;
    return positions[edge] >= 4 ? positions[edge] - 4 : positions[edge];
}
static LedBlend layout_blend(double x, double y) {
    const RimPoint *rim = calibration_effective() == CAL_CUSTOM ? calibration.points : g4_rim;
    double t = screen_perimeter(x, y);
    unsigned n = calibration_effective() == CAL_CUSTOM ? calibration.count
                                                       : sizeof(g4_rim) / sizeof(*g4_rim),
             right = 0;
    /* A custom profile without points cannot interpolate; fall back to the
     * first anchor instead of dividing by zero. */
    if (!n)
        return (LedBlend){0, 0, 0};
    while (right < n && rim[right].position <= t)
        right++;
    right %= n;
    unsigned left = (right + n - 1) % n;
    double span = rim[right].position - rim[left].position, distance = t - rim[left].position;
    if (span <= 0)
        span += 4;
    if (distance < 0)
        distance += 4;
    return (LedBlend){rim[left].region, rim[right].region,
                      (unsigned)(distance / span * 65535 + 0.5)};
}
/* Rectangle geometry: LG reports each region as the mean of the pixels inside
 * its own sampling rectangle. Those rectangles overlap and nest, so they are
 * not a partition and no single pixel colour can be recovered exactly. Use the
 * smallest measured rectangle that contains the point: it is the least
 * averaged measurement available there. Blending two overlapping rectangles
 * invented colours that were never in the picture (pale seams around a white
 * rectangle on a dark background). Outside every rectangle use the nearest
 * measured centre, again without blending. */
/* Rectangle geometry: LG reports each region as the mean of the pixels inside
 * its own sampling rectangle. Those rectangles overlap and nest, so they are
 * not a partition and no single pixel colour can be recovered exactly. This
 * renderer therefore *selects* the least averaged measurement available at a
 * point and copies it; it never mixes two windows, because a mix is a colour
 * that was never measured.
 *
 * Two properties of the firmware matter here, both measured with the preset
 * scan rather than assumed:
 *
 * - All windows except 14 and 15 are positioned inside the video window, so a
 *   letterboxed picture scales and shifts them. r14 and r15 keep identical bin
 *   totals in presets 0, 1 and 2, which makes them panel anchored: they stay on
 *   the bezel when the picture shrinks, and they must not be projected.
 * - Preset 1 is the letterboxed window set and scales the picture by 0.7471
 *   (16:9 to 2.39:1 is 0.7438). */
#define LAYOUT_LETTERBOX_SCALE 0.7471
static double layout_picture_top, layout_picture_height = 1.0;
static inline void layout_set_letterbox(bool enabled) {
    layout_picture_height = enabled ? LAYOUT_LETTERBOX_SCALE : 1.0;
    layout_picture_top = (1.0 - layout_picture_height) / 2.0;
}
/* The picture window from a measured bar height.
 *
 * r14 is the firmware's own black-bar probe: it is panel anchored, it sits in
 * the top strip, and its lit fraction times its own height is the bar. The
 * aspect sweep measured that to within 0.007 of the true bar from 1.85:1 to
 * 2.35:1, and it saturates at its own 0.125 height above about 2.4:1. Using it
 * replaces the assumed 0.7471 crop with a measurement, and it works before the
 * firmware has switched to its letterbox preset. */
static inline double layout_bar_height(unsigned char probe_level) {
    unsigned count = 0;
    const RegionRect *rects = calibration_blocks(&count);
    for (unsigned i = 0; i < count; i++)
        if (rects[i].region == 14)
            return rects[i].y1 * (1.0 - probe_level / 255.0);
    return -1.0; /* no probe in this profile */
}
static inline void layout_set_bar(double bar) {
    if (!(bar > 0))
        bar = 0;
    if (bar > 0.45)
        bar = 0.45;
    layout_picture_top = bar;
    layout_picture_height = 1.0 - 2.0 * bar;
}
/* Panels 14 and 15 are the only windows the firmware does not scale. */
static bool layout_panel_anchored(unsigned region) {
    return region == 14 || region == 15;
}
/* Screen y to the coordinate the window is expressed in. Outside the picture
 * the caller has already returned the bar, so this only ever sees picture rows. */
static double layout_window_y(const RegionRect *r, double y) {
    if (layout_panel_anchored(r->region))
        return y;
    return (y - layout_picture_top) / layout_picture_height;
}
/* The bar is part of the picture the receiver expects.
 *
 * The receiver runs its own black-bar detection and extension on whatever image
 * it is given. A plane built entirely from picture-relative windows fills the
 * frame, so that detector can never see a bar, and the bar probe r14 ends up
 * dark while every LED is lit. Filling the measured bar with the bar probe's
 * own colour makes the synthetic frame look like a real capture: the receiver
 * finds the bars itself and extends across them however it is configured.
 *
 * The bottom bar mirrors the top one because the service only classifies a
 * symmetric letterbox, and r14 is the only panel-anchored top probe. */
#define LAYOUT_BAR_REGION 14
static LedBlend layout_rectangle_blend(double x, double y) {
    if (layout_picture_top > 0.0 &&
        (y < layout_picture_top || y > 1.0 - layout_picture_top))
        return (LedBlend){LAYOUT_BAR_REGION, LAYOUT_BAR_REGION, 0};
    unsigned count = 0;
    const RegionRect *rects = calibration_blocks(&count);
    int best = -1;
    unsigned nearest = 0;
    double best_area = 0, best_distance = 0, nearest_distance = 0;
    for (unsigned i = 0; i < count; i++) {
        const RegionRect *r = &rects[i];
        double window_y = layout_window_y(r, y);
        double dx = x - (r->x0 + r->x1) / 2, dy = window_y - (r->y0 + r->y1) / 2;
        double distance = dx * dx + dy * dy;
        if (!i || distance < nearest_distance) {
            nearest_distance = distance;
            nearest = r->region;
        }
        if (x < r->x0 || x > r->x1 || window_y < r->y0 || window_y > r->y1)
            continue;
        double area = (r->x1 - r->x0) * (r->y1 - r->y0);
        /* Equal-area regions are common on this panel (halves, bands). Prefer
         * the one whose centre is closer so the partition stays local. */
        if (best < 0 || area < best_area - 1e-12 ||
            (area < best_area + 1e-12 && distance < best_distance)) {
            best = (int)i;
            best_area = area;
            best_distance = distance;
        }
    }
    unsigned region = best >= 0 ? rects[best].region : nearest;
    return (LedBlend){region, region, 0};
}
/* Active spatial model: measured rectangles when a custom profile opts in,
 * otherwise the perimeter rim interpolation. Both produce one LedBlend per
 * point, so the plane and the receiver LED mapping share the same geometry. */
static inline LedBlend layout_point_blend(double x, double y) {
    return calibration_rectangles() ? layout_rectangle_blend(x, y) : layout_blend(x, y);
}
/* Use the TV's JSON-C library; validate every rectangle before replacing the
 * layout. The safety limit is independent of the installed LED count. */
static bool layout_from_json(const char *text, LedBlend **result, unsigned *count) {
    void *lib = dlopen("libjson-c.so.5", RTLD_NOW);
    if (!lib)
        return false;
    void *(*parse)(const char *) = dlsym(lib, "json_tokener_parse");
    int (*get)(const void *, const char *, void **) = dlsym(lib, "json_object_object_get_ex");
    int (*type)(const void *) = dlsym(lib, "json_object_get_type");
    size_t (*length)(const void *) = dlsym(lib, "json_object_array_length");
    void *(*at)(const void *, size_t) = dlsym(lib, "json_object_array_get_idx");
    double (*number)(const void *) = dlsym(lib, "json_object_get_double");
    int (*put)(void *) = dlsym(lib, "json_object_put");
    if (!parse || !get || !type || !length || !at || !number || !put) {
        dlclose(lib);
        return false;
    }
    void *root = parse(text), *info = NULL, *leds = NULL;
    bool valid = root && type(root) == 4 && get(root, "info", &info) && type(info) == 4 &&
                 get(info, "leds", &leds) && type(leds) == 5;
    size_t n = valid ? length(leds) : 0;
    valid = valid && n > 0 && n <= 16384;
    LedBlend *mapping = valid ? calloc(n, sizeof(*mapping)) : NULL;
    valid = valid && mapping;
    const char *keys[] = {"hmin", "hmax", "vmin", "vmax"};
    for (size_t i = 0; valid && i < n; i++) {
        void *led = at(leds, i);
        double coordinates[4];
        valid = led && type(led) == 4;
        for (unsigned c = 0; valid && c < 4; c++) {
            void *v = NULL;
            valid = get(led, keys[c], &v) && (type(v) == 2 || type(v) == 3);
            if (valid) {
                coordinates[c] = number(v);
                valid = isfinite(coordinates[c]) && coordinates[c] >= 0 && coordinates[c] <= 1;
            }
        }
        if (valid)
            valid = coordinates[0] <= coordinates[1] && coordinates[2] <= coordinates[3];
        if (valid)
            mapping[i] = layout_point_blend((coordinates[0] + coordinates[1]) / 2,
                                            (coordinates[2] + coordinates[3]) / 2);
    }
    if (root)
        put(root);
    dlclose(lib);
    if (!valid) {
        free(mapping);
        return false;
    }
    *result = mapping;
    *count = (unsigned)n;
    return true;
}
#endif
