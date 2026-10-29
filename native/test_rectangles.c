#define _DEFAULT_SOURCE
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include "spatial_image.h"

static void same_blend(LedBlend got, unsigned a, unsigned b, unsigned weight) {
    assert(got.a == a && got.b == b && got.weight == weight);
}

int main(void) {
    (void)layout_from_json;
    PlaybackJson json = {0};
    assert(playback_json_init(&json));
    const char *profile =
        "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"geometry\":\"rectangles\","
        "\"points\":[{\"position\":0,\"region\":2},{\"position\":2,\"region\":10}],"
        "\"rectangles\":[{\"region\":10,\"x0\":0.9,\"y0\":0,\"x1\":1,\"y1\":1},"
        "{\"region\":2,\"x0\":0.8,\"y0\":0.4,\"x1\":0.95,\"y1\":0.6}]}";
    Calibration next = calibration;
    assert(calibration_parse(&json, profile, &next));
    assert(next.rect_count == 2 && next.rect_geometry && next.count == 2);
    calibration = next;
    strcpy(tv_model, "TV-ONE");
    calibration_generation++;
    assert(calibration_rectangles());

    /* Smallest containing rectangle: the local r2 measurement wins where both
     * rectangles overlap, with no invented blend colour. */
    same_blend(layout_point_blend(0.92, 0.5), 2, 2, 0);
    /* Only the wide rectangle covers here. */
    same_blend(layout_point_blend(0.95, 0.1), 10, 10, 0);
    /* Outside every rectangle: nearest measured centre. */
    same_blend(layout_point_blend(0.2, 0.5), 2, 2, 0);

    /* The rim model stays available for A/B comparison on the same profile. */
    calibration.rect_geometry = false;
    assert(calibration_rectangles() == false);
    LedBlend rim = layout_point_blend(0.92, 0.5);
    same_blend(rim, 2, 10, 49151);
    calibration.rect_geometry = true;

    /* The plane itself must follow the rectangle model: one measured region
     * colour per pixel, no blend. */
    unsigned char regions[16][3], plane[SPATIAL_PIXELS * 3], rim_rgb[3];
    memset(regions, 0, sizeof(regions));
    memset(regions[2], 40, 3);
    memset(regions[10], 200, 3);
    spatial_plane(regions, plane);
    const unsigned char *pixel = plane + (45 * SPATIAL_WIDTH + 147) * 3;
    assert(pixel[0] == 40 && pixel[1] == 40 && pixel[2] == 40);
    blend_color(regions[2], regions[10], rim.weight, rim_rgb);
    assert(memcmp(pixel, rim_rgb, 3));

    /* A new point list must not inherit rectangles measured for the old one. */
    Calibration replaced = calibration;
    assert(calibration_parse(&json,
                             "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"points\":[{\"position\":0,"
                             "\"region\":2},{\"position\":2,\"region\":10}]}",
                             &replaced));
    assert(!replaced.rect_count && replaced.rect_geometry && replaced.count == 2);
    /* Switching only the profile preserves the stored measurement. */
    Calibration kept = calibration;
    assert(calibration_parse(&json, "{\"profile\":\"auto\"}", &kept));
    assert(kept.rect_count == 2 && kept.rect_geometry);

    /* Geometry without imported rectangles must fall back to the rim model.
     * Inventing blocks from rim anchors made a Voronoi mosaic that did not
     * describe the TV picture. */
    const char *blocks =
        "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"geometry\":\"rectangles\","
        "\"points\":[{\"position\":0.5,\"region\":3},{\"position\":1.5,\"region\":10},"
        "{\"position\":2.5,\"region\":1},{\"position\":3.5,\"region\":15}]}";
    Calibration derived = calibration;
    assert(calibration_parse(&json, blocks, &derived));
    assert(derived.rect_count == 0 && derived.rect_geometry);
    calibration = derived;
    calibration_generation++;
    assert(calibration_rectangles() == false);
    unsigned blocks_count = 99;
    calibration_blocks(&blocks_count);
    assert(blocks_count == 0);
    LedBlend fallback = layout_point_blend(0.05, 0.5), rim_fallback = layout_blend(0.05, 0.5);
    assert(fallback.a == rim_fallback.a && fallback.b == rim_fallback.b &&
           fallback.weight == rim_fallback.weight);

    /* An emptied list keeps the mode but cannot paint rectangles. */
    Calibration emptied = calibration;
    assert(calibration_parse(&json,
                             "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"points\":[{\"position\":0.5,"
                             "\"region\":3},{\"position\":1.5,\"region\":10},{\"position\":2.5,"
                             "\"region\":1},{\"position\":3.5,\"region\":15}],\"rectangles\":[]}",
                             &emptied));
    assert(!emptied.rect_count && emptied.rect_geometry);
    calibration = emptied;
    calibration_generation++;
    assert(calibration_rectangles() == false);
    Calibration rim_mode = calibration;
    assert(calibration_parse(&json,
                             "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"geometry\":\"rim\","
                             "\"points\":[{\"position\":0.5,\"region\":3},{\"position\":1.5,"
                             "\"region\":10},{\"position\":2.5,\"region\":1},{\"position\":3.5,"
                             "\"region\":15}]}",
                             &rim_mode));
    assert(!rim_mode.rect_geometry);
    /* Recalibrating replaces the points but keeps the user's geometry choice. */
    Calibration recalibrated = calibration;
    assert(calibration_parse(&json,
                             "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"points\":[{\"position\":0.4,"
                             "\"region\":3},{\"position\":1.6,\"region\":10},{\"position\":2.4,"
                             "\"region\":1},{\"position\":3.6,\"region\":15}]}",
                             &recalibrated));
    assert(recalibrated.rect_geometry && !recalibrated.rect_count);

    /* Validation of an imported rectangle list. */
    const char *bad[] = {
        "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"points\":[{\"position\":0,\"region\":2},"
        "{\"position\":2,\"region\":10}],\"rectangles\":[{\"region\":1,\"x0\":0.5,\"y0\":0,\"x1\":0.5,\"y1\":1}]}",
        "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"points\":[{\"position\":0,\"region\":2},"
        "{\"position\":2,\"region\":10}],\"rectangles\":[{\"region\":16,\"x0\":0,\"y0\":0,\"x1\":0.5,\"y1\":1}]}",
        "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"points\":[{\"position\":0,\"region\":2},"
        "{\"position\":2,\"region\":10}],\"rectangles\":[{\"region\":1,\"x0\":0,\"y0\":0,\"x1\":1.5,\"y1\":1}]}",
        "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"points\":[{\"position\":0,\"region\":2},"
        "{\"position\":2,\"region\":10}],\"rectangles\":[{\"region\":1,\"x0\":0,\"y0\":0,\"x1\":0.5,\"y1\":1},"
        "{\"region\":1,\"x0\":0.5,\"y0\":0,\"x1\":1,\"y1\":1}]}",
        "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"geometry\":\"cones\",\"points\":[{\"position\":0,"
        "\"region\":2},{\"position\":2,\"region\":10}]}"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        Calibration before = next;
        assert(!calibration_parse(&json, bad[i], &before));
        assert(!memcmp(&before, &next, sizeof(next)));
    }

    /* Persistence keeps rectangles and the geometry selection. */
    const char *path = "/tmp/hyperdrm-rectangles-test.json";
    calibration = next;
    assert(access(path, F_OK) != 0);
    assert(calibration_save(path, &next));
    calibration = (Calibration){0};
    calibration_load(&json, path);
    assert(calibration.rect_count == 2 && calibration.rect_geometry);
    assert(calibration.rects[0].region == 10 && calibration.rects[1].x1 > 0.94);
    assert(calibration.count == 2 && !strcmp(calibration.model, "TV-ONE"));
    calibration = derived;
    assert(calibration_save(path, &derived));
    calibration = (Calibration){0};
    calibration_load(&json, path);
    assert(calibration.rect_count == 0 && calibration.rect_geometry && calibration.count == 4);
    assert(!calibration_rectangles());
    unlink(path);

    puts("PASS: rectangle geometry, smallest containing rectangle, measured-only rectangles "
         "with rim fallback, nearest-centre fallback, geometry kept across recalibration, plane "
         "output, validation and save/load");
    return 0;
}
