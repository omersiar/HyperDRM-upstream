#include <assert.h>
#include <stdio.h>
#include "dark_color.h"
int main(void) {
    for (unsigned v = 0; v <= 255; v++) {
        unsigned char gray[3] = {v, v, v};
        dark_color_filter(gray);
        unsigned expected = v <= 16 ? 0 : v >= 32 ? v : (v * (v - 16) + 8) / 16;
        for (unsigned c = 0; c < 3; c++) assert(gray[c] == expected);
    }
    unsigned char residual[3] = {4, 8, 2};
    dark_color_filter(residual);
    assert(!residual[0] && !residual[1] && !residual[2]);
    unsigned char measured[3] = {13, 14, 7};
    dark_color_filter(measured);
    assert(!measured[0] && !measured[1] && !measured[2]);
    unsigned char transition[3] = {12, 24, 8};
    dark_color_filter(transition);
    assert(transition[0] == 6 && transition[1] == 12 && transition[2] == 4);
    unsigned char saturated[3] = {0, 32, 0};
    dark_color_filter(saturated);
    assert(saturated[0] == 0 && saturated[1] == 32 && saturated[2] == 0);
    puts("PASS: near-black suppression, gradual transition, unchanged brighter colors");
}
