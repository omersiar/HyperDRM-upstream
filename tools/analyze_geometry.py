"""Turn a geometry scan recording into per-region screen maps.

Input is the JSONL written by `tools/geometry_scan.py`:

* `sample` rows - the sixteen region means from `status`, several per second
* `histograms` rows - raw 16x3x256 counts, about one per segment

The recording has no clock relationship to the video: the app starts the file
when it is ready, so the offset is unknown. The full-white beacon at the head of
every cycle is the only stimulus that lifts all sixteen regions at once, so its
falling edge is the cycle anchor and every sample is placed through the
schedule. Nothing is assumed about where a region samples.

Coverage of region r by block b is a fraction of the region's own area, so the
sixteen values for one block sum to one. `--synthetic` builds the same structures
from a saved profile and runs the identical code path, which validates the math
without the TV.
"""
import argparse
import base64
import json
import math
import statistics
import sys
from array import array
from pathlib import Path

BEACON_LEVEL = 240
MIN_FRACTION = 0.30
MAX_FRACTION = 0.85


# ---------------------------------------------------------------- colour space
def srgb_light(code):
    """Relative light of an encoded 8-bit level, matching native/color_space.h."""
    if code <= 0:
        return 0.0
    if code >= 255:
        return 1.0
    value = code / 255.0
    if value <= 0.04045:
        return value / 12.92
    return ((value + 0.055) / 1.055) ** 2.4


LIGHT = [srgb_light(code) for code in range(256)]


def srgb_encode(light):
    """8-bit level for relative light, matching color_encode() closely enough."""
    value = min(max(light, 0.0), 1.0)
    if value <= 0.0031308:
        encoded = value * 12.92
    else:
        encoded = 1.055 * value ** (1.0 / 2.4) - 0.055
    return int(round(encoded * 255))


# ------------------------------------------------------------------- recording
def decode_bins(text):
    """16 regions x 3 channels x 256 bins of little-endian uint16."""
    raw = base64.b64decode(text, validate=True)
    if len(raw) != 16 * 3 * 256 * 2:
        raise ValueError("histogram payload is {} bytes, expected 24576".format(len(raw)))
    values = array("H")
    values.frombytes(raw)
    if sys.byteorder != "little":
        values.byteswap()
    return [[[values[(region * 3 + channel) * 256 + b] for b in range(256)]
             for channel in range(3)] for region in range(16)]


def load_jsonl(path):
    recording = {"capture": {}, "samples": [], "histograms": []}
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except ValueError:
                continue
            kind = row.get("type")
            if kind == "capture":
                recording["capture"] = row
            elif kind == "sample":
                regions = row.get("regions")
                if isinstance(regions, list) and len(regions) == 16:
                    recording["samples"].append(row)
            elif kind == "histograms" and row.get("bins"):
                try:
                    row["decoded"] = decode_bins(row["bins"])
                except (ValueError, KeyError):
                    continue
                recording["histograms"].append(row)
    return recording


# ------------------------------------------------------------------- alignment
# Brightness below which the frame before a beacon counts as the black lead-in.
# It only has to separate "black" from "a picture that fills the screen": a black
# frame reads 0-70 here, a white stimulus reads 255 in at least one region.
DARK_LEVEL = 150


def beacon_anchors(samples, minimum_seconds=0.0, level=BEACON_LEVEL, require_dark=0.0):
    """Falling edges of the full-white beacon, one per cycle.

    A short white run is rejected because the app's own launch splash is white
    too; after the restart it lands a second before the video's beacon and
    would otherwise be taken for the cycle start.

    `require_dark` also demands that the sample immediately before the run was a
    dark frame. A stimulus that can fill the screen (a 16:9 picture in an aspect
    sweep) is white in every window and so indistinguishable from the beacon by
    brightness alone; the black lead-in is what tells them apart. Testing one
    predecessor sample rather than a duration keeps this correct when the status
    poll is slower than the lead-in is long.
    """
    anchors = []
    run_start = None
    preceded_dark = False
    for index, sample in enumerate(samples):
        dimmest = min(max(sample["regions"][region]) for region in range(16))
        if dimmest >= level:
            if run_start is None:
                run_start = index
                preceded_dark = index > 0 and max(
                    max(channel) for channel in samples[index - 1]["regions"]) <= DARK_LEVEL
            continue
        if run_start is not None:
            if index - run_start >= 2 and \
                    samples[index - 1]["t"] - samples[run_start]["t"] >= minimum_seconds and \
                    (require_dark <= 0 or preceded_dark):
                anchors.append((samples[index - 1]["t"] + sample["t"]) / 2.0)
            run_start = None
    return anchors


def match_cycles(anchors, cycles):
    """Keep one anchor per cycle, preferring the last ones.

    Anything extra is a spurious white frame before the video started.
    """
    if len(anchors) <= len(cycles):
        return anchors
    return anchors[-len(cycles):]


def place_samples(samples, schedule, anchors):
    """Assign samples to blocks; collect the black and white reference samples."""
    segment = schedule["segmentSeconds"]
    blocks = [row for row in schedule["segments"] if row["kind"] == "block"]
    placed, references = {}, []
    used = 0
    for index, cycle in enumerate(schedule["cycles"]):
        if index >= len(anchors):
            break
        used += 1
        anchor = anchors[index]
        for sample in samples:
            video_t = cycle["blocksStart"] + (sample["t"] - anchor)
            if video_t < cycle["start"] or video_t >= cycle["end"]:
                continue
            if video_t < cycle["whiteStart"]:
                references.append((index, "black", sample))
            elif video_t < cycle["blocksStart"]:
                references.append((index, "white", sample))
            else:
                offset = video_t - cycle["blocksStart"]
                number = int(offset // segment)
                if not 0 <= number < len(blocks):
                    continue
                fraction = (offset - number * segment) / segment
                if not MIN_FRACTION <= fraction <= MAX_FRACTION:
                    continue
                block = blocks[number]
                placed.setdefault((index, block["gy"], block["gx"]), []).append(sample)
    return placed, references, used


def reference_levels(references):
    """Per region/channel linear-light pedestal and peak, or None without them."""
    black = [[0.0] * 3 for _ in range(16)]
    white = [[1.0] * 3 for _ in range(16)]
    seen = {"black": 0, "white": 0}
    for kind in ("black", "white"):
        rows = [sample for (_, name, sample) in references if name == kind]
        seen[kind] = len(rows)
        if not rows:
            continue
        target = black if kind == "black" else white
        for region in range(16):
            for channel in range(3):
                target[region][channel] = statistics.median(
                    LIGHT[sample["regions"][region][channel]] for sample in rows
                )
    return black, white, seen


def coverage_of(sample, black, white):
    """Fraction of each region covered by the lit block, in linear light."""
    out = []
    for region in range(16):
        channels = []
        for channel in range(3):
            span = white[region][channel] - black[region][channel]
            if span < 1e-6:
                channels.append(0.0)
            else:
                value = (LIGHT[sample["regions"][region][channel]] - black[region][channel]) / span
                channels.append(max(0.0, value))
        out.append(sum(channels) / 3.0)
    return out


def coverage_grid(placed, black, white, columns, rows, cycle=None):
    """Median coverage per region per block, plus the sample count per block."""
    grid = [[[0.0] * columns for _ in range(rows)] for _ in range(16)]
    counts = [[0] * columns for _ in range(rows)]
    for (owner, gy, gx), rows_here in placed.items():
        if cycle is not None and owner != cycle:
            continue
        values = [coverage_of(sample, black, white) for sample in rows_here]
        counts[gy][gx] = len(values)
        for region in range(16):
            grid[region][gy][gx] = statistics.median(value[region] for value in values)
    return grid, counts


def distribution_distance(first, second, columns, rows):
    """Total variation between two block maps. Used as a noise floor."""
    total_a = sum(sum(row) for row in first) or 1.0
    total_b = sum(sum(row) for row in second) or 1.0
    distance = 0.0
    for gy in range(rows):
        for gx in range(columns):
            distance += abs(first[gy][gx] / total_a - second[gy][gx] / total_b)
    return 0.5 * distance


# ------------------------------------------------------------------------ fits
def box_marginal(x0, x1, cells):
    width = x1 - x0
    if width <= 0:
        return [0.0] * cells
    out = []
    for index in range(cells):
        low, high = index / cells, (index + 1) / cells
        out.append(max(0.0, min(high, x1) - max(low, x0)) / width)
    return out


def gaussian_marginal(mu, sigma, cells):
    scale = math.sqrt(2.0) * sigma
    out = []
    for index in range(cells):
        low, high = index / cells, (index + 1) / cells
        out.append((math.erf((high - mu) / scale) - math.erf((low - mu) / scale)) / 2.0)
    return out


def normalised_rmse(predicted, observed):
    observed_total = sum(observed) or 1.0
    predicted_total = sum(predicted) or 1.0
    total = 0.0
    for p, o in zip(predicted, observed):
        total += ((p / predicted_total) - (o / observed_total)) ** 2
    return math.sqrt(total / max(1, len(observed)))


def _box_error(x0, x1, cells, observed):
    width = x1 - x0
    error = 0.0
    for index in range(cells):
        low, high = index / cells, (index + 1) / cells
        predicted = max(0.0, min(high, x1) - max(low, x0)) / width
        difference = predicted - observed[index]
        error += difference * difference
    return error


def fit_box(observed, steps=64, refine=6, tolerance=0.03):
    """Rectangle extent for one marginal, with the range the data allows.

    A window narrower than about one cell only constrains the mass split across
    the cells it touches, so many extents fit equally well. Returning the mean
    of the near-best set, plus its range, keeps that ambiguity visible instead
    of reporting whichever vertex the optimiser happened to reach.
    """
    cells = len(observed)
    floor = 1.0 / (4 * cells)
    best = None
    candidates = []
    for i in range(steps + 1):
        x0 = i / steps
        for j in range(i + 1, steps + 1):
            x1 = j / steps
            if x1 - x0 < floor:
                continue
            error = _box_error(x0, x1, cells, observed)
            candidates.append((error, x0, x1))
            if best is None or error < best[0]:
                best = (error, x0, x1)
    span = 1.0 / steps
    for _ in range(refine):
        span /= 4.0
        centre = best
        for dx0 in (-span, 0.0, span):
            for dx1 in (-span, 0.0, span):
                x0 = min(max(centre[1] + dx0, 0.0), 1.0)
                x1 = min(max(centre[2] + dx1, 0.0), 1.0)
                if x1 - x0 < floor:
                    continue
                error = _box_error(x0, x1, cells, observed)
                if error < best[0]:
                    best = (error, x0, x1)
    limit = math.sqrt(best[0] / cells) + tolerance
    near = [c for c in candidates if math.sqrt(c[0] / cells) <= limit]
    if not near:
        near = [best]
    x0_values = [c[1] for c in near]
    x1_values = [c[2] for c in near]
    return {
        "x0": sum(x0_values) / len(x0_values),
        "x1": sum(x1_values) / len(x1_values),
        "rmse": math.sqrt(best[0] / cells),
        "x0Range": [min(x0_values), max(x0_values)],
        "x1Range": [min(x1_values), max(x1_values)],
        "ambiguous": (max(x0_values) - min(x0_values) > 2.0 / cells or
                      max(x1_values) - min(x1_values) > 2.0 / cells),
    }


def fit_gaussian(observed, steps=64):
    cells = len(observed)
    best = None
    for i in range(steps + 1):
        mu = i / steps
        for sigma in (0.01 + 0.01 * k for k in range(60)):
            predicted = gaussian_marginal(mu, sigma, cells)
            error = 0.0
            for p, o in zip(predicted, observed):
                error += (p - o) ** 2
            if best is None or error < best[0]:
                best = (error, mu, sigma)
    return {"mu": best[1], "sigma": best[2], "rmse": math.sqrt(best[0] / cells)}


def fit_axis(observed):
    """Best rectangle and best Gaussian for one marginal distribution.

    The marginal is normalised first: a region's absolute coverage is biased low
    because the panel pipeline under-reports very small per-block shares, and
    the shape of the distribution is what identifies the extent.
    """
    total = sum(observed) or 1.0
    shaped = [value / total for value in observed]
    box = fit_box(shaped)
    gauss = fit_gaussian(shaped)
    uniform = normalised_rmse([1.0 / len(shaped)] * len(shaped), shaped)
    box["normalisedRmse"] = normalised_rmse(
        box_marginal(box["x0"], box["x1"], len(shaped)), shaped)
    gauss["normalisedRmse"] = normalised_rmse(
        gaussian_marginal(gauss["mu"], gauss["sigma"], len(shaped)), shaped)
    gauss["support"] = [max(0.0, gauss["mu"] - 2 * gauss["sigma"]),
                        min(1.0, gauss["mu"] + 2 * gauss["sigma"])]
    winner = "box" if box["normalisedRmse"] <= gauss["normalisedRmse"] else "gaussian"
    return {"box": box, "gaussian": gauss, "uniformNormalisedRmse": uniform,
            "preferred": winner}


def separability(grid, columns, rows):
    """Total variation between the block map and the outer product of marginals.

    Zero means the window factorises into an x profile times a y profile, which
    is what a rectangle, a separable tent and a separable Gaussian all do. A
    large value means the window is not a product: an L, a ring or a rotated
    mask has the same marginals but a different joint distribution.
    """
    total = sum(sum(row) for row in grid)
    if total <= 0:
        return 1.0
    mass = [[grid[gy][gx] / total for gx in range(columns)] for gy in range(rows)]
    marginal_x = [sum(mass[gy][gx] for gy in range(rows)) for gx in range(columns)]
    marginal_y = [sum(mass[gy]) for gy in range(rows)]
    distance = 0.0
    for gy in range(rows):
        for gx in range(columns):
            distance += abs(mass[gy][gx] - marginal_y[gy] * marginal_x[gx])
    return 0.5 * distance


def analyse_region(grid, region, columns, rows, minimum_support=0.05):
    peak = max(
        ((grid[region][gy][gx], gx, gy) for gy in range(rows) for gx in range(columns)),
        key=lambda item: item[0],
    )
    marginal_x = [sum(grid[region][gy][gx] for gy in range(rows)) for gx in range(columns)]
    marginal_y = [sum(grid[region][gy][gx] for gx in range(columns)) for gy in range(rows)]
    top = peak[0] or 1.0
    support = sum(
        1 for gy in range(rows) for gx in range(columns)
        if grid[region][gy][gx] >= minimum_support * top
    )
    return {
        "region": region,
        "coverageSum": sum(marginal_x),
        "peak": {"gx": peak[1], "gy": peak[2], "coverage": peak[0]},
        "supportBlocks": support,
        "spread": (min(min(row) for row in grid[region]) /
                   (max(max(row) for row in grid[region]) or 1.0)),
        "x": fit_axis(marginal_x),
        "y": fit_axis(marginal_y),
        "separability": separability(grid[region], columns, rows),
        "marginalX": marginal_x,
        "marginalY": marginal_y,
        "grid": grid[region],
    }


# --------------------------------------------------------------------- reporting
PALETTE = [
    "#e6194b", "#3cb44b", "#ffe119", "#4363d8", "#f58231", "#911eb4", "#46f0f0", "#f032e6",
    "#bcf60c", "#fabebe", "#008080", "#e6beff", "#9a6324", "#fffac8", "#800000", "#aaffc3",
]


def heat_colour(value):
    value = min(max(value, 0.0), 1.0)
    red = min(1.0, value * 2.0)
    green = min(1.0, max(0.0, value * 2.0 - 0.7))
    blue = min(1.0, max(0.0, value * 2.0 - 1.5))
    return "#{:02x}{:02x}{:02x}".format(int(255 * red), int(255 * green), int(255 * blue))


def render_heatmaps(results, columns, rows, path, title):
    cell, pad, label = 11, 10, 44
    panel_w, panel_h = columns * cell + 2 * pad, rows * cell + label + pad
    across = 4
    down = max(1, (len(results) + across - 1) // across)
    width, height = across * panel_w + pad, down * panel_h + pad
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="{}" '
             'viewBox="0 0 {} {}">'.format(width, height, width, height),
             '<rect width="100%" height="100%" fill="#101014"/>',
             '<text x="{}" y="20" fill="#e8e8ee" font-family="monospace" '
             'font-size="14">{}</text>'.format(pad, title)]
    for index, result in enumerate(results):
        left = pad + (index % across) * panel_w
        top = pad + (index // across) * panel_h
        peak = max(max(row) for row in result["grid"]) or 1.0
        parts.append('<text x="{}" y="{}" fill="#e8e8ee" font-family="monospace" '
                     'font-size="12">{:02d}  sum {:.2f}  {}x{:.2f}-{:.2f}</text>'.format(
                         left, top + 12, result["region"], result["coverageSum"],
                         "box" if result["x"]["preferred"] == "box" else "gauss",
                         result["x"]["box"]["x0"], result["x"]["box"]["x1"]))
        parts.append('<text x="{}" y="{}" fill="#8a8a99" font-family="monospace" '
                     'font-size="10">y {:.2f}-{:.2f}  sep {:.2f}</text>'.format(
                         left, top + 26, result["y"]["box"]["x0"], result["y"]["box"]["x1"],
                         result["separability"]))
        origin_y = top + label - 8
        for gy in range(rows):
            for gx in range(columns):
                value = result["grid"][gy][gx] / peak
                parts.append('<rect x="{}" y="{}" width="{}" height="{}" fill="{}"/>'.format(
                    left + gx * cell, origin_y + gy * cell, cell - 1, cell - 1,
                    heat_colour(value)))
        parts.append('<rect x="{}" y="{}" width="{}" height="{}" fill="none" '
                     'stroke="#3a3a46"/>'.format(left - 1, origin_y - 1,
                                                 columns * cell + 1, rows * cell + 1))
    parts.append("</svg>")
    Path(path).write_text("\n".join(parts) + "\n", encoding="utf-8")


def render_screen_map(results, path, title):
    width, height = 960, 540
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="{}" '
             'viewBox="0 0 {} {}">'.format(width, height + 40, width, height + 40),
             '<rect width="100%" height="100%" fill="#101014"/>',
             '<rect x="0" y="0" width="{}" height="{}" fill="#000000" '
             'stroke="#3a3a46"/>'.format(width, height),
             '<text x="10" y="{}" fill="#e8e8ee" font-family="monospace" '
             'font-size="13">{}</text>'.format(height + 26, title)]
    for result in results:
        region = result["region"]
        colour = PALETTE[region % len(PALETTE)]
        box_x, box_y = result["x"]["box"], result["y"]["box"]
        x0, x1 = box_x["x0"] * width, box_x["x1"] * width
        y0, y1 = box_y["x0"] * height, box_y["x1"] * height
        parts.append('<rect x="{:.1f}" y="{:.1f}" width="{:.1f}" height="{:.1f}" fill="{}" '
                     'fill-opacity="0.10" stroke="{}" stroke-width="1.2"/>'.format(
                         x0, y0, max(1.0, x1 - x0), max(1.0, y1 - y0), colour, colour))
        cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        parts.append('<text x="{:.1f}" y="{:.1f}" fill="{}" font-family="monospace" '
                     'font-size="13">{}</text>'.format(cx - 6, cy - 10, colour, region))
        counts = result.get("histogram")
        if counts:
            peak = max(counts) or 1
            spark_w, spark_h = 46.0, 26.0
            step = spark_w / 256.0
            points = []
            for index in range(0, 256, 4):
                value = max(counts[index:index + 4]) / peak
                points.append("{:.1f},{:.1f}".format(
                    cx - spark_w / 2 + index * step, cy + spark_h / 2 - value * spark_h))
            parts.append('<rect x="{:.1f}" y="{:.1f}" width="{:.1f}" height="{:.1f}" '
                         'fill="#000000" fill-opacity="0.55" stroke="{}" '
                         'stroke-opacity="0.5"/>'.format(
                             cx - spark_w / 2, cy - spark_h / 2, spark_w, spark_h, colour))
            parts.append('<polyline points="{}" fill="none" stroke="{}" '
                         'stroke-width="1"/>'.format(" ".join(points), colour))
    parts.append("</svg>")
    Path(path).write_text("\n".join(parts) + "\n", encoding="utf-8")


def write_summary(results, path, schedule, extra_lines=()):
    lines = ["# Histogram region geometry", "",
             "Grid {}x{}, {} s per block, {} cycle(s), {} s of video.".format(
                 schedule["columns"], schedule["rows"], schedule["segmentSeconds"],
                 len(schedule["cycles"]), schedule["durationSeconds"]), ""]
    lines.extend(extra_lines)
    if extra_lines:
        lines.append("")
    lines.append("| region | sum | count area | support | x box | x rmse | y box | y rmse | sep | noise | shape | cnt/area |")
    lines.append("|---:|---:|---:|---:|---|---:|---|---:|---:|---:|---|---:|")
    for result in results:
        lines.append("| {} | {:.3f} | {:.3f} | {} | {:.3f}-{:.3f} | {:.3f} | {:.3f}-{:.3f} | "
                     "{:.3f} | {:.3f} | {:.3f} | {}{} | {:.0f} |".format(
                         result["region"], result["coverageSum"],
                         result.get("areaFromCounts", 0.0),
                         result["supportBlocks"],
                         result["x"]["box"]["x0"], result["x"]["box"]["x1"],
                         result["x"]["box"]["normalisedRmse"],
                         result["y"]["box"]["x0"], result["y"]["box"]["x1"],
                         result["y"]["box"]["normalisedRmse"],
                         result["separability"], result.get("noise", 0.0),
                         result["x"]["preferred"],
                         " ambiguous" if (result["x"]["box"].get("ambiguous") or
                                          result["y"]["box"].get("ambiguous")) else "",
                         result.get("countsPerArea", 0.0)))
    lines.append("")
    lines.append("`sum` is the total block coverage of the region. The panel pipeline")
    lines.append("under-reports very small per-block shares, so large regions sum to less")
    lines.append("than one; extents are therefore fitted on the normalised marginal.")
    lines.append("`count area` divides the raw bin total by one constant for all sixteen")
    lines.append("regions, so it is an independent area estimate that never uses the blocks.")
    lines.append("`sep` is the total variation between the block map and the outer product")
    lines.append("of its marginals; compare it with `noise`, the same distance between the")
    lines.append("two cycles. A separable window only has to fall below that floor.")
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


# -------------------------------------------------------------------- synthetic
def synthetic_recording(schedule, rectangles, blocks_per_second=6):
    """A perfect recording for the given rectangles, for self-tests."""
    samples, histograms = [], []
    rectangles = {rect["region"]: rect for rect in rectangles}
    weights = []
    for region in range(16):
        rect = rectangles.get(region)
        weights.append(((rect["x1"] - rect["x0"]) * (rect["y1"] - rect["y0"]))
                       if rect else 0.0)
    clock = 0.0

    def push(geometry):
        nonlocal clock
        regions = []
        for region in range(16):
            rect = rectangles.get(region)
            coverage = 1.0 if geometry == "white" else 0.0
            if rect is not None and isinstance(geometry, tuple):
                x, y, w, h = geometry
                ix = max(0.0, min(x + w, rect["x1"]) - max(x, rect["x0"]))
                iy = max(0.0, min(y + h, rect["y1"]) - max(y, rect["y0"]))
                area = (rect["x1"] - rect["x0"]) * (rect["y1"] - rect["y0"])
                coverage = ix * iy / area if area > 0 else 0.0
            code = srgb_encode(coverage)
            regions.append([code, code, code])
            samples.append({"t": clock, "seq": len(samples), "regions": regions,
                            "blackPercent": [100 - int(round(100 * coverage))] * 16})
        if int(clock * blocks_per_second) % 2 == 0:
            histograms.append({"t": clock, "seq": len(samples), "decoded": [
                [[max(1, int(round(15000 * weights[r]))) if b == regions[r][0] else 0
                  for b in range(256)] for _ in range(3)] for r in range(16)]})
        clock += 1.0 / blocks_per_second

    for cycle in schedule["cycles"]:
        while clock < cycle["whiteStart"]:
            push("black")
        while clock < cycle["blocksStart"]:
            push("white")
        for segment in schedule["segments"]:
            if segment["cycle"] != cycle["index"] or segment["kind"] != "block":
                continue
            end = segment["end"]
            geometry = (segment["x"] / schedule["width"], segment["y"] / schedule["height"],
                        segment["w"] / schedule["width"], segment["h"] / schedule["height"])
            while clock < end:
                push(geometry)
    return {"capture": {"synthetic": True}, "samples": samples, "histograms": histograms}


# ---------------------------------------------------------------------- analysis
def histogram_lookup(placed, histograms):
    """Per region, the histogram of the block where that region responds most."""
    ordered = sorted(histograms, key=lambda row: row["t"])
    lookup = {}
    for region in range(16):
        best = None
        for key, rows in placed.items():
            hist = next((row for row in ordered if row["seq"] >= rows[0]["seq"]), None)
            if hist is None:
                continue
            value = statistics.median(
                max(sample["regions"][region]) for sample in rows)
            if best is None or value > best[0]:
                best = (value, hist["decoded"][region])
        if best:
            channels = [sum(channel) for channel in best[1]]
            lookup[region] = best[1][channels.index(max(channels))] if max(channels) else best[1][0]
    return lookup


def count_totals(histograms):
    """Median per-region bin total.

    LG counts every pixel the window covers, so the total is proportional to the
    window area whatever the picture shows. It is therefore a second, entirely
    independent area measurement next to the block scan.
    """
    if not histograms:
        return None
    collected = [[] for _ in range(16)]
    for row in histograms:
        for region in range(16):
            collected[region].append(sum(sum(channel) for channel in row["decoded"][region]))
    return [statistics.median(values) for values in collected]


def analyse(recording, schedule, profile=None):
    samples = recording["samples"]
    columns, rows = schedule["columns"], schedule["rows"]
    minimum = 0.35 * schedule["beaconSeconds"]["white"]
    anchors = match_cycles(beacon_anchors(samples, minimum), schedule["cycles"])
    placed, references, cycles_used = place_samples(samples, schedule, anchors)
    black, white, seen = reference_levels(references)
    grids = [coverage_grid(placed, black, white, columns, rows, cycle=index)[0]
             for index in range(cycles_used)]
    _, counts = coverage_grid(placed, black, white, columns, rows)
    if len(grids) > 1:
        grid = [[[statistics.mean(grids[k][region][gy][gx] for k in range(len(grids)))
                  for gx in range(columns)] for gy in range(rows)]
                for region in range(16)]
        noise = [distribution_distance(grids[0][region], grids[1][region], columns, rows)
                 for region in range(16)]
    elif grids:
        grid, noise = grids[0], [0.0] * 16
    else:
        grid = [[[0.0] * columns for _ in range(rows)] for _ in range(16)]
        noise = [1.0] * 16
    results = [analyse_region(grid, region, columns, rows) for region in range(16)]
    for result in results:
        result["noise"] = noise[result["region"]]
    lookup = histogram_lookup(placed, recording["histograms"])
    for result in results:
        if result["region"] in lookup:
            result["histogram"] = lookup[result["region"]]
    report = {
        "schema": 1,
        "columns": columns,
        "rows": rows,
        "durationSeconds": schedule["durationSeconds"],
        "samples": len(samples),
        "histogramFrames": len(recording["histograms"]),
        "beacons": len(anchors),
        "cyclesUsed": cycles_used,
        "referenceSamples": seen,
        "blockSamples": {str((gy, gx)): counts[gy][gx]
                         for gy in range(rows) for gx in range(columns)},
        "capture": recording["capture"],
        "regions": results,
    }
    totals = count_totals(recording["histograms"])
    if totals:
        ratios = []
        for result in results:
            area = ((result["x"]["box"]["x1"] - result["x"]["box"]["x0"]) *
                    (result["y"]["box"]["x1"] - result["y"]["box"]["x0"]))
            result["countTotal"] = totals[result["region"]]
            result["countsPerArea"] = totals[result["region"]] / area if area > 0 else 0.0
            ratios.append(result["countsPerArea"])
        middle = statistics.median(ratios)
        report["countsPerAreaMedian"] = middle
        report["countsPerAreaSpread"] = (
            statistics.pstdev(ratios) / middle if middle > 0 else 0.0)
        for result in results:
            if middle > 0:
                result["areaFromCounts"] = result["countTotal"] / middle
    if profile:
        saved = {rect["region"]: rect for rect in profile.get("rectangles", [])}
        for result in results:
            rect = saved.get(result["region"])
            if not rect:
                continue
            result["savedRectangle"] = rect
            result["delta"] = {
                "x0": result["x"]["box"]["x0"] - rect["x0"],
                "x1": result["x"]["box"]["x1"] - rect["x1"],
                "y0": result["y"]["box"]["x0"] - rect["y0"],
                "y1": result["y"]["box"]["x1"] - rect["y1"],
            }
    return report


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", help="JSONL from tools/geometry_scan.py")
    parser.add_argument("--schedule", required=True, help="schedule JSON")
    parser.add_argument("--profile", help="profile JSON to compare against")
    parser.add_argument("--out-dir", default="video-tools/report")
    parser.add_argument("--synthetic", action="store_true",
                        help="derive a perfect recording from --profile and analyse it")
    args = parser.parse_args()

    schedule = json.loads(Path(args.schedule).read_text())
    profile = json.loads(Path(args.profile).read_text()) if args.profile else None

    if args.synthetic:
        if not profile or not profile.get("rectangles"):
            parser.error("--synthetic needs --profile with a rectangles list")
        recording = synthetic_recording(schedule, profile["rectangles"])
    else:
        if not args.input:
            parser.error("--input is required unless --synthetic is used")
        recording = load_jsonl(args.input)

    report = analyse(recording, schedule, profile)
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    (out / "geometry.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    render_heatmaps(report["regions"], report["columns"], report["rows"],
                    out / "heatmaps.svg",
                    "Region coverage  ({}x{}, {} beacons)".format(
                        report["columns"], report["rows"], report["beacons"]))
    render_screen_map(report["regions"],
                      out / "screen-map.svg", "Fitted rectangles and response histograms")
    extra = ["Samples {}, histogram frames {}, beacons {}, cycles used {}.".format(
        report["samples"], report["histogramFrames"], report["beacons"],
        report["cyclesUsed"])]
    if report["beacons"] < len(schedule["cycles"]):
        extra.append("WARNING: found {} beacons for {} cycles; some cycles were dropped.".format(
            report["beacons"], len(schedule["cycles"])))
    if "countsPerAreaSpread" in report:
        extra.append("Bin total / fitted area is constant within {:.1f} % across the sixteen "
                     "regions (median {:.0f}).".format(
                         100 * report["countsPerAreaSpread"], report["countsPerAreaMedian"]))
    write_summary(report["regions"], out / "summary.md", schedule, extra)

    print("beacons {} / {} cycles, {} samples, {} histogram frames".format(
        report["beacons"], len(schedule["cycles"]), report["samples"],
        report["histogramFrames"]))
    print("region  sum   xbox          ybox          sep")
    for result in report["regions"]:
        print("{:>6}  {:.3f} {:.3f}-{:.3f}   {:.3f}-{:.3f}   {:.3f}".format(
            result["region"], result["coverageSum"],
            result["x"]["box"]["x0"], result["x"]["box"]["x1"],
            result["y"]["box"]["x0"], result["y"]["box"]["x1"],
            result["separability"]))
    print(out / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
