"""Read an aspect-ratio sweep and report what each window actually sees.

The stimulus is a centred white rectangle on black. For any window, the measured
mean is the lit fraction of that window, so the table below is literally "how
much of this window is inside the picture at this ratio". A window that reads
zero at 2.76:1 is entirely inside the black bar, which is what a bar detector
must do.

The alignment, the beacon anchoring and the linear-light conversion are the same
ones the geometry scan uses; only the interpretation is new.
"""
import argparse
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_geometry as geo

PANEL_RATIO = 16 / 9
MIN_FRACTION = 0.30
MAX_FRACTION = 0.85


def picture_band(ratio, panel=PANEL_RATIO):
    """(x0, y0, x1, y1) of a centred picture of the given ratio on the panel."""
    if ratio >= panel:
        half = (1.0 - panel / ratio) / 2.0
        return 0.0, half, 1.0, 1.0 - half
    half = (1.0 - ratio / panel) / 2.0
    return half, 0.0, 1.0 - half, 1.0


def place(samples, schedule, anchors):
    """One bucket per (ratio, region) of the samples taken inside that segment."""
    buckets, references = {}, []
    for index, cycle in enumerate(schedule["cycles"]):
        if index >= len(anchors):
            break
        anchor = anchors[index]
        for sample in samples:
            video_t = cycle["blocksStart"] + (sample["t"] - anchor)
            if video_t < cycle["start"] or video_t >= cycle["end"]:
                continue
            if video_t < cycle["blocksStart"]:
                references.append((index, "white" if video_t >= cycle["whiteStart"] else "black",
                                   sample))
                continue
            segment = next((s for s in schedule["segments"]
                            if s["cycle"] == cycle["index"] and s["kind"] == "block"
                            and s["start"] <= video_t < s["end"]), None)
            if segment is None:
                continue
            fraction = (video_t - segment["start"]) / (segment["end"] - segment["start"])
            if not MIN_FRACTION <= fraction <= MAX_FRACTION:
                continue
            buckets.setdefault(segment["ratio"], []).append(sample)
    return buckets, references


def coverage_by_ratio(buckets, black, white):
    """ratio -> list of 16 lit fractions."""
    table = {}
    for ratio, rows in buckets.items():
        values = []
        for region in range(16):
            channels = []
            for channel in range(3):
                samples = []
                for sample in rows:
                    span = white[region][channel] - black[region][channel]
                    if span > 1e-6:
                        light = geo.LIGHT[sample["regions"][region][channel]]
                        samples.append(max(0.0, (light - black[region][channel]) / span))
                if samples:
                    channels.append(statistics.median(samples))
            values.append(sum(channels) / len(channels) if channels else 0.0)
        table[ratio] = values
    return table


def classify(table, regions=16):
    """Per region: what it sees at the widest and at the narrowest ratio."""
    ratios = sorted(table, reverse=True)
    if not ratios:
        return []
    widest, narrowest = ratios[0], ratios[-1]
    out = []
    for region in range(regions):
        wide = table[widest][region]
        narrow = table[narrowest][region]
        role = []
        if wide < 0.10:
            role.append("letterbox detector")
        elif wide < 0.60:
            role.append("partly in the bar")
        if narrow < 0.10:
            role.append("pillarbox detector")
        elif narrow < 0.60:
            role.append("partly in the pillar")
        if not role:
            role.append("picture content")
        out.append({"region": region, "atWidest": wide, "atNarrowest": narrow,
                    "roles": role})
    return out


def heat(value):
    value = min(max(value, 0.0), 1.0)
    if value < 0.5:
        return "#{:02x}{:02x}{:02x}".format(int(40 + 400 * value), int(10 + 60 * value), 20)
    return "#{:02x}{:02x}{:02x}".format(220, int(60 + 380 * (value - 0.5)), 30)


def render(table, path, title):
    ratios = sorted(table, reverse=True)
    cell_w, cell_h, left, top = 34, 20, 96, 54
    width = left + cell_w * 16 + 8
    height = top + cell_h * len(ratios) + 8
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="{}">'.format(
        width, height),
        '<rect width="100%" height="100%" fill="#101014"/>',
        '<text x="8" y="20" fill="#e8e8ee" font-family="monospace" '
        'font-size="13">{}</text>'.format(title),
        '<text x="8" y="38" fill="#8a8a99" font-family="monospace" font-size="10">'
        '0 = window is fully inside the black bar, 1 = fully inside the picture</text>']
    for region in range(16):
        parts.append('<text x="{}" y="{}" fill="#8a8a99" font-family="monospace" '
                     'font-size="11">r{}</text>'.format(left + region * cell_w + 8, top - 4,
                                                        region))
    for row, ratio in enumerate(ratios):
        y = top + row * cell_h
        parts.append('<text x="8" y="{}" fill="#e8e8ee" font-family="monospace" '
                     'font-size="11">{:.2f}:1</text>'.format(y + 14, ratio))
        for region in range(16):
            value = table[ratio][region]
            parts.append('<rect x="{}" y="{}" width="{}" height="{}" fill="{}"/>'.format(
                left + region * cell_w, y, cell_w - 1, cell_h - 1, heat(value)))
            parts.append('<text x="{}" y="{}" fill="#f0f0f0" font-family="monospace" '
                         'font-size="9">{:.2f}</text>'.format(
                             left + region * cell_w + 6, y + 14, value))
    parts.append("</svg>")
    Path(path).write_text("\n".join(parts) + "\n", encoding="utf-8")


def analyse(recording, schedule):
    samples = recording["samples"]
    minimum = 0.35 * schedule["beaconSeconds"]["white"]
    anchors = geo.match_cycles(
        geo.beacon_anchors(samples, minimum, require_dark=1), schedule["cycles"])
    buckets, references = place(samples, schedule, anchors)
    black, white, seen = geo.reference_levels(references)
    table = coverage_by_ratio(buckets, black, white)
    return {
        "schema": 1,
        "beacons": len(anchors),
        "cycles": len(schedule["cycles"]),
        "samples": len(samples),
        "referenceSamples": seen,
        "segmentsPerRatio": {str(ratio): len(rows) for ratio, rows in buckets.items()},
        "coverage": {str(ratio): values for ratio, values in table.items()},
        "roles": classify(table),
    }, table


def write_summary(report, table, path):
    ratios = sorted(table, reverse=True)
    lines = ["# Aspect-ratio sweep", "",
             "Lit fraction of each window. 0.00 means the window is entirely inside",
             "the black bar at that ratio; 1.00 means it is entirely inside the picture.", ""]
    header = "| ratio | " + " | ".join("r%d" % i for i in range(16)) + " |"
    lines.append(header)
    lines.append("|---|" + "---|" * 16)
    for ratio in ratios:
        lines.append("| {:.2f}:1 | ".format(ratio) +
                     " | ".join("{:.2f}".format(v) for v in table[ratio]) + " |")
    lines.append("")
    lines.append("| region | at {:.2f}:1 | at {:.2f}:1 | role |".format(ratios[0], ratios[-1]))
    lines.append("|---:|---:|---:|---|")
    for row in report["roles"]:
        lines.append("| {} | {:.2f} | {:.2f} | {} |".format(
            row["region"], row["atWidest"], row["atNarrowest"], ", ".join(row["roles"])))
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def synthetic_recording(schedule, rectangles, per_second=6):
    """A perfect recording of the sweep, so the analyser can be checked offline."""
    samples, clock = [], 0.0
    rects = {r["region"]: r for r in rectangles}

    def push(band):
        nonlocal clock
        regions = []
        for region in range(16):
            window = rects.get(region)
            lit = 1.0 if band is None else 0.0
            if window is not None and band is not None:
                x0, y0, x1, y1 = band
                ix = max(0.0, min(x1, window["x1"]) - max(x0, window["x0"]))
                iy = max(0.0, min(y1, window["y1"]) - max(y0, window["y0"]))
                area = ((window["x1"] - window["x0"]) * (window["y1"] - window["y0"]))
                lit = ix * iy / area if area > 0 else 0.0
            code = geo.srgb_encode(lit)
            regions.append([code, code, code])
        samples.append({"t": clock, "seq": len(samples), "regions": regions,
                        "blackPercent": [100 - int(round(100 * lit))] * 16})
        clock += 1.0 / per_second

    for cycle in schedule["cycles"]:
        for segment in schedule["segments"]:
            if segment["cycle"] != cycle["index"]:
                continue
            if segment["kind"] == "black":
                band = (0.0, 0.0, 0.0, 0.0)
            elif segment["kind"] == "white":
                band = None
            else:
                band = picture_band(segment["ratio"])
            while clock < segment["end"] - 1e-9:
                push(band)
    return {"capture": {}, "samples": samples, "histograms": []}


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", help="JSONL from tools/geometry_scan.py")
    parser.add_argument("--schedule", required=True)
    parser.add_argument("--profile", help="profile JSON, required for --synthetic")
    parser.add_argument("--out-dir", default="video-tools/report-aspect")
    parser.add_argument("--synthetic", action="store_true")
    args = parser.parse_args()

    schedule = json.loads(Path(args.schedule).read_text())
    if args.synthetic:
        profile = json.loads(Path(args.profile).read_text())
        recording = synthetic_recording(schedule, profile["rectangles"])
    else:
        if not args.input:
            parser.error("--input is required unless --synthetic is used")
        recording = geo.load_jsonl(args.input)

    report, table = analyse(recording, schedule)
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    (out / "aspect.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    render(table, out / "aspect-map.svg",
           "Lit fraction per window  ({} beacons, {} samples)".format(
               report["beacons"], report["samples"]))
    write_summary(report, table, out / "summary.md")

    ratios = sorted(table, reverse=True)
    print("beacons {} / {}, samples {}, refs {}".format(
        report["beacons"], report["cycles"], report["samples"], report["referenceSamples"]))
    print("ratio  " + " ".join("r%-4d" % i for i in range(16)))
    for ratio in ratios:
        print("%5.2f  " % ratio + " ".join("%-5.2f" % v for v in table[ratio]))
    print()
    for row in report["roles"]:
        print("r%-2d at widest %.2f  narrowest %.2f  %s" % (
            row["region"], row["atWidest"], row["atNarrowest"], ", ".join(row["roles"])))
    print(out / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
