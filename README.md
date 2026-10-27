# HyperDRM

Ambient Light for webOS, Hyperion and HyperHDR. An own webOS app and native ARM service that reads LG color histograms and sends spatial LED colors to a configurable Hyperion or HyperHDR receiver.

![HyperDRM logo](app/icon.png)

## Features

- Spatial colors from 16 LG histogram regions, rendered either as interpolated transitions along the screen perimeter or from measured region rectangles.
- Histogram means and plane blends are computed in linear light, so a region that mixes two colors keeps the picture's brightness instead of dimming to an encoded average.
- LED count, ordering and screen rectangles read from the receiver at startup and refreshed every five seconds without capture restart. No fixed 200-LED layout; layouts with 50 and 500 LEDs were tested.
- Automatic detection of top/bottom black bars on the calibrated LG G4.
- Bounded last-color hold on empty/failed histogram reads, real black detection and 500 ms source transitions using a raw receiver LED subscription.
- Guided full-screen video calibration for additional TV models, with review before saving and a four-color validation video.
- Original saturation and brightness (both gains 1.0).
- Start, stop, status and persistent boot autostart switch in the TV app.
- Configurable IPv4/IPv6 receiver and JSON TCP port, optional token authentication, priority 149, up to 10 updates per second.

## Requirements and limits

Tested on a rooted LG G4 with webOS 24, Homebrew Channel and Hyperion 2.0.16 and HyperHDR 22.0.0 tested on the TV. The native service requires root elevation and the TV's existing `libvtcapture`, Luna, GLib and JSON-C libraries. Other TV models or firmware versions have not been validated.

DRM playback color output was confirmed on the test TV. Support depends on LG's capture implementation; this does not guarantee support on other devices or apps. HyperDRM transmits colors, not captured video frames. The video preview does not show the TV picture. HyperHDR may show a generated histogram color plane instead.

The 16 histogram samples are independent measurements; interpolation produces smoother transitions without adding independently captured detail. Sample positions come from the selected model profile; the built-in profile contains approximate G4 calibration values. Auto uses uniform average colors on uncalibrated models. Top/bottom bar detection is supported; side bars, arbitrary bar thickness and HDR color calibration remain limited.

Only the own service is modified. Optional Auto PicCap can configure its receiver endpoint and start and stop the installed PicCap service through its existing API. No Hue, LG library, driver or firmware binaries are included. For histogram initialization, the service temporarily creates LG's developer marker and uses a guardian process to remove its own marker afterward; an existing marker is preserved.

## Installation

The current package is [HyperDRM_0.13.4_arm.ipk](HyperDRM_0.13.4_arm.ipk). Install it using the TV's webOS/Homebrew package installation workflow.

Elevate the installed own service through Homebrew Channel:

```sh
luna-send -n 1 luna://org.webosbrew.hbchannel.service/elevateService '{"id":"org.webosbrew.histogramhyperion.service"}'
```

Perform elevation before first opening the app or calling its service. If the service was already started without root, elevation updates its launch permissions but does not restart the existing process: terminate only the own `histogram-service` process (verify its executable path first), then reopen the app or call start again. Closing the UI alone does not terminate the native service.

Open **HyperDRM** on the TV and select **Start**. Use the remote arrow buttons to move between controls and OK to activate the selected control. Magic Remote pointer input is also supported. The receiver's JSON TCP server must be enabled at the IP/port saved in Receiver settings (default 127.0.0.1:19444). The existing Hyperion LED device/output configuration remains responsible for controlling the hardware.

The internal app/service IDs remain `org.webosbrew.histogramhyperion` and `org.webosbrew.histogramhyperion.service` so existing installations and the autostart hook remain compatible. Hyperion displays the source as **HyperDRM**. After changing the LED layout, restart the service.

## Autostart

The app's **Autostart: On/Off** switch controls the own hook `/var/lib/webosbrew/init.d/60-histogramhyperion`. Homebrew Channel runs it during boot after service elevation. The boot hook waits until the native service accepts startup, then exits. With autostart enabled, the native service retries every five seconds without an attempt limit when startup fails or the Hyperion connection drops. It checks Hyperion before acquiring the LG capture resource. The Homebrew failsafe can suppress boot hooks.

**Stop** ends the current capture session and pauses retries until **Start** or the next service launch/boot; it leaves the saved autostart preference unchanged. The hook is a symlink to `boot.sh` inside the service container. Removal leaves a harmless dangling link; the script also exits when its service target is absent. Turning autostart off removes the own hook and cancels waiting boot attempts. A foreign file or symlink at that path will not be overwritten.

Boot log: `/var/lib/webosbrew/histogramhyperion-boot.log`.

## Build

Use an ARM EABI cross-compiler and Python 3:

```sh
arm-linux-gnueabi-gcc -Wall -Wextra -Werror -O2 -rdynamic native/service.c -ldl -lm -o service/histogram-service
python3 build_package.py
python3 build_manifest.py
```

The package builder bundles only the own app, native executable, metadata and logo assets. A built executable and current IPK are included for the tested platform.

## Tests and calibration

The native tests cover histogram decoding, black bar classification, color boost, interpolation and dynamic layout validation. Build each `native/test_*.c` for ARM and run on the TV. The layout test needs the TV's JSON-C library and linking with `-ldl`.

Example:

```sh
arm-linux-gnueabi-gcc -Wall -Wextra -Werror -O2 native/test_layout.c -ldl -o test-layout
arm-linux-gnueabi-gcc -Wall -Wextra -Werror -O2 native/test_interpolation.c -o test-interpolation
```

The tests passed on the G4. Boot-hook invocation started a stopped service; an actual cold boot has not yet been tested. A real uninstall/reinstall cycle was tested on the G4: containers and running process were removed, the dangling startup link did not execute, and fresh installation plus root elevation and service restart restored color output. Isolated ARM tests also cover autostart deactivation, legacy-hook migration, foreign-hook protection and removal of the target container.

`calibration-gradient.mp4` is the own red/green coordinate ramp used for calibration. `spatial-calibration.json` and `spatial-interpolation.json` record the calibration and example layout. The offline mapping scripts require locally obtained `calibration-samples.jsonl` and `serverinfo.json`; these diagnostic snapshots are not distributed. Runtime mapping is implemented in `native/layout.h` and reads the actual Hyperion layout.

To regenerate the test video, install `imageio-ffmpeg` in your Python environment and run `python3 make_calibration.py`.

## Logo

The logo master is `app/logo.png`, with 80 px and 130 px installation icons.

## Publishing status

See [the publishing review](docs/publishing-review.md) for the requirements audit and unresolved submission conditions. This repository is not a claim that webOSbrew has approved the app.

Remote navigation regression tests: `node tests/remote-navigation.test.js`.

## Optional automatic PicCap handover

Version 0.7.0 adds **Auto PicCap**, disabled by default. The switch is available when PicCap is installed. Keep PicCap's own autostart disabled. From 0.9.0, HyperDRM manages PicCap's receiver address, FlatBuffer port and Unix socket setting when automatic handover is enabled; other capture preferences and the autostart preference are preserved.

When the session is started, the controller polls LG's active media resources. From 0.10.0, three consecutive foreground media-decoder samples over at least two seconds without a Secure Video Path resource allow PicCap to start. HyperDRM releases its histogram capture and clears priority 149 first, allowing PicCap's configured input priority to take over. Any `SVP_CPB` resource selects protected playback immediately. Missing, stale, malformed, idle or HDMI state selects the conservative HyperDRM fallback; an already active PicCap session gets at most 1.5 seconds of unknown-state grace for short playback pauses. Unknown states never start PicCap. PicCap must confirm that it has stopped before histogram capture resumes. Detection is specific to the tested G4 resource model, not a universal DRM-state API.

The switch is saved in the own service container and applies after reboot when Autostart is enabled. Stop pauses both managed capture paths until Start. Turning Auto PicCap off stops a PicCap session started by this controller and restores HyperDRM. Avoid starting another capture service independently while automatic handover is active.

The app displays the selected source and PicCap's Hyperion connection status. Histogram region swatches are hidden while PicCap supplies the colors. Playback classification and isolated controller tests are in `native/test_playback.c` and `native/test_handover.c`; link both with `-ldl`.

## Capture interruption recovery

LG may revoke histogram capture during playback transitions. From 0.7.1, HyperDRM releases capture while leaving its last color update to expire normally after 1.5 seconds, then retries initialization after 200 ms and up to two additional 400 ms intervals. Successful recovery clears this rapid retry budget. Persistent failure returns to the existing five-second retry interval; manual Stop, PicCap handover and shutdown still explicitly clear the own color source. Status exposes `captureRecoveries` and `lastRecoveryMs` for diagnosis.

## Hyperion and HyperHDR receiver settings

Choose **Receiver: Hyperion/HyperHDR**, enter an IPv4 or IPv6 address and the JSON TCP port, then select **Save receiver**. Hyperion uses per-LED JSON color arrays; HyperHDR uses a synthetic spatial RGB image because HyperHDR 22 treats the color command as a solid color. Both use clear and serverinfo. Use the JSON port (usually 19444), not the FlatBuffer port (usually 19400) or web interface port; the FlatBuffer transport option uses that FlatBuffer port instead. Numeric IP addresses are supported; hostnames and scoped IPv6 addresses are not currently accepted. Layout and LED count come from the selected receiver, including remote receivers.

Settings are saved in `target.json` inside the own service container and loaded after service restart/boot. Saving valid settings switches a running session to the new receiver and clears the own priority at the old receiver. Invalid settings leave the current target intact. Saving while manually stopped does not start a session. Uninstalling removes the saved settings with the container.

For a receiver requiring authentication, enter its API token. The token file is mode 0600; status only reports whether a token is saved. Leaving the input blank retains it; **Remove saved token** deletes it when saving. A token login happens before requesting the layout or initializing LG capture. JSON TCP traffic is unencrypted, so use a trusted network. Receiver authorization rules are unchanged.

Auto PicCap uses the selected receiver IP and the separate **PicCap FlatBuffer port** (default 19400), including remote IPv4 receivers and custom ports. Enable the receiver FlatBuffer server. The tested PicCap client supports IPv4 only: local ::1 is mapped to 127.0.0.1 for PicCap, while remote IPv6 keeps histogram capture. API tokens authenticate JSON only; PicCap uses the receiver FlatBuffer protocol.

`native/test_target.c` verifies validation, token retention/removal, private persistence and TCP connection/refusal. `tests/receiver-proxy.py` is a test-only relay with synthetic token login and fragmented replies. Local HyperHDR 22.0.0 and a remote relay using a different IP/port were tested on the G4; the relay forwarded real color updates to HyperHDR without changing its configuration.

## HyperHDR spatial output (0.8.1)

HyperHDR 22's JSON `color` handler ultimately registers only the first RGB triplet as a solid color. Version 0.8.1 uses JSON `image` output for the HyperHDR receiver selection. A cached 160x90 geometry interpolates the calibrated histogram samples into a synthetic color plane; HyperHDR applies its configured LED rectangles to that plane. The payload contains generated colors, not captured or decrypted video frames. HyperHDR's image processing and color calibration affect the resulting LED colors. Select a spatial image mapping mode in HyperHDR; a global single-color mapping mode will still give uniform LEDs.

Hyperion retains its original per-LED color-array output. Receiver IP, JSON port, optional token, priority, timeout and reconnection behavior remain shared. Tests cover exact RGB base64 encoding, image command metadata and spatial edge variation. Output must be verified through the receiver's LED stream, not solely a successful command acknowledgment.

A custom profile can render its samples as **region rectangles** instead of perimeter
cones. The mode is chosen with **Geometry** in the TV calibration panel, survives
recalibration and can be switched back for an A/B comparison; without imported
rectangles one block per measured rim anchor is derived, and measured rectangle
data always takes precedence. See [profiles/README.md](profiles/README.md).

## HyperHDR output for every LED hardware instance (unreleased)

HyperHDR runs one pipeline per LED hardware instance, and its JSON API binds an
instance to the connection: `color`, `image` and `clear` act on the instance that
connection selected, and the schema rejects a per-command `instance` field. A
second instance therefore never received HyperDRM colors; only the first one did.

Select **Output: FlatBuffer, all instances** and save. HyperDRM then sends the
same 160x90 synthetic plane to HyperHDR's FlatBuffer server (the **PicCap
FlatBuffer port**, default 19400) instead of the JSON port. A Register request
declares origin `HyperDRM` and priority 149. HyperHDR decodes each plane once and
broadcasts it through `GlobalSignals::SignalSetGlobalImage` to every running
instance, so every LED hardware instance follows the histogram from a single
connection. Keep the receiver's FlatBuffer server enabled; PicCap and HyperDRM
can share it because only one of them sends at a time.

**Output: JSON** keeps the previous behavior, including API-token authentication,
`tan` transaction matching and the LED layout probe. FlatBuffer traffic carries
no token and is restricted by HyperHDR's own network-origin rules. Both outputs
use priority 149 with a 1.5 s duration, so the receiver lists one input per
instance and falls back when HyperDRM stops; a disconnect also clears the
priority server side. Auto PicCap keeps its JSON fade, which addresses the first
instance only, while PicCap's own FlatBuffer stream covers every instance during
a handover.

`native/test_flatbuffer.c` validates the request layout (alignment, vtables, union
tags and the image plane) and `native/test_flatbuffer_output.c` validates framing,
registration, newest-frame coalescing and server error propagation. The wire
format was accepted by a live HyperHDR 22 FlatBuffer server at an invisible test
priority; per-instance LED output still needs verification on the TV.

## Diagnostics and connection test (0.9.0)

Save the receiver settings, then select **Test connection**. The test opens a separate nonblocking JSON connection, logs in with the saved token when present, and requests and validates the LED layout. It sends no colors, clears no priorities and acquires no TV capture. Results include success/failure, validated LED count, elapsed time and an actionable error. A successful test verifies connectivity and layout access, not the physical LEDs or FlatBuffer input.

The diagnostic line reports service version/root status, capture transition, playback classification, capture recovery count/time, age of the last histogram output and scheduled retry timing. PicCap reports its own connection status; histogram output age is not PicCap frame age. Color calibration, saturation, brightness and smoothing belong in Hyperion/HyperHDR; HyperDRM keeps neutral gains.

Before PicCap starts, its endpoint is read through getSettings. Matching settings are reused. Otherwise setSettings updates only address, port and unix-socket:false; the official implementation restarts PicCap itself, so the controller checks status rather than issuing another start. Endpoint changes are saved by PicCap and remain after disabling handover. Changing the receiver stops the managed PicCap session before configuring the new endpoint. Settings errors/timeouts stop PicCap before histogram fallback, with a cooldown to avoid rapid restarts.

Feature tests are native/test_diagnostics.c, native/test_target.c, native/test_handover.c and tests/receiver-ui.test.js. The native tests cover fragmented replies, rejected authentication, invalid layout, refused connection, timeout, custom PicCap endpoint, configuration failure and exclusive capture ownership. Test-only JSON and FlatBuffer relays allow a real receiver to be reached through another IP and ports without changing receiver configuration.

## Capture resilience and live layout (0.10.0)

If managed PicCap reports disconnected for five seconds, HyperDRM requests its stop, waits for confirmed stopped status and resumes histogram output. Reconnection inside that window clears the loss timer. A 30-second cooldown prevents immediate PicCap restart after a connection failure; normal capture transitions have a three-second reentry delay. Manual Stop, disabling handover, changing the receiver and secure playback take precedence over unknown-state grace.

While a capture session runs, a separate nonblocking authenticated JSON connection checks the LED layout every five seconds. Changed LED count, positions or order replace the validated mapping between frames. Invalid or timed-out responses retain the last good mapping and report a layout error; capture continues. Profile changes cancel obsolete probes. Receiver JSON streaming and the user connection test have independent sockets. HyperHDR continues to apply its own current LED rectangles to the generated color plane.

## TV model profiles (0.10.0)

Select Auto, LG G4 (measured), Average (uniform), or Custom model in TV calibration and select Save profile. The app obtains the model identifier through LG's system-property service. Auto selects an imported profile matching that exact identifier, then the built-in G4 series profile, otherwise uniform average colors. Unknown models are not assigned an invented spatial calibration. A manual G4 selection is available when model detection is unavailable.

Custom imports accept measured model JSON with 2-16 sorted clockwise perimeter points and histogram region indices 0-15. Profiles persist privately in the own service container and apply to the current session without restarting capture. See [profile format and calibration guidance](profiles/README.md). Only G4 hardware has been validated; importing a profile does not guarantee histogram support on another TV. Color calibration remains in Hyperion/HyperHDR.

## Color continuity and guided calibration

A populated histogram containing black pixels is a valid dark scene and is sent
immediately. Empty channel populations and failed reads are missing data, not
black. The last good colors are held for at most 1.5 seconds while capture remains
available; a longer failure triggers bounded capture recovery. Receiver failure
cannot be hidden by color holding. Real black does not wait for a recovery fade.

Source handovers retain the previous output briefly instead of clearing it before
PicCap connects. An independent nonblocking raw LED subscription provides the
previous PicCap colors for the return to HyperDRM, and a fresh PicCap target for
the forward transition. The transition lasts 500 ms, with a three-second overall
handover bound. Conversion from LED samples to histogram regions is approximate;
receiver color correction and configured smoothing remain in the receiver. If
streaming is unavailable, ordinary bounded source selection still works.

In **TV calibration**, select **Calibration** then **Start Auto Calibration**. The bundled H.264 video
shows a black reference and 32 white patches around all four edges. Keep the app
visible and the picture full-screen without zoom or bars. The service temporarily
disables PicCap handover and bar detection for this measurement, then restores
normal operation. Cancellation, Back, playback errors and a 110-second service
lease protect against leaving calibration active after the app closes.

Fresh measurements identify responsive edge regions. Global-only, absent or
ambiguous responses are rejected without overwriting a saved profile. Successful
measurements fill the Custom profile editor; choose **Save profile** to apply.
Select **Validate profile** afterwards: the top-left quadrant is red, top-right
green, bottom-right blue and bottom-left yellow. Check LED positions on every
edge. The measured profile is specific to the exact reported model; other models
still require their own measurement. This does not add HDR color calibration or
more independently captured regions.

An explicit app launch with `{"action":"calibrate"}` starts the same visible
wizard. The calibration video can be reproduced with
`python build_calibration_video.py --ffmpeg /path/to/ffmpeg`.

The TV UI separates Overview, Receiver and Calibration. Auto Calibration is also
linked from Overview. Fields use focusable selection frames; highlighting or
hovering a frame never focuses its input. OK or a click starts editing and opens
the TV keyboard. Keyboard dismissal or Back returns to the selection frame.
Only visible controls participate in arrow navigation. Custom profile JSON is
available through Edit profile JSON after selecting a custom profile.

If an update leaves an old process running without root, Overview shows Enable
Root Access. Changing autostart also handles a root-required reply automatically:
the app asks Homebrew Channel to elevate only its own service, restarts that
service, verifies UID 0, and retries the switch. Root and boot-hook failures have
separate messages. The explicit launch action `{"action":"repairRoot"}` invokes
the same visible repair flow. Homebrew Channel must be installed and permit the
request; denied elevation is reported without changing autostart.

### Development and releases

`VERSION` is the single source for app, package and native runtime versions.
Run `python sync_version.py` after editing it; `--check` rejects stale metadata.
Run `python release.py` to execute every JavaScript and portable native test,
compile the ARM service, build a reproducible IPK and verify its embedded versions,
ARM architecture, size and SHA-256 against the manifest. On Windows this requires
Node.js and Debian WSL with GCC, JSON-C and arm-linux-gnueabi-gcc; on Linux the
same tools must be on PATH. No TV connection or installation occurs.

After reviewing the working tree, `python release.py --push` also commits changes,
creates a new version tag and atomically pushes main and the tag to origin.
An existing tag or a branch other than main is rejected. Attach the exact verified
IPK to the matching Gitea release using an authorized session, then compare the
downloaded attachment's size and SHA-256. Git access does not authorize web uploads.
See [runtime architecture](docs/architecture.md) for module boundaries.

### Receiver resilience and updates

The main receiver socket stays nonblocking during connect, authentication, layout
loading, color output and acknowledgements. Only one command is in flight and the
newest unsent frame replaces older unsent frames. Timed-out or invalid replies
close the connection and use the existing retry policy. Start returns a Connecting
state; Stop cancels startup and sends a bounded asynchronous clear when needed.
LG capture initialization itself remains dependent on the vendor library.

Receiver, profile and PicCap preferences share an atomic writer with private
permissions, interrupted/partial-write handling, file sync, rename and directory
sync. Failures before rename preserve the previous preference. A directory-sync
failure after rename is reported as committed with uncertain crash durability;
it is not represented as a rollback.

The UI compares its generated version against the running service. Mismatches
block settings and startup actions while keeping Stop and status available. After
installing the matching package, Reload service explicitly restarts only HyperDRM
and briefly interrupts ambient lighting; it never runs automatically.

`python release.py --sanitizers` additionally checks protocol, atomic-file,
receiver-output and simulated service-lifecycle tests with ASan/UBSan. These host
tests do not replace real LG capture and remote-control validation on a TV.

Histogram near-black handling suppresses colors with a maximum RGB channel of 16 or less, gradually restores brightness between 16 and 32, and preserves brighter colors. This applies only to histogram output; PicCap, receiver calibration and raw calibration samples are unchanged.

Histogram means and spatial blends are evaluated in linear light (IEC 61966-2-1) and encoded once for output. Averaging encoded values under-reports light: a region that straddles two colors reached the LEDs as a dim olive (125,110,35) instead of the bright mix the picture contains (169,147,38), so mixed regions looked darker than their neighbors. Mixed regions now keep the picture's luminance exactly. Conversions use lookup tables built on first use, so the render loop only does arithmetic.

Thin letterbox detection compares both edges against lit interior histogram samples and requires a consistently dark upper edge for at least two seconds. Histogram probes cannot determine the exact bar height or guarantee detection on every scene.

Histogram capture defaults to 50 ms. The Receiver panel provides a saved Capture interval (ms) setting from 25 to 1000 in steps of 25. Existing settings without this field use 50 ms. PicCap capture timing is unchanged.
