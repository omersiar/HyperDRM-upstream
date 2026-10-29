#!/usr/bin/env python3
"""Launch TV calibration patterns and record fresh histogram-service samples."""

import argparse
import datetime as dt
import json
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

APP_ID = "org.webosbrew.histogramhyperion"
SERVICE_ID = "org.webosbrew.histogramhyperion.service"
REMOTE_SCRIPT = r"""set -eu
interval=$1
duration=$2
launch=$(luna-send -i -w 8000 -n 1 luna://com.webos.applicationManager/launch \
  '{"id":"org.webosbrew.histogramhyperion","params":{"action":"calibrate"}}')
printf '%s\n' "$launch"
started=$(date +%s)
seen_calibration=0
while :; do
  status=$(luna-send -i -w 5000 -n 1 luna://org.webosbrew.histogramhyperion.service/status '{}')
  printf '%s\n' "$status"
  case "$status" in
    *'"calibrating":true'*) seen_calibration=1 ;;
  esac
  if [ "$seen_calibration" -eq 1 ]; then
    case "$status" in
      *'"calibrating":false'*) break ;;
    esac
  fi
  now=$(date +%s)
  if [ "$((now - started))" -ge "$duration" ]; then
    break
  fi
  sleep "$interval"
done
"""


def pattern_phase(seconds):
    if seconds < 4:
        return "black-baseline"
    if seconds < 68:
        return f"edge-patch-{int((seconds - 4) // 2) + 1:02d}"
    if seconds < 70:
        return "black-reference"
    if seconds < 72:
        return "white-reference"
    if seconds < 90:
        return f"vertical-split-{int((seconds - 72) // 2) + 1:02d}"
    if seconds < 108:
        return f"horizontal-split-{int((seconds - 90) // 2) + 1:02d}"
    if seconds < 110:
        return "calibration-completion-boundary"
    if seconds < 116:
        return "color-quadrant-validation"
    return "finishing"


def parse_response(line):
    try:
        value = json.loads(line)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"TV returned non-JSON Luna output: {line[:240]!r}") from error
    if not isinstance(value, dict):
        raise RuntimeError("TV returned a Luna response that is not a JSON object.")
    if value.get("returnValue") is False:
        raise RuntimeError(f"TV Luna request failed: {value.get('errorText', value)!s}")
    return value


def valid_regions(value):
    def valid_channel(channel):
        return isinstance(channel, int) and not isinstance(channel, bool) and 0 <= channel <= 255

    return (
        isinstance(value, list)
        and len(value) == 16
        and all(
            isinstance(rgb, list)
            and len(rgb) == 3
            and all(valid_channel(channel) for channel in rgb)
            for rgb in value
        )
    )


def valid_black_percent(value):
    def valid_percent(percent):
        return isinstance(percent, int) and not isinstance(percent, bool) and 0 <= percent <= 100

    return (
        isinstance(value, list)
        and len(value) == 16
        and all(valid_percent(percent) for percent in value)
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tv",
        default="root@192.168.2.14",
        help="SSH destination (default: root@192.168.2.14)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="JSONL output path (default: timestamped file in the current directory)",
    )
    parser.add_argument(
        "--interval-ms",
        type=int,
        default=250,
        help="status poll interval, 100-2000 ms (default: 250)",
    )
    parser.add_argument(
        "--timeout", type=int, default=180, help="maximum run time in seconds (default: 180)"
    )
    parser.add_argument("--force", action="store_true", help="overwrite an existing output file")
    args = parser.parse_args()

    if not re.fullmatch(r"[A-Za-z0-9_.@:-]+", args.tv):
        parser.error("--tv may contain only a username and host name or IP address")
    if not 100 <= args.interval_ms <= 2000:
        parser.error("--interval-ms must be between 100 and 2000")
    if not 30 <= args.timeout <= 300:
        parser.error("--timeout must be between 30 and 300")

    output = args.output or Path(
        f"tv-histogram-{dt.datetime.now().strftime('%Y%m%d-%H%M%S')}.jsonl"
    )
    if output.exists() and not args.force:
        parser.error(f"{output} already exists; choose another --output or use --force")
    try:
        with output.open("w", encoding="utf-8"):
            pass
    except OSError as error:
        raise RuntimeError(f"Cannot create output file {output}: {error}") from error

    interval = f"{args.interval_ms / 1000:.3f}"
    remote_command = (
        "stty -echo; exec sh -c "
        + shlex.quote(REMOTE_SCRIPT)
        + f" capture {interval} {args.timeout}"
    )
    process = subprocess.Popen(
        [
            "ssh",
            "-tt",
            "-q",
            "-o",
            "BatchMode=yes",
            "-o",
            "ConnectTimeout=8",
            "--",
            args.tv,
            remote_command,
        ],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=None,
        text=True,
        bufsize=1,
    )
    if process.stdout is None:
        process.terminate()
        process.wait()
        raise RuntimeError("Could not open the SSH capture streams.")

    started_at = dt.datetime.now(dt.timezone.utc)
    first_calibration_at = None
    last_sequence = None
    sample_count = 0
    saw_calibration = False

    try:
        with output.open("w", encoding="utf-8") as capture:
            metadata = {
                "type": "capture",
                "schema": 1,
                "tv": args.tv,
                "appId": APP_ID,
                "serviceId": SERVICE_ID,
                "startedAt": started_at.isoformat(),
                "intervalMs": args.interval_ms,
                "patterns": "HyperDRM Auto Calibration video",
                "note": (
                    "Regions and blackPercent are live service status values; "
                    "profile changes are not saved."
                ),
            }
            capture.write(json.dumps(metadata, separators=(",", ":")) + "\n")
            for line in process.stdout:
                response = parse_response(line)
                if "sampleSequence" not in response:
                    print("TV app launch accepted.", file=sys.stderr)
                    continue
                if response.get("calibrating"):
                    saw_calibration = True
                    if first_calibration_at is None:
                        first_calibration_at = time.monotonic()
                elif saw_calibration:
                    break
                regions = response.get("regions")
                black_percent = response.get("blackPercent")
                sequence = response.get("sampleSequence")
                if (
                    first_calibration_at is None
                    or not response.get("colorValid")
                    or not valid_regions(regions)
                    or not valid_black_percent(black_percent)
                    or not isinstance(sequence, int)
                    or isinstance(sequence, bool)
                    or sequence == last_sequence
                ):
                    continue
                elapsed = time.monotonic() - first_calibration_at
                row = {
                    "type": "sample",
                    "capturedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
                    "elapsedSeconds": round(elapsed, 3),
                    "patternPhase": pattern_phase(elapsed),
                    "patternTimelineApproximate": True,
                    "tvModel": response.get("tvModel", ""),
                    "sampleSequence": sequence,
                    "sampleAgeMs": response.get("sampleAgeMs"),
                    "captureState": response.get("captureState"),
                    "activeSource": response.get("activeSource"),
                    "regions": regions,
                    "blackPercent": black_percent,
                }
                capture.write(json.dumps(row, separators=(",", ":")) + "\n")
                capture.flush()
                last_sequence = sequence
                sample_count += 1
                print(
                    f"\rCaptured {sample_count} fresh samples; "
                    f"phase={row['patternPhase']} model={row['tvModel'] or 'unknown'}",
                    end="",
                    file=sys.stderr,
                    flush=True,
                )
            exit_code = process.wait()
    except KeyboardInterrupt:
        process.terminate()
        process.wait()
        print("\nCapture interrupted.", file=sys.stderr)
        return 130
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait()

    print(file=sys.stderr)
    if exit_code != 0:
        raise RuntimeError(f"SSH/Luna capture failed with exit status {exit_code}.")
    if not saw_calibration:
        raise RuntimeError(
            "The TV never reported an active calibration. Check SSH/root access, "
            "the installed app/service, and the TV's Luna launch response."
        )
    if not sample_count:
        raise RuntimeError("Calibration ran, but no fresh 16-region samples were captured.")

    summary = {
        "type": "summary",
        "completedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
        "sampleCount": sample_count,
        "tvModel": response.get("tvModel", ""),
        "captureState": response.get("captureState"),
        "running": response.get("running"),
        "colorValid": response.get("colorValid"),
        "error": response.get("error"),
        "retryScheduled": response.get("retryScheduled"),
        "retryAttempts": response.get("retryAttempts"),
        "captureRecoveries": response.get("captureRecoveries"),
        "lastRecoveryMs": response.get("lastRecoveryMs"),
        "calibrationMessage": response.get("calibrationMessage"),
    }
    draft = response.get("calibrationDraft")
    if isinstance(draft, dict) and draft.get("model"):
        summary["calibrationDraft"] = draft

    with output.open("a", encoding="utf-8") as capture:
        capture.write(json.dumps(summary, separators=(",", ":")) + "\n")
    print(f"Saved {sample_count} samples to {output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
