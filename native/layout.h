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
            mapping[i] = layout_blend((coordinates[0] + coordinates[1]) / 2,
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
