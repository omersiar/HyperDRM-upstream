#ifndef HYPERDRM_CALIBRATION_H
#define HYPERDRM_CALIBRATION_H
#include <math.h>
#include <stdlib.h>
#include "playback.h"
#include "atomic_file.h"
#define CALIBRATION_PREF                                                                           \
    "/media/developer/apps/usr/palm/services/org.webosbrew.histogramhyperion.service/"             \
    "calibration.json"
#define CALIBRATION_JSON_MAX 4096
typedef struct {
    double position;
    unsigned char region;
} RimPoint;
typedef enum { CAL_AUTO, CAL_G4, CAL_AVERAGE, CAL_CUSTOM } CalibrationKind;
/* Optional measured sampling rectangle of one histogram region. LG reports the
 * mean of the pixels inside its own rectangle, so painting measured rectangles
 * back with 1/area weights is the inverse of that measurement. Rectangles may
 * overlap; the geometry selector chooses between this and perimeter rim
 * interpolation without discarding the other representation. */
typedef struct {
    double x0, y0, x1, y1;
    unsigned char region;
} RegionRect;
/* Depth of a derived edge block: how far inwards from the rim an anchor paints
 * when the rectangle geometry is selected without imported rectangles. */
#define REGION_BLOCK_DEPTH 0.19
typedef struct {
    CalibrationKind kind;
    char model[64];
    unsigned count;
    RimPoint points[16];
    unsigned rect_count;
    RegionRect rects[16];
    bool rect_geometry;
} Calibration;
static Calibration calibration;
static char tv_model[64];
static unsigned calibration_generation = 1;
static inline unsigned calibration_revision(void) {
    return calibration_generation;
}
static const RimPoint g4_rim[] = {{0.2078431373, 7},  {0.3176470588, 3},  {0.5490196078, 11},
                                  {0.7529411765, 12}, {0.8392156863, 13}, {1.4941176471, 10},
                                  {2.1843137255, 9},  {2.5490196078, 1},  {2.8666666667, 5},
                                  {3.4, 15}};
static inline const char *calibration_key(CalibrationKind kind) {
    const char *keys[] = {"auto", "g4", "average", "custom"};
    return keys[kind];
}
static inline bool calibration_model_valid(const char *model) {
    if (!model[0] || strlen(model) > 63)
        return false;
    for (const unsigned char *p = (const unsigned char *)model; *p; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              *p == '.' || *p == '-' || *p == '_' || *p == ' '))
            return false;
    return true;
}
/* LG retail model identifiers: OLED + two-digit panel size + G4 series. */
static inline bool calibration_is_g4(const char *model) {
    return strlen(model) >= 8 && !strncmp(model, "OLED", 4) && model[4] >= '0' && model[4] <= '9' &&
           model[5] >= '0' && model[5] <= '9' && model[6] == 'G' && model[7] == '4';
}
static inline CalibrationKind calibration_effective(void) {
    if (calibration.kind != CAL_AUTO)
        return calibration.kind;
    if (calibration.count >= 2 && tv_model[0] && !strcmp(calibration.model, tv_model))
        return CAL_CUSTOM;
    return calibration_is_g4(tv_model) ? CAL_G4 : CAL_AVERAGE;
}
/* Rectangle geometry is an opt-in alternative to perimeter rim interpolation
 * and only ever applies to a measured custom profile. Imported rectangles win;
 * without them the rim anchors are rendered as derived edge blocks. */
static inline bool calibration_rectangles(void) {
    return calibration.rect_geometry && calibration_effective() == CAL_CUSTOM;
}
static double wrap_perimeter(double value) {
    /* Bounded loop instead of fmod(): the service links without libm. */
    while (value >= 4)
        value -= 4;
    while (value < 0)
        value += 4;
    return value;
}
static void perimeter_point(double t, double *x, double *y) {
    t = wrap_perimeter(t);
    if (t < 1) {
        *x = t;
        *y = 0;
    } else if (t < 2) {
        *x = 1;
        *y = t - 1;
    } else if (t < 3) {
        *x = 3 - t;
        *y = 1;
    } else {
        *x = 0;
        *y = 4 - t;
    }
}
static RegionRect region_blocks[16];
static unsigned region_block_count, region_block_generation;
/* One block per rim anchor: the arc up to the neighbouring anchors becomes a
 * rectangle along that edge, deepened towards the picture. This is a starting
 * geometry that lets a rim profile be compared against rectangle rendering
 * without hand-entered data; imported rectangles always take precedence. */
static const RegionRect *calibration_blocks(unsigned *count) {
    if (calibration.rect_count) {
        *count = calibration.rect_count;
        return calibration.rects;
    }
    if (region_block_generation != calibration_generation) {
        region_block_count = 0;
        unsigned n = calibration.count;
        for (unsigned i = 0; i < n; i++) {
            double p = calibration.points[i].position;
            double previous = calibration.points[(i + n - 1) % n].position;
            double following = calibration.points[(i + 1) % n].position;
            double start = wrap_perimeter(p - wrap_perimeter(p - previous) / 2);
            double end = wrap_perimeter(p + wrap_perimeter(following - p) / 2);
            double ax, ay, bx, by;
            perimeter_point(start, &ax, &ay);
            perimeter_point(end, &bx, &by);
            double x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax;
            double y0 = ay < by ? ay : by, y1 = ay < by ? by : ay;
            if (x1 - x0 < 1e-9) {
                if (x0 > 0.5) {
                    x0 = 1 - REGION_BLOCK_DEPTH;
                    x1 = 1;
                } else {
                    x0 = 0;
                    x1 = REGION_BLOCK_DEPTH;
                }
            }
            if (y1 - y0 < 1e-9) {
                if (y0 > 0.5) {
                    y0 = 1 - REGION_BLOCK_DEPTH;
                    y1 = 1;
                } else {
                    y0 = 0;
                    y1 = REGION_BLOCK_DEPTH;
                }
            }
            unsigned char region = calibration.points[i].region;
            bool duplicate = false;
            for (unsigned k = 0; k < region_block_count; k++)
                duplicate = duplicate || region_blocks[k].region == region;
            if (!duplicate && x0 < x1 && y0 < y1)
                region_blocks[region_block_count++] = (RegionRect){x0, y0, x1, y1, region};
        }
        region_block_generation = calibration_generation;
    }
    *count = region_block_count;
    return region_blocks;
}
static inline bool calibration_parse(PlaybackJson *j, const char *text, Calibration *result) {
    void *root = playback_parse_exact(j, text);
    if (!root)
        return false;
    Calibration next = *result;
    bool valid = false;
    for (unsigned k = 0; k < 4; k++)
        if (playback_is(j, root, "profile", calibration_key(k))) {
            next.kind = k;
            valid = true;
            break;
        }
    void *points = NULL;
    if (j->type(root) == 4 && j->get(root, "points", &points)) {
        void *model = playback_field(j, root, "model");
        valid = valid && points && j->type(points) == 5 && model && j->type(model) == 6 &&
                calibration_model_valid(j->string(model));
        size_t count = valid ? j->length(points) : 0;
        valid = valid && count >= 2 && count <= 16;
        if (valid) {
            strcpy(next.model, j->string(model));
            next.count = count;
            memset(next.points, 0, sizeof(next.points));
        }
        for (size_t i = 0; valid && i < count; i++) {
            void *point = j->at(points, i), *position = playback_field(j, point, "position"),
                 *region = playback_field(j, point, "region");
            valid = position && (j->type(position) == 2 || j->type(position) == 3) && region &&
                    j->type(region) == 3;
            if (valid) {
                char *end;
                double x = strtod(j->string(position), &end);
                valid = end && !*end && isfinite(x) && x >= 0 && x < 4;
                long r = strtol(j->string(region), &end, 10);
                valid = valid && end && !*end && r >= 0 && r < 16 &&
                        (!i || x > next.points[i - 1].position);
                if (valid) {
                    next.points[i].position = x;
                    next.points[i].region = r;
                }
            }
        }
        /* Rectangles belong to the same measurement; a new point list must not
         * keep rectangles measured for a previous one. The geometry choice is
         * a user setting and survives recalibration. */
        next.rect_count = 0;
        memset(next.rects, 0, sizeof(next.rects));
    }
    void *rectangles = NULL, *geometry = NULL;
    if (j->type(root) == 4 && j->get(root, "rectangles", &rectangles)) {
        size_t count = j->type(rectangles) == 5 ? j->length(rectangles) : 0;
        valid = valid && j->type(rectangles) == 5 && count <= 16;
        next.rect_count = 0;
        memset(next.rects, 0, sizeof(next.rects));
        for (size_t i = 0; valid && i < count; i++) {
            void *rect = j->at(rectangles, i), *region = playback_field(j, rect, "region");
            const char *keys[] = {"x0", "y0", "x1", "y1"};
            double edge[4] = {0};
            valid = rect && j->type(rect) == 4 && region && j->type(region) == 3;
            for (unsigned k = 0; valid && k < 4; k++) {
                void *v = playback_field(j, rect, keys[k]);
                valid = v && (j->type(v) == 2 || j->type(v) == 3);
                if (valid) {
                    char *end;
                    edge[k] = strtod(j->string(v), &end);
                    valid = end && !*end && isfinite(edge[k]) && edge[k] >= 0 && edge[k] <= 1;
                }
            }
            long r = -1;
            if (valid) {
                char *end;
                r = strtol(j->string(region), &end, 10);
                valid = end && !*end && r >= 0 && r < 16;
            }
            valid = valid && edge[0] < edge[2] && edge[1] < edge[3];
            for (size_t k = 0; valid && k < i; k++)
                valid = next.rects[k].region != (unsigned char)r;
            if (valid) {
                next.rects[i] = (RegionRect){edge[0], edge[1], edge[2], edge[3], (unsigned char)r};
                next.rect_count = i + 1;
            }
        }
        if (valid && count > 0)
            next.rect_geometry = true;
    }
    if (j->type(root) == 4 && j->get(root, "geometry", &geometry)) {
        valid = valid && j->type(geometry) == 6;
        if (valid && !strcmp(j->string(geometry), "rectangles"))
            next.rect_geometry = true;
        else if (valid && !strcmp(j->string(geometry), "rim"))
            next.rect_geometry = false;
        else
            valid = false;
    }
    if (next.kind == CAL_CUSTOM && next.count < 2)
        valid = false;
    j->put(root);
    if (valid)
        *result = next;
    return valid;
}
static inline bool calibration_save(const char *path, const Calibration *value) {
    if (!value || value->kind < CAL_AUTO || value->kind > CAL_CUSTOM || value->count > 16 ||
        (value->count && (value->count < 2 || !calibration_model_valid(value->model))))
        return false;
    for (unsigned i = 0; i < value->count; i++) {
        double x = value->points[i].position;
        if (!isfinite(x) || x < 0 || x >= 4 || value->points[i].region >= 16 ||
            (i && x <= value->points[i - 1].position))
            return false;
    }
    if (value->rect_count > 16)
        return false;
    for (unsigned i = 0; i < value->rect_count; i++) {
        const RegionRect *r = &value->rects[i];
        if (!isfinite(r->x0) || !isfinite(r->y0) || !isfinite(r->x1) || !isfinite(r->y1) ||
            r->x0 < 0 || r->y0 < 0 || r->x1 > 1 || r->y1 > 1 || r->x0 >= r->x1 || r->y0 >= r->y1 ||
            r->region >= 16)
            return false;
        for (unsigned k = 0; k < i; k++)
            if (value->rects[k].region == r->region)
                return false;
    }

    char data[CALIBRATION_JSON_MAX];
    int used = snprintf(data, sizeof(data), "{\"profile\":\"%s\"", calibration_key(value->kind));
    if (value->count) {
        used += snprintf(data + used, sizeof(data) - used, ",\"model\":\"%s\",\"points\":[",
                         value->model);
        for (unsigned i = 0; i < value->count; i++)
            used +=
                snprintf(data + used, sizeof(data) - used, "%s{\"position\":%.12g,\"region\":%u}",
                         i ? "," : "", value->points[i].position, value->points[i].region);
        used += snprintf(data + used, sizeof(data) - used, "]");
    }
    if (value->rect_count || value->rect_geometry) {
        used += snprintf(data + used, sizeof(data) - used, ",\"geometry\":\"%s\"",
                         value->rect_geometry ? "rectangles" : "rim");
    }
    if (value->rect_count) {
        used += snprintf(data + used, sizeof(data) - used, ",\"rectangles\":[");
        for (unsigned i = 0; i < value->rect_count; i++)
            used += snprintf(data + used, sizeof(data) - used,
                             "%s{\"region\":%u,\"x0\":%.6g,\"y0\":%.6g,\"x1\":%.6g,\"y1\":%.6g}",
                             i ? "," : "", value->rects[i].region, value->rects[i].x0,
                             value->rects[i].y0, value->rects[i].x1, value->rects[i].y1);
        used += snprintf(data + used, sizeof(data) - used, "]");
    }
    used += snprintf(data + used, sizeof(data) - used, "}\n");
    return used > 0 && (size_t)used < sizeof(data) &&
           atomic_replace(path, data, (size_t)used) != SAVE_UNCHANGED;
}
static inline void calibration_load(PlaybackJson *j, const char *path) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return;
    char data[CALIBRATION_JSON_MAX];
    ssize_t n = read(fd, data, sizeof(data) - 1);
    close(fd);
    if (n <= 0 || n == (ssize_t)sizeof(data) - 1)
        return;
    data[n] = 0;
    calibration_parse(j, data, &calibration);
}
static inline void calibration_colors(const unsigned char input[16][3],
                                      unsigned char output[16][3]) {
    memcpy(output, input, 48);
    if (calibration_effective() != CAL_AVERAGE)
        return;
    unsigned sum[3] = {0};
    for (unsigned i = 0; i < 16; i++)
        for (unsigned c = 0; c < 3; c++)
            sum[c] += input[i][c];
    for (unsigned i = 0; i < 16; i++)
        for (unsigned c = 0; c < 3; c++)
            output[i][c] = sum[c] / 16;
}
#endif
