# Runtime boundaries

The native service remains one translation unit because LG symbols are resolved
at runtime. Its implementation headers share the context declared in service.c;
they are included once in dependency order, not independent public libraries.
Formatting must preserve include order (`SortIncludes: Never`).

| Module | Responsibility |
| --- | --- |
| service.c | Runtime context, LG/Luna loading, registration and shutdown |
| service_api.h | Start/stop, root restart, settings and autostart actions |
| service_calibration.h | Bounded calibration lease, proposals, profiles, model discovery |
| service_status.h | Read-only status serialization |
| capture_loop.h | Scheduler, histogram acquisition, missing-data recovery and output |
| layout.h | Window geometry: rectangle selection, letterbox projection, rim interpolation |
| edge_feed.h | Builds the output plane by copying measurements, never blending them |
| scene_cut.h | Detects a shot change from the histogram distance, before the filter smears it |
| color_grade.h | Brightness and colour in OKLab, with a gamut map, after the measurement |
| reconstruct.h | Opt-in smooth renderer: solves the measurements for a coarse grid and interpolates it |
| reconstruct_table.h | Generated 576x16 fixed-point map from the sixteen measurements to that grid |
| frame_output.h | Transport selection between the JSON and FlatBuffer output paths |
| receiver_output.h | Main asynchronous connection, transaction matching and frame coalescing |
| flatbuffer.h | Back-to-front FlatBuffers writer for HyperHDR's Request schema |
| flatbuffer_output.h | Length-prefixed FlatBuffer connection, registration, coalescing and clear |
| atomic_file.h | Durable preference replacement and explicit commit outcomes |
| service_context.h / runtime_context.h | Capture, receiver/layout and calibration session ownership |
| receiver_stream.h | Independent, nonblocking receiver LED subscription |
| source_bridge.h | Bounded color continuity during source transitions |
| handover.h | Exclusive PicCap/histogram capture ownership |

Connection stages are explicitly named in runtime_states.h:
Disconnected → Connecting → Authenticating (when configured) → Layout → Subscribed.
Any connection, authorization or parsing failure closes the connection and
schedules a bounded retry. Up to eight buffered messages are consumed per tick,
including ticks where no new bytes arrive. Buffer capacity and read budgets remain
bounded. LED count/profile changes trigger a fresh layout subscription.

Output transport is chosen per target in target.h. Hyperion and HyperHDR with
`output=json` use receiver_output.h: Idle → Connecting → (Login) → Layout →
Ready, one in-flight transaction matched by `tan`, and one replaceable color
frame. HyperHDR with `output=flatbuffer` uses flatbuffer_output.h instead:
Connecting → Registering → Ready, length-prefixed big-endian frames, a Register
request carrying origin `HyperDRM` and priority 149, then one RawImage plane per
sample. The receiver decodes that plane once and broadcasts it through
`GlobalSignals::SignalSetGlobalImage`, which every running LED hardware instance
consumes, so one connection reaches all instances. The JSON API cannot: it binds
an instance to a connection and rejects a per-command `instance` field. Reply
error strings are parsed; a non-empty error fails the output. Disconnect clears
the priority server side, and a stop still sends an explicit Clear.

The color bridge uses Idle → Holding → WaitingFrame → Fading → Idle. It holds the
last histogram frame while PicCap starts, clears its priority before observing a
fresh PicCap frame, then blends for 500 ms. The transition expires after 3 seconds;
real black bypasses the fade. The opposite direction seeds histogram continuity
from a fresh receiver frame. Capture ownership remains in handover.h; the observer
never acquires LG capture. Stop cancels the bridge and releases owned resources.

Each region arrives as 768 counters, and the pipeline reduces them to one 8-bit
code per channel. That reduction is the only irreversible loss in the path and it
is a choice: half a screen of black beside half of white has exactly the same mean
as flat mid-grey. `calibration.color_stat` selects what to keep — `mean` (default),
`active`, `p50`, `p90` or `peak` — where `active` averages only the bins above the
black reference (via `histogram_active_linear_mean()`, for a small bright object on
a dark background) and the others use `histogram_linear_percentile()`. A percentile returns the code of
the chosen bin, so it round-trips through the encode table unchanged. Everything
downstream is then lossless with respect to those sixteen colours: `edge_feed.h`
copies a measured colour per cell and never invents one.

The statistic is a display choice and nothing else. `capture_ctx.detect_rgb` keeps
the plain mean for the letterbox probe and the r14 bar meter, because both were
calibrated against it: a `p50` reading is 0 in a dark-but-lit interior and hid
every letterbox, and a `peak` reading treats one bright pixel in the bar strip as
"no bar". Decisions therefore do not move when the feed's summary changes.

The stabiliser takes a jump of `STABILIZE_JUMP` (32) codes at once, because a cut
is orders of magnitude larger than the one-code dither the filter exists to remove;
smaller changes still have to survive the three-sample confirm. Measured on the
flash stimulus, the raw-to-output lag fell from 110 ms typical (320 ms worst) to
0-60 ms, with no loss of dither rejection.

The UI calibration session uses Starting → Waiting → Measuring/Validating →
Committing → finished. Each asynchronous callback checks its original session
identity, so a cancelled session cannot modify a newly started session. Proposals
remain unsaved until the user selects Save profile. The native lease still bounds
the session if the UI disappears.

UI action errors take precedence over transient status errors. Polling cannot
erase them; starting the next action clears them. Receiver settings, capture state,
calibration state and presentation errors remain distinct.

VERSION generates app/package metadata and native/version.h. release.py tests
portable logic locally, builds the ARM service and verifies the exact package.
Hardware capture and Magic Remote behavior still require a later TV check; a host
test is not evidence of LG hardware behavior.

The main receiver owns a ReceiverOutput context passed explicitly to begin, tick,
frame, stop and close. Its lifecycle is Idle -> Connecting -> Login (optional) ->
Layout -> Ready, with Failed on transport/protocol errors. Requests use a positive
32-bit `tan`, echoed by the receiver, to avoid acknowledging the wrong command.
Each tick has a 64 KiB send budget and a 64 KiB read budget; buffered input has an
eight-message budget and 2 MiB limit. An ACK can send the newest queued frame in
the same tick without resetting that byte budget. There is one pending command
and one replaceable frame, not an unbounded queue. Transaction deadlines are three
seconds. Shutdown and cancellation release all owned buffers and sockets.

Recovery is driven by intent, not by the autostart hook. `start` sets
`capture_ctx.wanted`, `stop` clears it, and the tick keeps retrying while that flag
is set, however long the obstacle lasts: a cast app holds VT_CAPTURE for as long as
it plays, so a bounded attempt count gave up minutes before the app released it.
Every latch that gates recovery carries a deadline (`RECOVERY_LATCH_MS`). A stalled
`starting` or `closing` latch, and a failed connection, are dropped so the retry can
actually be taken; a half-open receiver socket once left the feed dead with a retry
permanently scheduled and never taken. `status.receiverStage` and
`status.recoveryAttempts` expose that state instead of it being silent.

Feed cost lives in the plane build, so it is measured rather than assumed:
`status` reports `vtGetMs`, `renderMs`, `sendMs`, `tickMs`, `sampleGapMs`,
`outputGapMs` and both `sampleFps`/`outputFps`. Resolving the geometry per pixel
per frame cost 60-110 ms of every tick on this TV, because the ARM build is
soft-float and each rectangle test is a dozen double comparisons. `edge_feed.h`
resolves the owning measurement once per profile and bar into a cell table, so the
per-frame work is a table walk: render fell to under a millisecond and the feed went
from 12 Hz to 30 Hz at a 25 ms capture interval. `outputRegions` reports the colours
a frame actually carried, which is what makes receiver-visible latency measurable.

## Scene cuts and the temporal filter

`stabilize_filter.h` holds dither, but it must not hold a real change. Its
asymmetry follows what a viewer notices: an increase above the dither band is
accepted on the first sample, a decrease needs a second, and a step beyond
`STABILIZE_JUMP` is real light and goes out at once either way.

A cut is different in kind, and `scene_cut.h` finds it before the filter sees it.
The region means cannot see a cut -- two differently arranged shots can share a
palette -- but the distributions can: `d` is the normalised L1 between this
frame's 16x3x256 counts and the last, in permille, so 0 is identical and 1000 is
no shared mass. Above `SCENE_CUT_DISTANCE` the frame is adopted whole
(`stabilize_snap`), skipping both the confirmation and the startup median, which
would otherwise spend two frames on a colour neither shot had. `status` reports
`sceneCuts` and the last `sceneCutDistance`, so the threshold is judged against
real content rather than guessed.

The source bridge receives CaptureContext explicitly; layout refresh receives
ReceiverLayoutContext explicitly. Capture data, continuity, recovery counters and
LG handles are grouped separately from receiver transport and calibration session
lease/model-query/proposal fields. LG/Luna symbols, persisted calibration and
handover ownership still have shared service scope; this refactor reduces globals
rather than claiming complete isolation.

Transaction echo behavior is defined by the official implementations:
[Hyperion 2.0.16 JsonAPI](https://github.com/hyperion-project/hyperion.ng/blob/2.0.16/libsrc/api/JsonAPI.cpp)
and [HyperHDR HyperAPI](https://github.com/awawa-dev/HyperHDR/blob/master/sources/api/HyperAPI.cpp).
This code check does not replace a deployment test against the user's receiver.
