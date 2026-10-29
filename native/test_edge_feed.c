#define _DEFAULT_SOURCE
#include <assert.h>
#include <string.h>
#include "spatial_image.h"

static void set_rect(unsigned index, unsigned region, double x0, double y0, double x1, double y1) {
    calibration.rects[index] = (RegionRect){x0, y0, x1, y1, (unsigned char)region};
}

static void measured_profile(void) {
    memset(&calibration, 0, sizeof(calibration));
    calibration.kind = CAL_CUSTOM;
    calibration.rect_geometry = true;
    calibration.rect_count = 3;
    /* A panel-anchored top probe, a picture-relative top band and a background.
     * The two thin windows (r14, r15) are the ones the preset scan showed the
     * firmware does not scale with the picture. */
    set_rect(0, 14, 0.0, 0.0, 0.3, 0.2);
    set_rect(1, 3, 0.5, 0.0, 0.8, 0.2);
    set_rect(2, 0, 0.0, 0.0, 1.0, 1.0);
    layout_set_letterbox(false);
}

int main(void) {
    measured_profile();
    (void)layout_from_json;
    assert(calibration_rectangles());

    /* The smallest containing window wins, so the most local measurement is
     * used and nothing is averaged. */
    assert(layout_point_blend(0.15, 0.10).a == 14);
    assert(layout_point_blend(0.65, 0.10).a == 3);
    assert(layout_point_blend(0.90, 0.90).a == 0);
    /* Selection never produces a blend. */
    LedBlend picked = layout_point_blend(0.15, 0.10);
    assert(picked.a == picked.b && picked.weight == 0);

    /* No invented colour: every plane byte belongs to one of the sixteen
     * measured regions, unchanged. */
    unsigned char regions[16][3], plane[SPATIAL_PIXELS * 3];
    memset(regions, 0, sizeof(regions));
    for (unsigned i = 0; i < 16; i++) {
        regions[i][0] = (unsigned char)(10 + i * 7);
        regions[i][1] = (unsigned char)(200 - i * 5);
        regions[i][2] = (unsigned char)(40 + i * 3);
    }
    spatial_plane(regions, plane);
    for (unsigned i = 0; i < SPATIAL_PIXELS; i++) {
        unsigned match = 0;
        for (unsigned r = 0; r < 16; r++)
            if (!memcmp(plane + i * 3, regions[r], 3))
                match++;
        assert(match == 1);
    }

    /* Every measurement that owns screen area is actually delivered, and the
     * ownership counts cover the plane exactly once. */
    unsigned counts[16];
    edge_ownership(SPATIAL_WIDTH, SPATIAL_HEIGHT, counts);
    unsigned total = 0;
    for (unsigned i = 0; i < 16; i++)
        total += counts[i];
    assert(total == SPATIAL_PIXELS);
    assert(counts[14] > 0 && counts[3] > 0 && counts[0] > 0);
    assert(counts[1] == 0 && counts[15] == 0);

    /* Letterbox with a 0.25 bar: inside the picture the picture-relative band is
     * projected in... */
    layout_set_bar(0.25);
    assert(layout_point_blend(0.65, 0.30).a == 3);
    /* ...the panel-anchored probe is not projected, so at panel y 0.30 it is
     * outside its 0..0.2 band and the background wins... */
    assert(layout_point_blend(0.15, 0.30).a == 0);
    /* ...and the bar itself carries the probe's own measured colour, top and
     * bottom, so the receiver can find the bars in the frame. */
    assert(layout_point_blend(0.15, 0.10).a == 14);
    assert(layout_point_blend(0.65, 0.10).a == 14);
    assert(layout_point_blend(0.90, 0.90).a == 14);
    /* With no bar nothing is intercepted. */
    layout_set_bar(0.0);
    assert(layout_point_blend(0.65, 0.22).a == 0);
    assert(layout_point_blend(0.15, 0.22).a == 0);

    /* The letterbox scale is the one the preset scan measured. */
    layout_set_letterbox(true);
    assert(layout_picture_height == LAYOUT_LETTERBOX_SCALE);
    assert(layout_picture_top > 0.12 && layout_picture_top < 0.13);
    layout_set_letterbox(false);
    assert(layout_picture_height == 1.0 && layout_picture_top == 0.0);

    /* The bar height comes from r14, the panel-anchored probe: a full-white
     * probe means no bar, a black one means the bar fills the probe. */
    assert(layout_bar_height(255) == 0.0);
    assert(layout_bar_height(0) == 0.2);
    assert(layout_bar_height(128) > 0.09 && layout_bar_height(128) < 0.11);
    layout_set_bar(0.05);
    assert(layout_picture_top == 0.05);
    assert(layout_picture_height > 0.89 && layout_picture_height < 0.91);
    layout_set_bar(-1);
    assert(layout_picture_top == 0.0 && layout_picture_height == 1.0);

    /* The geometry lookup is resolved once per profile and bar, never per frame:
     * rebuilding it every frame is what held the feed at 12 Hz. */
    spatial_plane(regions, plane);
    unsigned builds = edge_cell_build_count();
    for (unsigned i = 0; i < 20; i++)
        spatial_plane(regions, plane);
    assert(edge_cell_build_count() == builds);
    /* A bar that moves a whole cell does invalidate it, and the plane follows. */
    layout_set_bar(0.25);
    spatial_plane(regions, plane);
    assert(edge_cell_build_count() > builds);
    unsigned bar_cells = 0;
    for (unsigned i = 0; i < SPATIAL_PIXELS; i++)
        bar_cells += !memcmp(plane + i * 3, regions[14], 3);
    /* 22 rows above and 22 below the band at a 0.25 bar. */
    assert(bar_cells == 44 * SPATIAL_WIDTH);
    layout_set_bar(0.0);
    layout_set_bar(9);
    assert(layout_picture_height > 0.0);
    /* A profile without r14 says so instead of claiming a zero bar. */
    RegionRect saved = calibration.rects[0];
    calibration.rects[0] = (RegionRect){0.0, 0.0, 0.3, 0.2, 3};
    assert(layout_bar_height(0) < 0.0);
    calibration.rects[0] = saved;

    /* Rim profiles keep the documented blend: two anchors, no measured extent. */
    calibration.rect_geometry = false;
    calibration.count = 2;
    calibration.points[0] = (RimPoint){0.5, 2};
    calibration.points[1] = (RimPoint){2.5, 10};
    assert(!calibration_rectangles());
    LedBlend rim = layout_point_blend(0.5, 0.5);
    assert(rim.a != rim.b);
    /* An empty custom point list must not divide by zero. */
    calibration.count = 0;
    LedBlend empty = layout_point_blend(0.5, 0.5);
    assert(empty.a == empty.b && empty.weight == 0);
    puts("PASS: exact-copy feed, smallest-window selection, letterbox projection, panel anchoring");
    return 0;
}
