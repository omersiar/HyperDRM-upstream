"""Record the sixteen LG histogram regions while a pattern video plays.

Uploads the generated stimulus into the installed app, launches it full screen
on the video plane, and polls two luna methods over one SSH session:

* `status` several times per second - the sixteen region means, cheap
* `histograms` about once per segment - the raw 256-bin counts, 33 KB

Nothing here interprets the data: the recording keeps wall-clock order and the
analyser aligns it with the schedule through the full-white beacon.

The service must already be capturing (`running` and `activeSource` HyperDRM).
Capture is started before the video is launched, so the black and white beacon
at the head of the file is sampled from its first second; LG revokes capture
when the app's renderer dies, so `--restart-app` runs first and this afterwards.

`--restart-app` kills the WebAppMgr renderer, which is needed once after a new
`app/wizard.js` is copied to the TV and also guarantees that the launch
parameters reach the app instead of a relaunch with stale ones.
"""
import argparse
import json
import math
import shlex
import subprocess
import sys
import time
from pathlib import Path

APP_ID = "org.webosbrew.histogramhyperion"
SERVICE_ID = "org.webosbrew.histogramhyperion.service"
DEFAULT_APP_DIR = (
    "/media/developer/apps/usr/palm/applications/org.webosbrew.histogramhyperion"
)

REMOTE_SCRIPT = r"""
set -u
interval=$1
bins_every=$2
duration=$3
source_name=$4
launch=$(luna-send -i -w 8000 -n 1 luna://com.webos.applicationManager/launch \
  "{\"id\":\"org.webosbrew.histogramhyperion\",\"params\":{\"action\":\"play\",\"src\":\"$source_name\"}}")
printf 'L %s\n' "$launch"
started=$(date +%s)
i=0
while :; do
  printf 'S '
  luna-send -i -w 5000 -n 1 luna://org.webosbrew.histogramhyperion.service/status '{}'
  if [ "$bins_every" -gt 0 ] && [ $((i % bins_every)) -eq 0 ]; then
    printf 'B '
    luna-send -i -w 5000 -n 1 luna://org.webosbrew.histogramhyperion.service/histograms '{}'
  fi
  i=$((i + 1))
  now=$(date +%s)
  if [ $((now - started)) -ge "$duration" ]; then break; fi
  sleep "$interval"
done
"""

RESTART_SCRIPT = r"""
for p in /proc/[0-9]*; do
  grep -q -- '--[a]pp-id=org.webosbrew.histogramhyperion' "$p/cmdline" 2>/dev/null && \
    kill "${p#/proc/}" 2>/dev/null
done
exit 0
"""


def ssh_base(tv):
    return ["ssh", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "--", tv]


def last_json(text):
    for line in reversed(text.strip().splitlines()):
        line = line.strip()
        if line.startswith("{"):
            try:
                return json.loads(line)
            except ValueError:
                continue
    return None


def remote_status(tv):
    command = "luna-send -i -w 5000 -n 1 luna://{}/status '{{}}'".format(SERVICE_ID)
    result = subprocess.run(ssh_base(tv) + [command], capture_output=True, text=True)
    return last_json(result.stdout)


def remote_method(tv, method, payload="{}"):
    command = "luna-send -i -w 5000 -n 1 luna://{}/{} {}".format(
        SERVICE_ID, method, shlex.quote(payload))
    subprocess.run(ssh_base(tv) + [command], capture_output=True, text=True)


def wait_stopped(tv, seconds=12.0):
    """Wait for LG to revoke capture after the renderer dies.

    Without this, a status reply that still says `running:true` lets the scan
    launch the video against a service that is about to lose its handle.
    """
    deadline = time.monotonic() + seconds
    status = remote_status(tv)
    while time.monotonic() < deadline and status and status.get("running"):
        time.sleep(0.5)
        status = remote_status(tv)
    return status


def ensure_capture(tv, wait_seconds=25.0):
    """Start the service and wait for a valid sample.

    Capture works while the app is closed, which is what makes the launch order
    simple: start capture first, then launch the video, so the beacon at the
    head of the file is already being sampled. LG revokes capture when the
    app's renderer dies, so this must run after `--restart-app`, not before.
    """
    status = remote_status(tv)
    if status and status.get("running") and status.get("colorValid"):
        return status
    print("capture: starting the service", file=sys.stderr)
    remote_method(tv, "start")
    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        time.sleep(1.5)
        status = remote_status(tv)
        if status and status.get("running") and status.get("colorValid"):
            return status
    state = (status or {}).get("captureState")
    error = (status or {}).get("error")
    raise SystemExit("capture did not start: {} ({})".format(state, error))


def validate(status, allow_active_mean=False):
    if status.get("activeSource") != "HyperDRM":
        raise SystemExit("Capture source is {!r}, not HyperDRM. Stop PicCap handover first.".format(
            status.get("activeSource")))
    if status.get("customActiveMean") and not allow_active_mean:
        raise SystemExit("customActiveMean is on: the region mean would report the lit pixels "
                         "instead of their share. Turn it off, or pass --allow-active-mean.")
    if status.get("customCompensate"):
        print("warning: customCompensate is on; coverage is normalised against the measured "
              "references, so this cancels, but the black/white segments must be present",
              file=sys.stderr)
    return status


def upload(tv, video, app_dir, name):
    target = "{}:{}/{}".format(tv, app_dir, name)
    subprocess.run(["scp", "-q", str(video), target], check=True)


def restart_app(tv):
    subprocess.run(ssh_base(tv) + [RESTART_SCRIPT], check=False)


def record(tv, source_name, duration, interval_ms, bins_every):
    command = (
        "stty -echo; exec sh -c "
        + shlex.quote(REMOTE_SCRIPT)
        + " capture {:.3f} {} {}".format(interval_ms / 1000.0, bins_every, duration)
        + " " + shlex.quote(source_name)
    )
    return subprocess.Popen(
        ["ssh", "-tt", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "--", tv, command],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=None,
        text=True,
        bufsize=1,
    )


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tv", default="root@192.168.2.14")
    parser.add_argument("--schedule", default="video-tools/pattern.json")
    parser.add_argument("--video", default="video-tools/pattern.mp4")
    parser.add_argument("--output", default="video-tools/geometry-scan.jsonl")
    parser.add_argument("--app-dir", default=DEFAULT_APP_DIR)
    parser.add_argument("--interval-ms", type=int, default=100)
    parser.add_argument("--bins-every", type=int, default=4,
                        help="histogram poll every N status polls; 0 disables bins")
    parser.add_argument("--margin-seconds", type=float, default=6.0)
    parser.add_argument("--skip-upload", action="store_true")
    parser.add_argument("--restart-app", action="store_true",
                        help="kill the WebAppMgr renderer first: loads a newly copied "
                             "wizard.js and makes the launch parameters authoritative")
    parser.add_argument("--allow-active-mean", action="store_true")
    parser.add_argument("--dry-run", action="store_true",
                        help="preflight only, do not launch or record")
    args = parser.parse_args()

    schedule = json.loads(Path(args.schedule).read_text())
    source_name = Path(args.video).name
    duration = int(math.ceil(schedule["durationSeconds"] + args.margin_seconds))

    if args.skip_upload:
        print("upload: skipped", file=sys.stderr)
    else:
        upload(args.tv, args.video, args.app_dir, source_name)
        print("upload: {} -> {}/{}".format(args.video, args.app_dir, source_name), file=sys.stderr)
    if args.restart_app:
        print("app: restarting renderer", file=sys.stderr)
        restart_app(args.tv)
        wait_stopped(args.tv)
    status = ensure_capture(args.tv)
    validate(status, args.allow_active_mean)
    print("service: {} source {}, {} ms, model {}".format(
        status.get("captureState"), status.get("activeSource"),
        status.get("captureIntervalMs"), status.get("tvModel") or "unknown"), file=sys.stderr)
    print("video: {} s + {} s margin = {} s, {} blocks, {} cycles".format(
        schedule["durationSeconds"], args.margin_seconds, duration,
        len(schedule["blockOrder"]), len(schedule["cycles"])), file=sys.stderr)
    if args.dry_run:
        return 0

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    handle = open(output, "w", encoding="utf-8")

    def emit(row):
        handle.write(json.dumps(row, separators=(",", ":")) + "\n")

    emit({
        "type": "capture",
        "schema": 1,
        "tv": args.tv,
        "appId": APP_ID,
        "serviceId": SERVICE_ID,
        "startedAt": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "schedule": str(args.schedule),
        "video": source_name,
        "videoSha256": schedule.get("videoSha256"),
        "durationSeconds": schedule["durationSeconds"],
        "intervalMs": args.interval_ms,
        "blocks": len(schedule["blockOrder"]),
        "cycles": len(schedule["cycles"]),
        "tvModel": status.get("tvModel", ""),
        "customActiveMean": status.get("customActiveMean"),
        "customCompensate": status.get("customCompensate"),
        "spatialGeometry": status.get("spatialGeometry"),
        "captureIntervalMs": status.get("captureIntervalMs"),
        "note": "Region means and raw bins are service values; the analyser aligns them "
                "with the schedule through the full-white beacon.",
    })

    process = record(args.tv, source_name, duration, args.interval_ms, args.bins_every)
    started = time.monotonic()
    origin = None
    samples = histograms = skipped = 0
    beacon_seen = False
    warned = False
    try:
        for raw in process.stdout:
            line = raw.strip()
            if not line:
                continue
            tag, _, body = line.partition(" ")
            body = body.strip()
            if tag == "L":
                origin = origin if origin is not None else time.monotonic()
                print("launch: {}".format(body[:160]), file=sys.stderr)
                continue
            if tag not in ("S", "B") or not body.startswith("{"):
                skipped += 1
                continue
            try:
                reply = json.loads(body)
            except ValueError:
                skipped += 1
                continue
            if origin is None:
                origin = time.monotonic()
            moment = round(time.monotonic() - origin, 4)
            if tag == "S":
                regions = reply.get("regions")
                if not isinstance(regions, list) or len(regions) != 16:
                    skipped += 1
                    continue
                emit({
                    "type": "sample",
                    "t": moment,
                    "seq": reply.get("sampleSequence"),
                    "ageMs": reply.get("sampleAgeMs"),
                    "regions": regions,
                    "blackPercent": reply.get("blackPercent"),
                })
                samples += 1
                if not beacon_seen:
                    beacon_seen = min(max(channel) for channel in regions) >= 240
                    if beacon_seen:
                        print("beacon: first full-white frame at {:.1f} s".format(moment),
                              file=sys.stderr)
                if not beacon_seen and not warned and moment > 30:
                    warned = True
                    print("warning: no full-white beacon after 30 s. If the screen stayed on the "
                          "app UI, the renderer is stale: rerun with --restart-app.",
                          file=sys.stderr)
            else:
                bins = reply.get("bins")
                if not isinstance(bins, str):
                    skipped += 1
                    continue
                emit({
                    "type": "histograms",
                    "t": moment,
                    "seq": reply.get("sampleSequence"),
                    "bins": bins,
                })
                histograms += 1
            if (samples + histograms) % 100 == 0:
                print("\r{} samples, {} histogram frames".format(samples, histograms),
                      end="", file=sys.stderr, flush=True)
        process.wait()
    except KeyboardInterrupt:
        process.terminate()
    finally:
        handle.write(json.dumps({
            "type": "summary",
            "elapsedSeconds": round(time.monotonic() - started, 3),
            "samples": samples,
            "histogramFrames": histograms,
            "skippedLines": skipped,
            "exitCode": process.returncode,
        }, separators=(",", ":")) + "\n")
        handle.close()

    print("\nwrote {}: {} samples, {} histogram frames, {} skipped lines".format(
        output, samples, histograms, skipped), file=sys.stderr)
    if not beacon_seen:
        print("no beacon found; the analyser cannot align this recording", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
