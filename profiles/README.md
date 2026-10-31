# TV model calibration profiles

The built-in LG G4 profile is measured. Other models do not have verified spatial
profiles yet. Auto uses uniform average colors on an unknown model, unless an
imported custom profile matches its exact reported model identifier.
`65QNED87A6B.BEUFLJP.json` is the profile that Auto Calibration measured on the
tested 65QNED87A6B on webOS 24: ten rim anchors and all sixteen region
rectangles. Import it only for the exact model identifier; a model match alone
does not establish correctness on another panel, SoC or firmware.

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

Region rectangles are painted only from actual measurements. Selecting the mode
without measured extents falls back to the perimeter model: blocks invented
from rim anchors produced a Voronoi mosaic that did not describe the picture.

### Measured rectangles

Auto Calibration measures the actual sampling rectangle of every LG histogram
region. The profile stores them next to the rim anchors:

```json
{
  "model": "EXACT-MODEL-IDENTIFIER-FROM-APP",
  "points": [
    {"position": 3.5, "region": 15}
  ],
  "geometry": "rectangles",
  "compensate": false,
  "colorStat": "mean",
  "rectangles": [
    {"region": 15, "x0": 0.0, "y0": 0.41, "x1": 0.19, "y1": 0.79},
    {"region": 4, "x0": 0.19, "y0": 0.3, "x1": 0.38, "y1": 0.82}
  ]
}
```

- 1-16 rectangles, at most one per region index 0-15.
- Coordinates are normalized to the full screen: `0 <= x0 < x1 <= 1` and
  `0 <= y0 < y1 <= 1` (`x` rightwards, `y` downwards).
- Rectangles may overlap, as LG's own regions do. Each rectangle mean is a box
  average of the pixels it covers. The renderer never mixes two windows: it
  picks the smallest measured rectangle that contains the point and copies that
  colour, so every pixel sent to the receiver is one of the sixteen measurements
  unchanged. Outside every rectangle it uses the nearest centre. `status`
  reports `edgeOwnership`, the number of plane cells each measurement owns, so a
  window that never wins its own area is visible instead of silent.
- `"geometry"` defaults to `rectangles` when the list is present; `"geometry":
  "rim"` keeps the rectangles stored but renders the perimeter model.
- Without a `rectangles` list the rectangle mode renders the perimeter model.
  The service never invents rectangle extents from the rim anchors.
- A new `points` list discards rectangles measured for the previous one. To keep
  them, keep the `rectangles` array in the same JSON that carries the new points.
- Only a measured custom profile uses this geometry. Auto, G4 and Average ignore
  it, and the receiver still applies its own LED layout, depth and image
  processing to the plane.
- The status line shows the active geometry, and `status` reports
  `customRectangleCount` (imported), `spatialRectangleCount` (rendered),
  `customGeometry`, `spatialGeometry`, `customCompensate` and
  `customHasReference`.
- Optional `black` and `white` arrays each hold 16 `[r,g,b]` entries measured on
  a full-black and full-white frame. `compensate` is the user toggle; it is off
  by default. When on, each region mean is normalised in linear light against
  the matching reference, which neutralises a Warm picture mode or Eye
  Comfort/Blue Light Reduction on the LEDs. Calibration itself always samples
  raw values.
- `colorStat` selects how a region's 256 bins become one value, and is one of
  `mean` (default), `active`, `p50`, `p90` or `peak`. `active` averages only the
  bins above the measured black level, renormalising the remaining counts, so a
  small bright object is not diluted by a large dark background; it changes which
  pixels are counted, never their ratios. A 12-level codec-noise floor keeps
  near-black dither from switching the estimate between darkness and the lit
  object. A percentile returns the code of the chosen bin unchanged, so it
  round-trips through the encode table exactly. Calibration always samples the raw
  mean, and letterbox/bar detection always uses the plain mean whatever this is set
  to. Profiles written before this key existed used `"activeMean": true` for the
  same behaviour and are still read correctly.
- The profile JSON limit is 8 KiB, enough for 16 points, 16 rectangles and the
  32 reference triplets.

### Measuring extents

Auto Calibration measures the black and white references, then plays nine
vertical and nine horizontal white/black splits. At split `s`, the share of a
region's pixels that is black is the fraction of its rectangle to the right
(or below) `s`, so a bounded least-squares fit recovers `x0/x1` and `y0/y1` with
no gamma assumptions. The response is linear, so nine positions also handle
regions whose response saturates at either end. The fit must stay within
0.12 RMSE and measure at least 4 regions, otherwise the proposal keeps the
perimeter model.

## Guided measurement

The app's auto calibration uses an actual local video plane, not UI overlays.
It collects a black reference and fresh samples from 32 perimeter patches,
rejects global responses, and derives sorted rim positions from each region's
local response peak. A candidate must cover all four edges. The same run then
measures the 16 region rectangles from the split sweeps. Measurement takes
about two minutes. A single missed status reply does not discard the run: a
missing patch is treated as the black reference, and a missing extent step is
skipped by the fit.

The resulting JSON is a proposal until Save profile is selected. Use Validate
profile after saving and check all four color quadrants before contributing the
profile. Cancellation and failed measurement preserve the saved profile.
