#ifndef HYPERDRM_COLOR_GRADE_H
#define HYPERDRM_COLOR_GRADE_H
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include "color_space.h"
/* Brightness and colour, treated separately, after the measurement is done.
 *
 * The reconstruction can only work on area means, so anything that needs a
 * per-colour decision has to happen afterwards. OKLab is the space where that
 * decision means what a viewer means by it: L is lightness on its own, and the
 * a/b pair is chroma and hue, so a vibrance curve can lift muted colours
 * without touching the brightness of anything.
 *
 * Two knobs, both in the light domain:
 *   vibrance   extra chroma, most for the muted and least for the already
 *              saturated, so a grey stays grey and a strong red stays a strong
 *              red instead of clipping to orange
 *   brightness ceiling and floor. The ceiling keeps the strip below the panel,
 *              which is what bias lighting should be; the floor keeps a black
 *              scene from turning the wall into a void. Everything below the
 *              knee is left exactly as measured and only the highlights roll
 *              off towards the ceiling, because a single gain would dim the
 *              whole picture to fix its top end.
 *
 * Chroma that the ceiling cannot hold is reduced along constant hue until it
 * fits, which is a gamut map rather than a clip: clipping moves a colour's hue,
 * and a wrong hue is far more visible than a slightly duller one. The gamut is
 * sRGB because that is what the output path encodes; giving it the strip's own
 * primaries later is a change to one function here, not to the pipeline. */
/* sRGB holds about 0.32 of OKLab chroma at its most saturated. */
#define COLOR_GRADE_CHROMA_REF 0.32
#define COLOR_GRADE_OFF 0
#define COLOR_GRADE_GENTLE 1
#define COLOR_GRADE_STRONG 2
typedef struct {
    bool enabled;
    double vibrance;   /* 0 = untouched, 1 = muted colours doubled */
    double knee;       /* light below this is passed through unchanged */
    double brightness; /* output ceiling in light, 1 = full */
    double floor;      /* output floor in light; use color_light() for a code level */
} ColorGrade;
static inline const char *color_grade_key(unsigned preset) {
    return preset == COLOR_GRADE_GENTLE ? "gentle" : preset == COLOR_GRADE_STRONG ? "strong" : "off";
}
static inline bool color_grade_parse(const char *key, unsigned *preset) {
    if (!key)
        return false;
    if (!strcmp(key, "off"))
        *preset = COLOR_GRADE_OFF;
    else if (!strcmp(key, "gentle"))
        *preset = COLOR_GRADE_GENTLE;
    else if (!strcmp(key, "strong"))
        *preset = COLOR_GRADE_STRONG;
    else
        return false;
    return true;
}
static inline ColorGrade color_grade_preset(unsigned preset) {
    if (preset == COLOR_GRADE_GENTLE)
        return (ColorGrade){true, 0.30, 0.70, 0.96, color_light(5)};
    if (preset == COLOR_GRADE_STRONG)
        return (ColorGrade){true, 0.65, 0.50, 0.90, color_light(12)};
    return (ColorGrade){false, 0.0, 1.0, 1.0, 0.0};
}
/* Highlights roll off towards the ceiling instead of being scaled: below the
 * knee nothing changes, above it the curve approaches `ceiling` and never
 * reaches it. */
static inline double color_grade_ceiling(double light, double knee, double ceiling) {
    if (light <= knee || ceiling <= knee)
        return light <= knee ? light : knee;
    double span = ceiling - knee;
    return knee + span * (1.0 - exp(-(light - knee) / span));
}
/* OKLab from linear RGB (Bjorn Ottosson). */
static inline void color_grade_to_oklab(const double rgb[3], double lab[3]) {
    double l = 0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2];
    double m = 0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2];
    double s = 0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2];
    l = cbrt(l);
    m = cbrt(m);
    s = cbrt(s);
    lab[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    lab[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    lab[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}
/* Linear RGB from OKLab. Returns false when the colour falls outside sRGB,
 * which is what the gamut search asks it. */
static inline bool color_grade_from_oklab(const double lab[3], double rgb[3]) {
    double l = lab[0] + 0.3963377774 * lab[1] + 0.2158037573 * lab[2];
    double m = lab[0] - 0.1055613458 * lab[1] - 0.0638541728 * lab[2];
    double s = lab[0] - 0.0894841775 * lab[1] - 1.2914855480 * lab[2];
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    rgb[0] = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    rgb[1] = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    rgb[2] = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    for (unsigned c = 0; c < 3; c++)
        if (rgb[c] < -0.0005 || rgb[c] > 1.0005)
            return false;
    return true;
}
/* Reduce chroma at constant lightness and hue until the colour fits. A clip
 * would move the hue, which reads as a colour cast on the wall. */
static inline void color_grade_fit(double lab[3], double rgb[3]) {
    if (color_grade_from_oklab(lab, rgb))
        return;
    double chroma = sqrt(lab[1] * lab[1] + lab[2] * lab[2]);
    if (!(chroma > 1e-9)) {
        /* A neutral out of gamut can only come from a lightness outside [0,1],
         * which color_grade_apply has already clamped. */
        rgb[0] = rgb[1] = rgb[2] = lab[0];
        return;
    }
    double low = 0.0, high = chroma;
    for (unsigned i = 0; i < 12; i++) {
        double middle = (low + high) / 2;
        double scale = middle / chroma;
        double trial[3] = {lab[0], lab[1] * scale, lab[2] * scale};
        if (color_grade_from_oklab(trial, rgb))
            low = middle;
        else
            high = middle;
    }
    double scale = low / chroma;
    lab[1] *= scale;
    lab[2] *= scale;
    (void)color_grade_from_oklab(lab, rgb);
}
static inline void color_grade_apply(const ColorGrade *grade, unsigned char rgb[3]) {
    if (!grade || !grade->enabled)
        return;
    double linear[3];
    for (unsigned c = 0; c < 3; c++)
        linear[c] = color_light(rgb[c]);
    double lab[3];
    color_grade_to_oklab(linear, lab);
    /* Vibrance: the muted get most of the gain, the saturated get little. */
    double chroma = sqrt(lab[1] * lab[1] + lab[2] * lab[2]);
    if (chroma > 1e-6) {
        double saturation = chroma / COLOR_GRADE_CHROMA_REF;
        if (saturation > 1.0)
            saturation = 1.0;
        double gain = 1.0 + grade->vibrance * (1.0 - saturation);
        lab[1] *= gain;
        lab[2] *= gain;
    }
    /* Brightness: the highlights roll off under a ceiling and the whole range
     * is lifted onto a floor. OKLab's L is the cube root of light and the knobs
     * are stated in light, so the curve is applied there. */
    double lightness = lab[0];
    if (lightness < 0.0)
        lightness = 0.0;
    if (lightness > 1.0)
        lightness = 1.0;
    double light = lightness * lightness * lightness;
    light = color_grade_ceiling(light, grade->knee, grade->brightness);
    light = grade->floor + (1.0 - grade->floor) * light;
    if (light < 0.0)
        light = 0.0;
    if (light > 1.0)
        light = 1.0;
    lab[0] = cbrt(light);
    color_grade_fit(lab, linear);
    for (unsigned c = 0; c < 3; c++)
        rgb[c] = color_encode(linear[c]);
}
#endif
