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

## Guided measurement

The app's auto calibration uses an actual local video plane, not UI overlays.
It collects a black reference and fresh samples from 32 perimeter patches,
rejects global responses, and derives sorted positions from each region's local
response peak. A candidate must cover all four edges. The resulting JSON is a
proposal until Save profile is selected. Use Validate profile after saving and
check all four color quadrants before contributing the profile. Cancellation and
failed measurement preserve the saved profile.
