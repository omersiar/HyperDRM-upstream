"""Build the region-geometry scan video and its schedule.

One white block at a time on black. The grid tiles the frame exactly: the picture
is split into `columns` x `rows` blocks without gaps or overlap, so every pixel is
lit by exactly one segment and a region's response sums to its own area.

Each cycle opens with a full-black and a full-white segment. The white one is the
only stimulus that lifts all sixteen LG histogram regions at once, which is what
lets the recorder find the cycle start in the recorded series instead of trusting
a wall clock.

The schedule JSON is the single source of truth for the video, the recorder and
the analyser: geometry, order and timing are read from it, never recomputed.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

DEFAULT_COLUMNS = 16
DEFAULT_ROWS = 9
DEFAULT_WIDTH = 1920
DEFAULT_HEIGHT = 1080
DEFAULT_FPS = 30
DEFAULT_SEGMENT = 1.5
DEFAULT_CYCLES = 2
DEFAULT_BLACK = 2.0
DEFAULT_WHITE = 2.0


def block_grid(columns, rows, width, height):
    """Return the blocks in row-major order, covering [0,width] x [0,height]."""
    if columns < 1 or rows < 1:
        raise ValueError("the grid needs at least one column and one row")
    if width < columns or height < rows:
        raise ValueError("the grid is finer than the frame")
    blocks = []
    for gy in range(rows):
        y0 = round(gy * height / rows)
        y1 = round((gy + 1) * height / rows)
        for gx in range(columns):
            x0 = round(gx * width / columns)
            x1 = round((gx + 1) * width / columns)
            blocks.append(
                {"gx": gx, "gy": gy, "x": x0, "y": y0, "w": x1 - x0, "h": y1 - y0}
            )
    return blocks


def build_schedule(
    columns=DEFAULT_COLUMNS,
    rows=DEFAULT_ROWS,
    width=DEFAULT_WIDTH,
    height=DEFAULT_HEIGHT,
    fps=DEFAULT_FPS,
    segment=DEFAULT_SEGMENT,
    cycles=DEFAULT_CYCLES,
    black=DEFAULT_BLACK,
    white=DEFAULT_WHITE,
):
    """Describe every segment of the scan, in playback order."""
    if segment <= 0 or black <= 0 or white <= 0:
        raise ValueError("segment and beacon durations must be positive")
    if cycles < 1:
        raise ValueError("at least one cycle is required")
    if fps < 1:
        raise ValueError("fps must be positive")
    blocks = block_grid(columns, rows, width, height)
    header = round(black + white, 3)
    cycle_seconds = round(header + len(blocks) * segment, 3)
    segments = []
    cycle_rows = []
    for index in range(cycles):
        base = round(index * cycle_seconds, 3)
        black_start = base
        white_start = round(base + black, 3)
        blocks_start = round(white_start + white, 3)
        segments.append({"cycle": index, "kind": "black", "start": black_start, "end": white_start})
        segments.append(
            {"cycle": index, "kind": "white", "start": white_start, "end": blocks_start}
        )
        for i, block in enumerate(blocks):
            start = round(blocks_start + i * segment, 3)
            row = {"cycle": index, "kind": "block", "start": start,
                   "end": round(start + segment, 3)}
            row.update(block)
            segments.append(row)
        cycle_rows.append(
            {"index": index, "start": round(base, 3), "whiteStart": white_start,
             "blocksStart": blocks_start, "end": round(base + cycle_seconds, 3)}
        )
    return {
        "schema": 1,
        "columns": columns,
        "rows": rows,
        "width": width,
        "height": height,
        "fps": fps,
        "segmentSeconds": segment,
        "beaconSeconds": {"black": black, "white": white},
        "cycleSeconds": cycle_seconds,
        "durationSeconds": round(cycles * cycle_seconds, 3),
        "blockOrder": [
            {"gx": b["gx"], "gy": b["gy"], "x": b["x"], "y": b["y"], "w": b["w"], "h": b["h"]}
            for b in blocks
        ],
        "cycles": cycle_rows,
        "segments": segments,
    }


def _window(segment):
    return "gte(t,{:.3f})*lt(t,{:.3f})".format(segment["start"], segment["end"])


def _union(segments):
    """One enable expression covering every listed window."""
    return "+".join(_window(segment) for segment in segments)


def filter_graph(schedule):
    """One drawbox per distinct stimulus, enabled only inside its windows.

    Repeated cycles reuse the same drawbox with an extra window instead of a
    second filter, so the graph stays at 1 + columns*rows entries regardless of
    the cycle count.
    """
    width, height = schedule["width"], schedule["height"]
    filters = []
    white = [s for s in schedule["segments"] if s["kind"] == "white"]
    if white:
        filters.append(
            "drawbox=x=0:y=0:w={}:h={}:color=white:t=fill:enable='{}'".format(
                width, height, _union(white)
            )
        )
    ordered = schedule.get("blockOrder") or []
    windows = {}
    for segment in schedule["segments"]:
        if segment["kind"] == "block":
            windows.setdefault((segment["gy"], segment["gx"]), []).append(segment)
    for block in ordered:
        key = (block["gy"], block["gx"])
        filters.append(
            "drawbox=x={}:y={}:w={}:h={}:color=white:t=fill:enable='{}'".format(
                block["x"], block["y"], block["w"], block["h"], _union(windows[key])
            )
        )
    return filters


def ffmpeg_command(schedule, output, ffmpeg="ffmpeg"):
    source = "color=black:s={}x{}:r={}:d={}".format(
        schedule["width"], schedule["height"], schedule["fps"], schedule["durationSeconds"]
    )
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


def write_schedule(path, schedule, video_name, video_sha256=None):
    document = dict(schedule)
    document["video"] = video_name
    if video_sha256:
        document["videoSha256"] = video_sha256
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=1) + "\n")
    return path


def load_schedule(path):
    return json.loads(Path(path).read_text())


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--columns", type=int, default=DEFAULT_COLUMNS)
    parser.add_argument("--rows", type=int, default=DEFAULT_ROWS)
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS)
    parser.add_argument("--segment-seconds", type=float, default=DEFAULT_SEGMENT)
    parser.add_argument("--cycles", type=int, default=DEFAULT_CYCLES)
    parser.add_argument("--black-seconds", type=float, default=DEFAULT_BLACK)
    parser.add_argument("--white-seconds", type=float, default=DEFAULT_WHITE)
    parser.add_argument("--output", default="video-tools/pattern.mp4")
    parser.add_argument("--schedule", default="video-tools/pattern.json")
    parser.add_argument("--schedule-only", action="store_true",
                        help="write the schedule without encoding the video")
    args = parser.parse_args()

    schedule = build_schedule(
        columns=args.columns, rows=args.rows, width=args.width, height=args.height,
        fps=args.fps, segment=args.segment_seconds, cycles=args.cycles,
        black=args.black_seconds, white=args.white_seconds,
    )
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if args.schedule_only:
        write_schedule(args.schedule, schedule, output.name)
        print(args.schedule)
        return 0
    subprocess.run(ffmpeg_command(schedule, output, args.ffmpeg), check=True)
    write_schedule(args.schedule, schedule, output.name, file_sha256(output))
    print(output)
    print(args.schedule)
    print("duration {:.1f} s, {} blocks, {} cycles".format(
        schedule["durationSeconds"], len(schedule["blockOrder"]), args.cycles))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
