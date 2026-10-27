# HyperDRM 0.15.1

Make the region rectangle geometry an actual selectable option instead of a
JSON-only flag.

The 0.15.0 build only rendered rectangles when a profile carried a `rectangles`
array, and a normal recalibration replaced the profile with its points-only
result, which silently dropped the mode. Users therefore had to hand-edit JSON
after every calibration to compare the two models.

Changes:

- **Geometry: Perimeter / Region rectangles** button in the TV calibration
  panel (custom profile only). The choice is saved with the profile, so it
  survives recalibration, service restarts and profile switches.
- **Derived blocks**: selecting rectangles without imported data builds one
  block per measured rim anchor - the arc up to the neighbouring anchors,
  deepened inwards by `REGION_BLOCK_DEPTH` (0.19). Pixels outside every block use
  the nearest block centre. A calibrated profile can therefore be compared
  against the perimeter model without entering any coordinates.
- Measured rectangles still take precedence over derived blocks, remain
  editable in the profile JSON, and are the only way to express real extents.
- Recalibration keeps the selected geometry and replaces only the point list;
  an imported rectangle list is kept when it is part of the same JSON.
- The profile status line shows the active geometry next to the profile, and
  `status` adds `spatialRectangleCount` next to `customRectangleCount`.
- The profile editor field accepts 4 KiB, matching the service limit for 16
  points and 16 rectangles.

`native/test_rectangles.c` additionally covers derived blocks, the 1/area blend
of overlapping corner blocks, nearest-centre fallback, keeping the geometry
across recalibration and persistence of a derived-mode profile. The app tests
cover the toggle, the profile round-trip and the wizard keeping the geometry.
No rectangle extents have been measured on a TV; the split-sweep procedure is in
`profiles/README.md`.
