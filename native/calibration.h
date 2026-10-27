#ifndef HYPERDRM_CALIBRATION_H
#define HYPERDRM_CALIBRATION_H
#include <math.h>
#include "playback.h"
#include "atomic_file.h"
#define CALIBRATION_PREF                                                                           \
    "/media/developer/apps/usr/palm/services/org.webosbrew.histogramhyperion.service/"             \
    "calibration.json"
typedef struct {
    double position;
    unsigned char region;
} RimPoint;
typedef enum { CAL_AUTO, CAL_G4, CAL_AVERAGE, CAL_CUSTOM } CalibrationKind;
typedef struct {
    CalibrationKind kind;
    char model[64];
    unsigned count;
    RimPoint points[16];
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

    char data[2048];
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
    used += snprintf(data + used, sizeof(data) - used, "}\n");
    return used > 0 && (size_t)used < sizeof(data) &&
           atomic_replace(path, data, (size_t)used) != SAVE_UNCHANGED;
}
static inline void calibration_load(PlaybackJson *j, const char *path) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return;
    char data[2048];
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
