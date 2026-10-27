# TV model calibration profiles

The built-in LG G4 profile is measured. Other models do not have verified spatial
profiles yet. Auto uses uniform average colors on an unknown model, unless an
imported custom profile matches its exact reported model identifier.

To import measured data, select Custom in the TV app, paste the profile JSON,
and select Save profile. Auto subsequently uses the custom profile on that exact
model. One custom model profile is stored in the own service container. Importing
another replaces it; switching to Auto, G4 or Average preserves the imported data.

```json
{
  "model": "EXACT-MODEL-IDENTIFIER-FROM-APP",
  "points": [
    {"position": 0.25, "region": 7},
    {"position": 2.25, "region": 9}
  ]
}
```

This two-point example describes the file format, not a calibrated TV model.
Use 2-16 actual measured points sorted by position. Region indices are 0-15.
The clockwise perimeter position starts at the top-left corner: top 0-1, right
1-2, bottom 2-3, left 3-4. The final point interpolates back to the first.

Use the coordinate-ramp calibration video with full-screen playback, observe
the LG histogram region colors, and derive their measured perimeter positions.
Validate all four edges against the receiver LED output before sharing a model
profile. A model match alone does not establish correctness on another panel,
SoC or firmware; document that test context with contributed profiles.

Profiles only control spatial region assignment. Saturation, brightness and
smoothing remain receiver settings. Runtime selection invalidates the generated
HyperHDR geometry and refreshes the Hyperion LED mapping without capture restart.
## Spatial geometry

Two renderers turn the 16 samples into the receiver's image. They are a saved
per-profile choice, not a fixed behaviour, because the best one differs between
TVs:

- **Perimeter** (default) interpolates the measured rim positions along the
  screen edge. Simple, but a single sample owns a wide cone, so one dark or
  dropped region paints a large area.
- **Region rectangles** paints blocks instead of cones.

The app offers this as **Geometry: Perimeter / Region rectangles** in the TV
calibration panel (custom profile only). Recalibrating replaces the point list
but keeps the selected geometry; use the same button to compare both models on
the receiver's LED preview and keep the one that matches the LEDs.

### Derived blocks

Selecting rectangles on an ordinary calibrated profile needs no extra data: one
block is derived per rim anchor, covering the arc up to its neighbouring anchors
and deepened inwards by `REGION_BLOCK_DEPTH` (0.19 of the screen). Pixels outside
every block use the nearest block centre. This removes the cones and limits each
sample to its own edge segment, but it is an approximation - the rim calibration
does not measure how far into the picture a region reaches.

### Measured rectangles

A profile may carry the actual sampling rectangle of each region; those always
take precedence over derived blocks:

```json
{
  "model": "EXACT-MODEL-IDENTIFIER-FROM-APP",
  "points": [
    {"position": 3.5, "region": 15}
  ],
  "geometry": "rectangles",
  "rectangles": [
    {"region": 15, "x0": 0.0, "y0": 0.41, "x1": 0.19, "y1": 0.79},
    {"region": 4, "x0": 0.19, "y0": 0.3, "x1": 0.38, "y1": 0.82}
  ]
}
```

- 1-16 rectangles, at most one per region index 0-15.
- Coordinates are normalized to the full screen: `0 <= x0 < x1 <= 1` and
  `0 <= y0 < y1 <= 1` (`x` rightwards, `y` downwards).
- Rectangles may overlap, as LG's own regions do. Each pixel is the `1/area`
  weighted average of the rectangles covering it, so a small local rectangle
  dominates a large nested one. Pixels outside every rectangle use the nearest
  measured centre.
- `"geometry"` defaults to `rectangles` when the list is present; `"geometry":
  "rim"` keeps the rectangles stored but renders the perimeter model.
- A new `points` list discards rectangles measured for the previous one. To keep
  them, keep the `rectangles` array in the same JSON that carries the new points.
- Only a measured custom profile uses this geometry. Auto, G4 and Average ignore
  it, and the receiver still applies its own LED layout, depth and image
  processing to the plane.
- The status line shows the active geometry, and `status` reports
  `customRectangleCount` (imported), `spatialRectangleCount` (rendered),
  `customGeometry` and `spatialGeometry`.

### Measuring extents

The coordinate ramp only yields each region's centre. Show a frame that is white
left of `s` and black right of it; each region's mean colour is then the fraction
of its area left of `s`. Sweeping `s` gives that region's horizontal extent, and
repeating the sweep with top/bottom splits gives the vertical extent. Two split
positions per axis are enough per region because the response is linear.

## Guided measurement

The app's auto calibration uses an actual local video plane, not UI overlays.
It collects a black reference and fresh samples from 32 perimeter patches,
rejects global responses, and derives sorted positions from each region's local
response peak. A candidate must cover all four edges. The resulting JSON is a
proposal until Save profile is selected. Use Validate profile after saving and
check all four color quadrants before contributing the profile. Cancellation and
failed measurement preserve the saved profile.
