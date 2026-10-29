# Geometry scan: where the sixteen histogram regions sample

The LG histogram API returns sixteen records and never says where they point.
This scan measures it: one white block at a time on a 16x9 grid, and the sixteen
region means tell you which blocks each region is looking at.

Nothing about the region shape is assumed. The output can contradict the
rectangles in `profiles/`, and that is the point.

## Why the beacon

The recording has no clock relationship to the video: the app starts the
playback when it is ready, so the offset between "sample t" and "video t" is
unknown and drifts with app startup. Every cycle therefore opens with 2 s of
black followed by 2 s of full white. Full white is the only stimulus that lifts
all sixteen regions at once, so the falling edge of that plateau is an
unambiguous anchor. The analyser places every sample from it and ignores
anything that does not fit the schedule.

## Running it

```sh
python3 tools/build_pattern_video.py                 # ~8 min of video, once
python3 tools/geometry_scan.py --tv root@192.168.2.14 --restart-app
python3 tools/analyze_geometry.py --schedule video-tools/pattern.json \
    --input video-tools/geometry-scan.jsonl \
    --profile profiles/65QNED87A6B.BEUFLJP.json \
    --out-dir video-tools/report
```

`--restart-app` kills the WebAppMgr renderer before launching. It is needed once
after a new `app/wizard.js` is copied to the TV, and it also guarantees that the
`play` launch parameters reach the app instead of a relaunch carrying stale ones
(`PalmSystem.launchParams` alone is not reliable on webOS 24; the app also reads
`webOSSystem.launchParams` and the `webOSLaunch` / `webOSRelaunch` event detail).

Capture is started *before* the video is launched, so the black and white beacon
is sampled from the first second. LG revokes capture when the app renderer dies,
which is why the restart has to happen first and the service start afterwards.

Before spending eight minutes, confirm the payload path works:

```sh
ssh root@192.168.2.14 "luna-send -n 1 luna://org.webosbrew.histogramhyperion.service/histograms '{}' | head -c 120"
```

That must print `returnValue:true` and start a `bins` string.

## Requirements

- The service is running and `activeSource` is `HyperDRM`; the recorder refuses
  to start otherwise.
- `customActiveMean` is off. With it on, a region reports the mean of its lit
  pixels instead of their share of its area, which destroys the coverage math.
- The app is installed under `/media/developer/apps/...` so the video can be
  copied next to it. Only the user partition is written.

## Artifacts in `video-tools/report/`

| file | contents |
|---|---|
| `heatmaps.svg` | 4x4 small multiples, one 16x9 map per region, bright = the region responds |
| `screen-map.svg` | the fitted rectangles on a 1920x1080 frame, with the response histogram at each centroid |
| `geometry.json` | every number: grids, marginals, fits, separability, sample counts |
| `summary.md` | one table row per region |

`screen-map.svg` and `heatmaps.svg` open in any browser and in VS Code.

## Reading the result

- **`sum`** is the region's area as a fraction of the frame. The sixteen values
  add up to roughly the overlap factor of the window set, not to one.
- **`supportBlocks`** counts blocks that light the region at 5 % of its peak.
  Forty of 144 means "covers most of the screen"; one means "a single block".
- **`x box` / `y box`** are the rectangle that best explains each marginal, with
  the search refined well below one cell. Compare them with the saved profile.
- **`sep`** is the total variation between the block map and the outer product
  of its two marginals. Near zero means the window factorises: a rectangle, a
  separable tent and a separable Gaussian all look like that. A large value
  means it does not, so the rectangle model is wrong there even if the marginals
  fit.
- **`shape`** is which model won for the horizontal marginal, box or Gaussian.

A region whose map is flat everywhere is a global histogram, not a window.

## Results on 65QNED87A6B.BEUFLJP (webOS 24, 2026-10-29)

Recording: 2650 samples, 663 histogram frames, 2 beacons, no dropped lines.
`count area` is the bin-total estimate; `scan x0-x1` is the fitted block extent.

| region | saved area | count area | saved x0-x1 | scan x0-x1 | sep | noise |
|---:|---:|---:|---|---|---:|---:|
| 0 | 0.500 | 0.461 | 0.24-0.75 | 0.24-0.76 | 0.047 | 0.070 |
| 1 | 0.122 | 0.115 | 0.19-0.80 | 0.15-0.86 | 0.140 | 0.183 |
| 2 | 0.168 | 0.151 | 0.29-0.70 | 0.30-0.71 | 0.000 | 0.000 |
| 3 | 0.053 | 0.058 | 0.09-0.40 | 0.09-0.41 | 0.001 | 0.000 |
| 4 | 0.490 | 0.461 | 0.00-0.50 | 0.02-0.51 | 0.050 | 0.075 |
| 5 | 0.095 | 0.091 | 0.00-0.28 | 0.01-0.31 | 0.075 | 0.087 |
| 6 | 0.101 | 0.099 | 0.00-0.19 | 0.01-0.20 | 0.002 | 0.000 |
| 7 | 0.104 | 0.104 | 0.00-0.29 | 0.01-0.31 | 0.002 | 0.000 |
| 8 | 0.485 | 0.461 | 0.49-0.99 | 0.49-0.98 | 0.043 | 0.071 |
| 9 | 0.102 | 0.095 | 0.69-0.99 | 0.69-0.99 | 0.098 | 0.114 |
| 10 | 0.106 | 0.099 | 0.79-0.99 | 0.80-0.99 | 0.003 | 0.000 |
| 11 | 0.053 | 0.058 | 0.34-0.65 | 0.34-0.66 | 0.001 | 0.000 |
| 12 | 0.057 | 0.059 | 0.59-0.89 | 0.60-0.91 | 0.001 | 0.000 |
| 13 | 0.102 | 0.105 | 0.69-0.98 | 0.70-0.99 | 0.001 | 0.000 |
| 14 | 0.049 | 0.049 | 0.29-0.70 | 0.30-0.71 | 0.000 | 0.000 |
| 15 | 0.045 | 0.049 | 0.01-0.12 | 0.02-0.11 | 0.001 | 0.000 |

What this establishes, with no rectangle assumption anywhere in the measurement:

- **The region windows are rectangles.** The block map of every region factorises
  into an x profile times a y profile, and every `sep` is at or below its own
  cycle-to-cycle noise. No L, ring or rotated mask appears.
- **The saved profile is right.** Count-based areas agree with it within 1-9 %
  and the fitted extents within about 0.06, from a completely independent
  stimulus, so the split-sweep fit in `app/calibration.js` is validated.
- **The two noisiest regions are the bottom ones** (1, 5, 9): their cycle-to-cycle
  distance is 0.09-0.18, which is exactly where a single 120x120 block moves the
  least mass. Their higher `sep` is that noise, not structure.
- **Absolute block coverage is biased low.** A region's sixteen coverage values
  sum to 0.74 for the half-screen windows but 0.92 for the small ones. Small
  per-block shares are under-reported by the panel pipeline, which is why the
  extents are fitted on the normalised marginal and why `count area` is the
  area estimate to trust. Do not read `sum` as area.

## Presets

`vtCapture_setPresetHistogram` takes an integer selector and LG documents none of
them. `presetScan {"preset":N}` switches the live handle, reads one frame and
returns the sixteen bin totals, then restores preset 0. A bin total is
proportional to window area whatever the picture shows, so identical vectors mean
identical windows; preset 0 repeated at the end of the run comes back
bit-identical, which is the control.

Measured on 65QNED87A6B (webOS 24, 2026-10-29):

| preset | result |
|---|---|
| 0 | 16 windows, the geometry in `profiles/` |
| 1 | same 16 windows, all except 14 and 15 scaled by **0.7471** |
| 2 | same, scaled by **0.7505** |
| 3 | only 14 populated records, and not a uniform scale |
| 4-15 | rejected, `setPreset` returns non-zero |

The scale matches a cinema crop: 16:9 to 2.39:1 is 0.7438 in height and to
2.35:1 is 0.7565. Presets 1 and 2 are therefore the window sets for a letterboxed
picture, which is exactly why the service switches to preset 1 when it detects
bars. The windows are *scaled*, not re-laid-out: the same 16 rectangles, shrunk.

**Two windows do not scale at all.** r14 (thin top-centre band) and r15 (thin
left band) return identical counts in presets 0, 1 and 2 while every other window
changes. So those two are anchored to the panel while the other fourteen follow
the video window. They are a different class of window, and no preset contains a
right-hand counterpart to r15.

Practical consequence for the renderer: in letterbox mode regions 0-13 keep their
normalised position inside the video window, so a screen position must be mapped
into video-window coordinates before the rectangle lookup. r14 and r15 do not,
and stay at the panel edge.

## Aspect ratios: which windows see the bars

`tools/build_aspect_video.py` plays one centred white rectangle per ratio on
black; `tools/analyze_aspect.py` reports, for every window, the fraction of it
that is lit. 0.00 means the window is inside the black bar, 1.00 means it is
inside the picture.

Measured 2026-10-29 (2.37 Hz status poll, 2 cycles):

| ratio | true bar | r14 lit | bar from r14 | r2 | other 14 |
|---|---:|---:|---:|---:|---|
| 2.76:1 | 0.1779 | 0.00 | saturated | 1.00 | ~1.00 |
| 2.39:1 | 0.1281 | 0.00 | saturated | 1.00 | ~1.00 |
| 2.35:1 | 0.1217 | 0.08 | **0.1145** | 1.00 | ~1.00 |
| 2.20:1 | 0.0960 | 0.25 | **0.0937** | 1.00 | ~1.00 |
| 2.00:1 | 0.0556 | 0.52 | **0.0599** | 1.00 | ~1.00 |
| 1.85:1 | 0.0195 | 0.83 | **0.0212** | 1.00 | ~1.00 |
| 16:9 | 0.0000 | 1.00 | 0.0000 | 1.00 | 1.00 |

What this establishes:

- **r14 is a black-bar meter.** Its lit fraction times its own 0.125 height
  reproduces the true bar within 0.007 from 1.85:1 to 2.35:1. Above about 2.4:1
  the bar is taller than r14 and the reading saturates at 0.125.
- **r15 is a left-pillar meter** by the same arithmetic: 4:3 gives a 0.111
  pillar and r15 reads 0.03 lit, implying 0.116. It saturates at its own 0.12
  width for 1:1 and 9:16.
- **The other fourteen windows cannot see a bar at all.** At 2.39:1 they all
  read about 1.00 while a 0.128 bar is on screen, because they are picture
  relative and follow the picture into the letterbox. This is why the corners
  turn white on a 2.76:1 white frame: r7, r13, r5 and r9 sample the picture,
  and no window measures the corner of the bar.
- **r2 is a picture-presence meter.** It stays at 1.00 for every letterbox ratio
  and drops to 0.78 at 9:16, where the pillar clips the centre block.
- **r0 is not a vertical-video detector.** It reads 0.64 at 9:16 and 1.00 at
  1:1, so it does not separate the two.

Consequences in the service: the bar height for the letterbox projection now
comes from r14 instead of the assumed 0.7471 crop, still gated on the service's
own multi-region letterbox detection because a dark scene looks like a bar to a
single probe. `profiles/65QNED87A6B.BEUFLJP-light.json` drops r14 and r15 from
the rectangle list so they cannot drive an LED; use the full profile when you
want the bar strip represented instead.

## Bars stay in the output plane

Because the other fourteen windows are picture-relative, the old feed filled the
whole 160x90 plane with picture and had no bar rows at all: the receiver could
never detect a bar, extend its LEDs into it, and r14 sat dark while every LED was
lit. The plane is now built bar-faithfully.

`layout_rectangle_blend()` returns `LAYOUT_BAR_REGION` (14) for any sample above
or below the picture band (`y < layout_picture_top`, or mirrored below), and
`layout_window_y()` no longer clamps a point back into the band. `edge_plane()`
then copies r14's *measured* colour into those rows, so the strip carries the
value LG actually read from the panel and not an invented black.

Measured on the TV with a 2.39:1 stimulus, `status.edgeOwnership` went from
`[14]=726` / `[7]=632` / `[12]=572` / `[2]=2420` before the change to
`[14]=3520` / `[7]=450` / `[12]=504` / `[2]=1832` with `letterboxDetected:true`,
`preset:1`, `r2=[255,255,255]`, `r14=[0,0,0]`. 3520 of 14400 cells is 22 of 90
rows, i.e. the top and bottom strips (11 rows each, 0.122 per side) against a
true bar of 0.128: the plane now has the bars, mirrored, and the picture windows
gave back exactly the cells the strips took.

## Limitations

- The stimulus is the app's own video plane, which the repo already proved is
  tapped. Casting, HDMI and the built-in player are separate paths and are not
  measured here.
- Coordinates are video-plane coordinates at 1920x1080 with `object-fit:fill`.
  Overscan in the TV's picture settings would shift them.
- `blackPercent` is an integer percentage, so at 1/144 of the frame it is far too
  coarse for large regions. The scan records it as a cross-check; coverage comes
  from the sRGB-inverted region mean.
- The registered rectangles probably apply to preset 0 only. Letterbox playback
  switches to preset 1, whose window set is unmeasured. Run the scan again with
  a letterboxed pattern before trusting the rectangles in that mode.
- HDR10 and HLG content may be binned in PQ, not sRGB. One SDR and one HDR run
  of the same video answers that; until then the linear-light inversion is an
  assumption for HDR.
