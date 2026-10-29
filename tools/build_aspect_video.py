"""Build the aspect-ratio sweep video and its schedule.

A centred white rectangle of a known aspect ratio on black. For a window that
sits partly in the black bar, the measured mean is exactly the lit fraction of
that window, so a sweep of ratios measures every window's bar-side edge: the
ratio at which a window goes black is the ratio at which the bar reaches it.

That turns "is r14 a black-bar detector?" into a number.

The schedule keeps the block-scan shape (`cycles`, `blockOrder`, `blocksStart`)
so `tools/geometry_scan.py` records it unchanged; the extra `ratio` field is what
the aspect analyser reads.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

# Wide to narrow. The last four pillarbox instead of letterbox.
DEFAULT_RATIOS = [2.76, 2.39, 2.35, 2.20, 2.00, 1.85, 16 / 9, 4 / 3, 1.0, 9 / 16]
DEFAULT_WIDTH = 1920
DEFAULT_HEIGHT = 1080
DEFAULT_FPS = 30
DEFAULT_SEGMENT = 3.0
DEFAULT_CYCLES = 2
DEFAULT_BLACK = 2.0
DEFAULT_WHITE = 2.0


def aspect_rect(ratio, width=DEFAULT_WIDTH, height=DEFAULT_HEIGHT):
    """Centred rectangle of the given display aspect ratio, in pixels."""
    if ratio <= 0:
        raise ValueError("aspect ratio must be positive")
    panel = width / height
    if ratio >= panel:
        w = width
        h = int(round(width / ratio))
    else:
        h = height
        w = int(round(height * ratio))
    w = max(2, min(w, width))
    h = max(2, min(h, height))
    return (width - w) // 2, (height - h) // 2, w, h


def build_schedule(ratios=None, width=DEFAULT_WIDTH, height=DEFAULT_HEIGHT, fps=DEFAULT_FPS,
                   segment=DEFAULT_SEGMENT, cycles=DEFAULT_CYCLES, black=DEFAULT_BLACK,
                   white=DEFAULT_WHITE):
    ratios = list(ratios or DEFAULT_RATIOS)
    if not ratios:
        raise ValueError("at least one ratio is required")
    if segment <= 0 or black <= 0 or white <= 0:
        raise ValueError("segment and beacon durations must be positive")
    if cycles < 1:
        raise ValueError("at least one cycle is required")
    header = round(black + white, 3)
    cycle_seconds = round(header + len(ratios) * segment, 3)
    segments, cycle_rows, order = [], [], []
    for index in range(cycles):
        base = round(index * cycle_seconds, 3)
        white_start = round(base + black, 3)
        content_start = round(white_start + white, 3)
        segments.append({"cycle": index, "kind": "black", "start": base, "end": white_start})
        segments.append({"cycle": index, "kind": "white", "start": white_start,
                         "end": content_start})
        for i, ratio in enumerate(ratios):
            x, y, w, h = aspect_rect(ratio, width, height)
            start = round(content_start + i * segment, 3)
            segments.append({"cycle": index, "kind": "block", "ratio": round(ratio, 4),
                             "x": x, "y": y, "w": w, "h": h,
                             "start": start, "end": round(start + segment, 3)})
            if index == 0:
                order.append({"index": i, "ratio": round(ratio, 4),
                              "x": x, "y": y, "w": w, "h": h})
        cycle_rows.append({"index": index, "start": base, "whiteStart": white_start,
                           "blocksStart": content_start, "end": round(base + cycle_seconds, 3)})
    return {
        "schema": 2,
        "kind": "aspects",
        "width": width,
        "height": height,
        "fps": fps,
        "segmentSeconds": segment,
        "beaconSeconds": {"black": black, "white": white},
        "cycleSeconds": cycle_seconds,
        "durationSeconds": round(cycles * cycle_seconds, 3),
        "blockOrder": order,
        "cycles": cycle_rows,
        "segments": segments,
    }


def _window(segment):
    return "gte(t,{:.3f})*lt(t,{:.3f})".format(segment["start"], segment["end"])


def filter_graph(schedule):
    width, height = schedule["width"], schedule["height"]
    filters = []
    white = [s for s in schedule["segments"] if s["kind"] == "white"]
    if white:
        filters.append("drawbox=x=0:y=0:w={}:h={}:color=white:t=fill:enable='{}'".format(
            width, height, "+".join(_window(s) for s in white)))
    windows = {}
    for segment in schedule["segments"]:
        if segment["kind"] == "block":
            windows.setdefault(segment["index"] if "index" in segment else
                               (segment["x"], segment["y"], segment["w"], segment["h"]),
                               []).append(segment)
    for key in windows:
        first = windows[key][0]
        filters.append("drawbox=x={}:y={}:w={}:h={}:color=white:t=fill:enable='{}'".format(
            first["x"], first["y"], first["w"], first["h"],
            "+".join(_window(s) for s in windows[key])))
    return filters


def ffmpeg_command(schedule, output, ffmpeg="ffmpeg"):
    source = "color=black:s={}x{}:r={}:d={}".format(
        schedule["width"], schedule["height"], schedule["fps"], schedule["durationSeconds"])
    return [
        ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", source,
        "-vf", ",".join(filter_graph(schedule)),
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "16",
        "-pix_fmt", "yuv420p", "-profile:v", "main", "-level", "4.0",
        "-g", "30", "-an", "-movflags", "+faststart",
        str(output),
    ]


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ratios", default="",
                        help="comma separated; default is the built-in sweep")
    parser.add_argument("--segment-seconds", type=float, default=DEFAULT_SEGMENT)
    parser.add_argument("--cycles", type=int, default=DEFAULT_CYCLES)
    parser.add_argument("--output", default="video-tools/aspect.mp4")
    parser.add_argument("--schedule", default="video-tools/aspect.json")
    parser.add_argument("--schedule-only", action="store_true")
    args = parser.parse_args()
    ratios = [float(part) for part in args.ratios.split(",") if part.strip()] or None
    schedule = build_schedule(ratios=ratios, segment=args.segment_seconds, cycles=args.cycles)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    document = dict(schedule)
    document["video"] = output.name
    if not args.schedule_only:
        subprocess.run(ffmpeg_command(schedule, output, args.ffmpeg), check=True)
        document["videoSha256"] = file_sha256(output)
    Path(args.schedule).parent.mkdir(parents=True, exist_ok=True)
    Path(args.schedule).write_text(json.dumps(document, indent=1) + "\n")
    print(output)
    print(args.schedule)
    print("duration {:.1f} s, {} ratios, {} cycles".format(
        schedule["durationSeconds"], len(schedule["blockOrder"]), args.cycles))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
