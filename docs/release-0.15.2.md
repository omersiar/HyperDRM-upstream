# HyperDRM 0.15.2

Compute histogram means and spatial blends in linear light.

LG reports each region as the mean of encoded 8-bit samples, and the 160x90 plane
blended two region samples in the encoded domain as well. Both under-report
light, so any region that straddles two colors arrived at the LEDs darker than
the picture: on the four-quadrant validation video the top-centre region was
rgb(125,110,35) instead of the picture's mix rgb(169,147,38), and the
bottom-centre region was rgb(128,132,135) instead of rgb(173,174,171). Perceived
luminance of those regions was 47-63 % of the content, which is the "dim middle"
seen next to the correct corner regions.

Measured relative light (IEC 61966-2-1) of the validation video:

| region | previous encoded mean | now, mean of light | picture |
|---|---|---|---|
| top centre (red+green) | Y = 0.156 | Y = 0.294 | Y = 0.295 |
| left (red+yellow) | Y = 0.327 | Y = 0.473 | Y = 0.473 |
| right (green+blue) | Y = 0.143 | Y = 0.243 | Y = 0.242 |
| bottom centre (yellow+blue) | Y = 0.228 | Y = 0.421 | Y = 0.420 |

Changes:

- `native/color_space.h` provides light <-> encoded conversion through lookup
  tables built on first use, so the render loop stays arithmetic. Every 8-bit
  level round-trips exactly and the encode table is forced monotonic.
- `histogram_mean()` averages bin counts in light instead of averaging the bin
  index. Single-value regions are unchanged; mixed regions become as bright as
  the content.
- `blend_color()` mixes in light, so transitions between two lit regions keep
  their brightness instead of sagging towards a dark encoded average. Blending
  red and green now passes through rgb(188,188,0) rather than rgb(127,128,0).
- The service links `-lm`; `release.py` and the README build command are
  updated.

Tests cover the light mean of all 256 levels (151 instead of 128), the
black+white region case, the linear-light midpoint, unchanged single-value
regions, endpoints, monotonicity and constant colors, plus the existing spatial,
rectangle and service suites. Regions are unchanged when they contain one color,
so pure corners and single-color content behave exactly as before.
