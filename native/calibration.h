#ifndef HYPERDRM_CALIBRATION_H
#define HYPERDRM_CALIBRATION_H
#include <math.h>
#include <stdlib.h>
#include "playback.h"
#include "atomic_file.h"
#define CALIBRATION_PREF                                                                           \
    "/media/developer/apps/usr/palm/services/org.webosbrew.histogramhyperion.service/"             \
    "calibration.json"
#define CALIBRATION_JSON_MAX 8192
typedef struct {
    double position;
    unsigned char region;
} RimPoint;
typedef enum { CAL_AUTO, CAL_G4, CAL_AVERAGE, CAL_CUSTOM } CalibrationKind;
/* Optional measured sampling rectangle of one histogram region. LG reports the
 * mean of the pixels inside its own rectangle; the renderer inverts those
 * overlapping box averages on a small grid instead of treating them as tiles.
 * Rectangles may overlap. */
typedef struct {
    double x0, y0, x1, y1;
    unsigned char region;
} RegionRect;
/* How the 256 bins of a region/channel become one value. The mean is the
 * default; a percentile keeps the bright end of a region that mixes light and
 * dark, and active averages only the pixels above the black reference so a small
 * bright object is not diluted by a large dark background. */
#define CAL_STAT_MEAN 0
#define CAL_STAT_P50 1
#define CAL_STAT_P90 2
#define CAL_STAT_PEAK 3
#define CAL_STAT_ACTIVE 4
static inline const char *calibration_stat_key(unsigned stat) {
    return stat == CAL_STAT_P50      ? "p50"
           : stat == CAL_STAT_P90    ? "p90"
           : stat == CAL_STAT_PEAK   ? "peak"
           : stat == CAL_STAT_ACTIVE ? "active"
                                     : "mean";
}
typedef struct {
    CalibrationKind kind;
    char model[64];
    unsigned count;
    RimPoint points[16];
    unsigned rect_count;
    RegionRect rects[16];
    bool rect_geometry;
    /* Full-black and full-white reference means, measured per region/channel.
     * Optional: only used when the user enables colour-cast compensation. */
    bool has_reference;
    bool compensate;
    /* How the rendered feed summarises each region; see CAL_STAT_*. */
    unsigned color_stat;
    unsigned char black[16][3];
    unsigned char white[16][3];
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
 * and only ever applies to a measured custom profile with real rectangles.
 * Without measured rectangles the rim model is used: inventing blocks from the
 * rim anchors made a Voronoi mosaic that did not describe the TV picture. */
static inline bool calibration_rectangles(void) {
    return calibration.rect_geometry && calibration_effective() == CAL_CUSTOM &&
           calibration.rect_count > 0;
}
/* Only measured rectangles are painted. The rectangle geometry has no derived
 * fallback: a rim anchor does not describe how far a region reaches inwards. */
static const RegionRect *calibration_blocks(unsigned *count) {
    *count = calibration.rect_count;
    return calibration.rects;
}
static inline bool calibration_reference_parse(PlaybackJson *j, void *root, const char *key,
                                               unsigned char out[16][3]) {
    void *array = NULL;
    if (!j->get(root, key, &array) || j->type(array) != 5 || j->length(array) != 16)
        return false;
    for (size_t i = 0; i < 16; i++) {
        void *row = j->at(array, i);
        if (!row || j->type(row) != 5 || j->length(row) != 3)
            return false;
        for (size_t c = 0; c < 3; c++) {
            void *value = j->at(row, c);
            if (!value || (j->type(value) != 2 && j->type(value) != 3))
                return false;
            char *end;
            double number = strtod(j->string(value), &end);
            if (!end || *end || !isfinite(number) || number < 0 || number > 255)
                return false;
            out[i][c] = (unsigned char)(number + 0.5);
        }
    }
    return true;
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
    bool has_points = false;
    if (j->type(root) == 4 && j->get(root, "points", &points)) {
        has_points = true;
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
    void *black = NULL, *white = NULL, *compensate = NULL;
    bool black_present = j->type(root) == 4 && j->get(root, "black", &black);
    bool white_present = j->type(root) == 4 && j->get(root, "white", &white);
    unsigned char black_ref[16][3], white_ref[16][3];
    bool black_valid = !black_present || calibration_reference_parse(j, root, "black", black_ref);
    bool white_valid = !white_present || calibration_reference_parse(j, root, "white", white_ref);
    valid = valid && black_valid && white_valid;
    if (has_points) {
        /* A new point list belongs to a new measurement. Keep references only
         * when the same JSON carries both arrays. */
        next.has_reference = black_present && white_present && black_valid && white_valid;
        if (next.has_reference) {
            memcpy(next.black, black_ref, sizeof(next.black));
            memcpy(next.white, white_ref, sizeof(next.white));
        } else {
            memset(next.black, 0, sizeof(next.black));
            memset(next.white, 0, sizeof(next.white));
        }
    } else if (black_present && white_present && black_valid && white_valid) {
        next.has_reference = true;
        memcpy(next.black, black_ref, sizeof(next.black));
        memcpy(next.white, white_ref, sizeof(next.white));
    }
    if (j->type(root) == 4 && j->get(root, "compensate", &compensate)) {
        valid = valid && j->type(compensate) == 1;
        if (valid)
            next.compensate = playback_true(j, root, "compensate");
    }
    bool stat_set = false;
    if (playback_is(j, root, "colorStat", "p50")) {
        next.color_stat = CAL_STAT_P50;
        stat_set = true;
    } else if (playback_is(j, root, "colorStat", "p90")) {
        next.color_stat = CAL_STAT_P90;
        stat_set = true;
    } else if (playback_is(j, root, "colorStat", "peak")) {
        next.color_stat = CAL_STAT_PEAK;
        stat_set = true;
    } else if (playback_is(j, root, "colorStat", "active")) {
        next.color_stat = CAL_STAT_ACTIVE;
        stat_set = true;
    } else if (playback_is(j, root, "colorStat", "mean")) {
        next.color_stat = CAL_STAT_MEAN;
        stat_set = true;
    }
    /* A profile saved before the statistic existed carried ignore-black as its
     * own flag; it selects the same behaviour. */
    if (!stat_set && playback_true(j, root, "activeMean"))
        next.color_stat = CAL_STAT_ACTIVE;
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
    if (value->has_reference || value->compensate) {
        used += snprintf(data + used, sizeof(data) - used, ",\"compensate\":%s",
                         value->compensate ? "true" : "false");
        if (value->has_reference) {
            used += snprintf(data + used, sizeof(data) - used, ",\"black\":[");
            for (unsigned i = 0; i < 16; i++)
                used += snprintf(data + used, sizeof(data) - used, "%s[%u,%u,%u]", i ? "," : "",
                                 value->black[i][0], value->black[i][1], value->black[i][2]);
            used += snprintf(data + used, sizeof(data) - used, "],\"white\":[");
            for (unsigned i = 0; i < 16; i++)
                used += snprintf(data + used, sizeof(data) - used, "%s[%u,%u,%u]", i ? "," : "",
                                 value->white[i][0], value->white[i][1], value->white[i][2]);
            used += snprintf(data + used, sizeof(data) - used, "]");
        }
    }
    if (value->color_stat)
        used += snprintf(data + used, sizeof(data) - used, ",\"colorStat\":\"%s\"",
                         calibration_stat_key(value->color_stat));
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
