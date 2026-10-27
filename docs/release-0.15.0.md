# HyperDRM 0.15.0

Add an **optional rectangle geometry** for measured custom profiles, so the
synthetic HyperHDR plane can follow the TV's real region shapes instead of the
perimeter cones.

Until now `layout_blend()` mapped every pixel to its nearest screen edge and
interpolated the two neighbouring rim samples. That model is 1-D: a single rim
sample owns a whole edge, so one dark or dropped histogram region painted a wide
cone across the plane (for the 65QNED87A6B profile, region 15 alone covered
18.5 % of the image and every left-side LED). LG's regions are in fact measured
rectangles that overlap, so a profile can now carry them:

```json
"geometry": "rectangles",
"rectangles": [{"region": 15, "x0": 0.0, "y0": 0.41, "x1": 0.19, "y1": 0.79}]
```

Rendering: each plane pixel is the `1/area` weighted average of the rectangles
covering it, so a small local rectangle outranks a large nested one. Pixels
outside every rectangle fall back to the nearest measured centre. Rectangles are
validated (region 0-15, `0 <= x0 < x1 <= 1`, `0 <= y0 < y1 <= 1`, no duplicate
region) and stored next to the rim points, so `"geometry": "rim"` renders the old
model from the same file for an A/B comparison.

Scope and safety:

- The model only applies to a measured custom profile; Auto, G4 and Average are
  unchanged, and the built-in G4 rim table is untouched.
- Saving a new `points` list discards rectangles that belonged to the old one;
  switching profile keeps them.
- The per-pixel table is built once per calibration change, so a frame costs the
  same as before. The receiver still samples the plane with its own LED layout,
  depth and image processing.
- `status` reports `customRectangles`, `customGeometry` and the effective
  `spatialGeometry`; the app keeps rectangles when the profile editor is
  reopened and shows `Region rectangles (1/area)` while the model is active.
- The JSON profile limit grows to 4 KiB to hold 16 points and 16 rectangles.

`native/test_rectangles.c` covers 1/area weighting, the single-rectangle and
nearest-centre cases, plane output versus the rim model, rejection of malformed
or duplicate rectangles, stale-rectangle removal, profile switching and
save/load round-trip. No rectangle extents have been measured on a TV yet; the
split-sweep procedure is documented in `profiles/README.md`.
