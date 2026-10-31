#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "color_grade.h"

static double chroma(const unsigned char rgb[3]) {
    double linear[3], lab[3];
    for (unsigned c = 0; c < 3; c++)
        linear[c] = color_light(rgb[c]);
    color_grade_to_oklab(linear, lab);
    return sqrt(lab[1] * lab[1] + lab[2] * lab[2]);
}

static double lightness(const unsigned char rgb[3]) {
    double linear[3], lab[3];
    for (unsigned c = 0; c < 3; c++)
        linear[c] = color_light(rgb[c]);
    color_grade_to_oklab(linear, lab);
    return lab[0];
}

/* Chroma per unit lightness: the brightness ceiling compresses C with L, so the
 * only fair comparison of "how colourful is this" is the ratio. */
static double saturation(const unsigned char rgb[3]) {
    double light = lightness(rgb);
    return light > 1e-4 ? chroma(rgb) / light : 0.0;
}

static void grade(unsigned preset, unsigned char rgb[3]) {
    ColorGrade settings = color_grade_preset(preset);
    color_grade_apply(&settings, rgb);
}

int main(void) {
    /* The presets round-trip through their saved names. */
    unsigned preset = 99;
    assert(color_grade_parse("off", &preset) && preset == COLOR_GRADE_OFF);
    assert(color_grade_parse("gentle", &preset) && preset == COLOR_GRADE_GENTLE);
    assert(color_grade_parse("strong", &preset) && preset == COLOR_GRADE_STRONG);
    assert(!color_grade_parse("loud", &preset));
    assert(!strcmp(color_grade_key(COLOR_GRADE_GENTLE), "gentle"));

    /* Off is a no-op, byte for byte. */
    unsigned char sample[3] = {37, 122, 201}, untouched[3];
    memcpy(untouched, sample, 3);
    grade(COLOR_GRADE_OFF, sample);
    assert(!memcmp(sample, untouched, 3));

    /* The OKLab pair round-trips: every colour the stage moves goes through it,
     * so its precision is checked directly rather than through a preset. */
    for (unsigned v = 0; v < 256; v += 17) {
        unsigned char rgb[3] = {(unsigned char)v, (unsigned char)(255 - v),
                                (unsigned char)((v * 7) % 256)};
        double linear[3], lab[3], back[3];
        for (unsigned c = 0; c < 3; c++)
            linear[c] = color_light(rgb[c]);
        color_grade_to_oklab(linear, lab);
        assert(color_grade_from_oklab(lab, back));
        for (unsigned c = 0; c < 3; c++)
            assert(fabs(back[c] - linear[c]) < 0.002);
    }
    puts("PASS: OKLab and back agree to a quarter of an 8-bit step");

    /* A neutral stays neutral: all three channels move together or not at all. */
    unsigned char grey[3] = {96, 96, 96};
    grade(COLOR_GRADE_GENTLE, grey);
    assert(grey[0] == grey[1] && grey[1] == grey[2]);

    /* Vibrance lifts a muted colour and hardly touches a saturated one. */
    unsigned char muted[3] = {96, 116, 128}, saturated[3] = {250, 24, 18};
    double muted_before = saturation(muted), saturated_before = saturation(saturated);
    grade(COLOR_GRADE_GENTLE, muted);
    grade(COLOR_GRADE_GENTLE, saturated);
    double muted_after = saturation(muted), saturated_after = saturation(saturated);
    assert(muted_after > muted_before * 1.15);
    assert(saturated_after >= saturated_before * 0.99);
    assert(saturated_after < saturated_before * 1.08);
    printf("PASS: vibrance %.3f -> %.3f muted, %.3f -> %.3f saturated\n", muted_before,
           muted_after, saturated_before, saturated_after);

    /* Brightness is a ceiling: white gets dimmer but stays white, which is what
     * bias lighting should do. */
    unsigned char white[3] = {255, 255, 255};
    grade(COLOR_GRADE_GENTLE, white);
    assert(white[0] == white[1] && white[1] == white[2]);
    assert(white[0] > 240 && white[0] < 255);

    /* The ceiling is not a gain: a midtone keeps its brightness and only the top
     * end rolls off. A plain gain here would dim the whole picture. */
    unsigned char midtone[3] = {128, 128, 128};
    grade(COLOR_GRADE_GENTLE, midtone);
    assert(abs((int)midtone[0] - 128) <= 1);

    /* Brightness is also a floor: black is no longer a hole in the wall. */
    unsigned char black[3] = {0, 0, 0};
    grade(COLOR_GRADE_GENTLE, black);
    assert(black[0] == black[1] && black[1] == black[2]);
    assert(black[0] >= 4 && black[0] <= 8);
    grade(COLOR_GRADE_STRONG, black);
    assert(black[0] >= 10 && black[0] <= 16);

    /* A colour that cannot fit the gamut loses chroma rather than changing hue:
     * the stronger preset must not turn a red into an orange. */
    unsigned char red[3] = {255, 0, 0};
    grade(COLOR_GRADE_STRONG, red);
    assert(red[1] < 40 && red[2] < 40);
    assert(red[0] > 150);

    /* Everything stays inside the output range, whatever goes in. */
    for (unsigned r = 0; r < 256; r += 51)
        for (unsigned g = 0; g < 256; g += 51)
            for (unsigned b = 0; b < 256; b += 51) {
                unsigned char rgb[3] = {(unsigned char)r, (unsigned char)g, (unsigned char)b};
                grade(COLOR_GRADE_STRONG, rgb);
                for (unsigned c = 0; c < 3; c++)
                    assert(rgb[c] <= 255);
                /* OKLab L is the cube root of light, so the ceiling is 0.9^(1/3). */
                assert(lightness(rgb) <= 0.966);
            }
    puts("PASS: the grade holds its ceiling, its floor and its hues");
    return 0;
}
