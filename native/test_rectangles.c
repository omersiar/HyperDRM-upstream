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

    /* 1/area: where both rectangles cover, the small local one carries more. */
    double wide = 1.0 / (0.1 * 1.0), narrow = 1.0 / (0.15 * 0.2);
    unsigned expected = (unsigned)(wide / (wide + narrow) * 65535 + 0.5);
    same_blend(layout_point_blend(0.92, 0.5), 2, 10, expected);
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

    /* The plane itself must follow the rectangle model. */
    unsigned char regions[16][3], plane[SPATIAL_PIXELS * 3], expected_rgb[3], rim_rgb[3];
    memset(regions, 0, sizeof(regions));
    memset(regions[2], 40, 3);
    memset(regions[10], 200, 3);
    spatial_plane(regions, plane);
    blend_color(regions[2], regions[10], expected, expected_rgb);
    const unsigned char *pixel = plane + (45 * SPATIAL_WIDTH + 147) * 3;
    assert(!memcmp(pixel, expected_rgb, 3));
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

    /* Geometry without imported rectangles derives one edge block per anchor,
     * so a plain calibrated profile can be compared against the rim model. */
    const char *blocks =
        "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"geometry\":\"rectangles\","
        "\"points\":[{\"position\":0.5,\"region\":3},{\"position\":1.5,\"region\":10},"
        "{\"position\":2.5,\"region\":1},{\"position\":3.5,\"region\":15}]}";
    Calibration derived = calibration;
    assert(calibration_parse(&json, blocks, &derived));
    assert(derived.rect_count == 0 && derived.rect_geometry);
    calibration = derived;
    calibration_generation++;
    assert(calibration_rectangles());
    unsigned blocks_count = 0;
    calibration_blocks(&blocks_count);
    assert(blocks_count == 4);
    same_blend(layout_point_blend(0.05, 0.5), 15, 15, 0);
    same_blend(layout_point_blend(0.95, 0.5), 10, 10, 0);
    same_blend(layout_point_blend(0.5, 0.05), 3, 3, 0);
    same_blend(layout_point_blend(0.5, 0.95), 1, 1, 0);
    /* Uncovered centre: nearest block centre, not a cone. */
    same_blend(layout_point_blend(0.5, 0.4), 3, 3, 0);
    /* Overlapping corner blocks blend by 1/area (equal areas -> half). */
    LedBlend corner = layout_point_blend(0.1, 0.1);
    assert(corner.a == 3 && corner.b == 15 && corner.weight >= 32767 && corner.weight <= 32768);
    memset(regions[15], 90, 3);
    spatial_plane(regions, plane);
    pixel = plane + (45 * SPATIAL_WIDTH + 3) * 3;
    assert(pixel[0] == 90);

    /* An emptied list keeps the mode (derived blocks); geometry rim disables it. */
    Calibration emptied = calibration;
    assert(calibration_parse(&json,
                             "{\"profile\":\"custom\",\"model\":\"TV-ONE\",\"points\":[{\"position\":0.5,"
                             "\"region\":3},{\"position\":1.5,\"region\":10},{\"position\":2.5,"
                             "\"region\":1},{\"position\":3.5,\"region\":15}],\"rectangles\":[]}",
                             &emptied));
    assert(!emptied.rect_count && emptied.rect_geometry);
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

    /* Persistence keeps rectangles, the geometry selection and derived mode. */
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
    assert(calibration_rectangles());
    unlink(path);

    puts("PASS: rectangle geometry, 1/area weights, derived blocks, nearest-centre fallback, "
         "geometry kept across recalibration, plane output, validation and save/load");
    return 0;
}
