# webOSbrew publishing review

Requirements: https://www.webosbrew.org/develop/guides/publishing/rules/
Reviewed: 2026-10-06. No submission has been opened.

## Human review and disclosure

The implementation and original icon were produced with substantial AI assistance. The owner directed development and tested visual behavior on a real LG G4 with webOS 24. These user tests do not establish that a human has reviewed the entire source. Before submission, a maintainer must meaningfully review the implementation and be able to explain its design; the submission's AI disclosure must reflect the actual history. Removing creation notes does not change that history. The generated icon should be reviewed or replaced by the owner, according to the recommendation that icons and descriptions be the author's own work.

Design review points: histogram bin width and RGB averages; LG developer-marker lifetime; bounded Hyperion layout parsing; interpolation/calibration limits; black-bar preset transitions; root-only methods; boot-hook migration; source-transition retries and cancellation.

## DRM and content

No media, DRM keys, ROMs, account bypasses or third-party capture binaries are distributed. The calibration video is a locally generated coordinate ramp. The app sends RGB color values rather than video frames.

The capture initializer temporarily creates `/var/luna/preferences/vt_devmode` to pass LG's developer gate. Although no decryption or DRM patch is implemented, it has not been established whether the resulting access to color data during protected playback is acceptable under webOSbrew's prohibition of DRM circumvention. This must be reviewed explicitly before submission. Do not certify DRM compliance solely because only colors are output.

## Hardware and removal

No writes to signed system partitions. Writes outside the service container are limited to the own Homebrew autostart symlink, bounded boot log and transient developer marker. A guardian removes the own marker after initialization or process termination; a previously existing marker is preserved. Concurrent writes/power loss remain review considerations.

The boot hook is a symlink to `service/boot.sh` in the installed container. The script checks for its executable and service metadata before acting, checks them again on every retry, and exits when the hook is disabled or the target is removed. Since 0.6.4 it delegates ongoing Hyperion recovery to the native service once startup or background retries are accepted. Only an exact known legacy own hook is migrated; foreign files/symlinks are rejected. Logs are truncated at each invocation and contain only startup/delegation/cancellation messages, so waiting has no unbounded boot-log growth.

## Source, licence and release

Public repository page and raw README access were verified without credentials (HTTP 200); the API restricts anonymous requests. An identifiable project licence must be chosen by the owner. No vendored proprietary libraries are shipped. A release manifest must record the exact IPK hash/size and root requirement. The apps-repo package must use pool `main`, a public sourceUrl and public HTTPS manifest/IPK/icon URLs. No listing is ready until those URLs resolve without credentials.

## Evidence and remaining checks

G4/webOS 24: original capture, spatial output, color boost, dynamic layout tests (50/500), black-bar classification and boot-hook invocation tested. Other models are unsupported until tested. Autostart symlink enable/disable, exact legacy migration, rejection of foreign files/symlinks, temporary-file collision and missing-target removal passed in an isolated ARM test on the TV. Version 0.6.1 was installed and started on the G4; the existing copied production hook migrated to the correct container symlink. Boot script syntax passed on the TV. Actual cold boot remains untested. A real uninstall and clean-state reinstall were performed on 2026-10-06; details follow below. Public listing eligibility remains pending human and DRM review, release URL validation and licence choice. The runtime reports Rockhopper 10.2.2-57 and device SoC o24; these measured values are used for the package requirements rather than an unverified minimum release.

## Actual uninstall/reinstall test (2026-10-06)

Platform: LG G4, Rockhopper 10.2.2-57, SoC o24. Package: HyperDRM 0.6.1.

- Removed the running app through `com.webos.appInstallService/dev/remove`; installer reported `removed`, progress 100. Both app and service containers disappeared and the native process stopped.
- The own startup symlink became dangling. Homebrew run-parts dry-run reported the missing target and did not list it among executable hooks. This produces a diagnostic line, rather than executing an orphaned script.
- A saved copy of the own script, invoked with its installed target absent, exited 0 immediately. The boot log hash was unchanged.
- Removed only the verified own dangling symlink for a fresh-state test. Reinstallation reported `update:false`. Before root elevation, status showed running:false, autostart:false and a non-root UID.
- Root elevation after that first status call did not replace the already-running unprivileged service process. Start failed explicitly rather than creating the developer marker. Verified the exact own process path and terminated that process with SIGTERM; the next activation started as root and color output succeeded. Installation instructions now require elevation before first activation and explain this recovery case.
- Restored the user's enabled autostart setting. No Hyperion configuration, PicCap or foreign hook was changed.

This is an actual package lifecycle test, not a TV factory reset and not a cold boot.

## Persistent startup/reconnect retries (0.6.4)

With autostart enabled, the native service retries startup every five seconds without an attempt limit. Hyperion TCP availability and layout are checked before acquiring capture. The boot hook exits once native startup or background retries are accepted; it no longer owns the long-running reconnect loop. Manual Stop pauses retries for the current service session. Start or re-enabling autostart resumes them. Failed developer-marker cleanup blocks retries until manual intervention. Turning autostart off cancels automatic pending attempts.

ARM retry-state tests on the G4 passed 10000 delayed-start attempts, five-second pacing, manual Stop, disabling autostart, the cleanup safety block, bounded non-autostart source recovery and disconnect retry. Live tests temporarily stopped and restored Hyperion without changing configuration; automatic retry continued beyond six attempts (observed counts 10 and 11). Manual Stop left retryPaused:true and retryScheduled:false after Hyperion was restored.

A second live disconnect test confirmed retryScheduled:true while Hyperion was unavailable, followed by running:true and colorValid:true automatically within the six-second observation window after Hyperion restarted, without a new HyperDRM Start command.

## Optional PicCap handover (0.7.0)

The default-off Auto PicCap switch stores its preference in the own service container. If enabled, it uses the installed PicCap service's existing start/stop/status methods; it does not rewrite PicCap settings or its autostart hook. Only foreground media decoder resources without `SVP_CPB` are eligible, after two samples. Any secure resource or unknown/stale state restores the histogram path. VT capture is released and HyperDRM priority cleared before starting PicCap; stopped status is required before histogram capture resumes. Manual Stop pauses both managed paths. Shutdown attempts to stop a managed PicCap session before unregistering; a forced kill or unresponsive external service cannot guarantee this cleanup.

On the G4, playback-classification and isolated controller tests passed: secure/clear/unknown states, mixed pipelines, malformed records, two-sample gating, exclusive transitions, manual Stop, disabling handover and timeout cancellation. The fifth control passed remote-navigation regression tests. Version 0.7.0 was installed and elevated; with Disney+ playing, enabling handover reported protected playback, maintained histogram color output and left PicCap stopped. The test preference was restored to off. Live handover was then verified with the user's unprotected test video: PicCap reported videoRunning:true and connected:true, and Hyperion selected its visible FlatBuffer input at priority 150 with spatial output for the current 120-LED layout. Disabling handover stopped PicCap and restored histogram output. Re-enabling handover and switching back to DRM automatically stopped PicCap and restored HyperDRM as Hyperion's visible priority-149 source. The preference was restored to off after the complete test.

## Capture blackout diagnosis and recovery (0.7.1)

A live 0.7.0 observation reproduced the reported blackout while Auto PicCap was disabled and PicCap was stopped. LG revoked histogram capture; the controller cleared priority 149 and waited five seconds before reinitializing. Hyperion LED-stream measurements recorded an all-black interval of approximately 4.9 seconds. WLED was reachable and reported an active DDP stream from the TV; no WLED settings were changed.

Version 0.7.1 preserves the last update's existing 1.5-second expiry during a revocation recovery, instead of explicitly clearing it. Initialization retries begin after 200 ms with two additional 400 ms retry intervals before returning to normal five-second pacing. This does not extend stale colors indefinitely. Explicit capture stops and PicCap handovers retain their clear behavior. Status records recovery count and elapsed initialization time.

ARM retry and handover regression tests passed on the G4, as did remote navigation tests. Version 0.7.1 was installed and elevated on the TV. A later actual LG revocation recovered in 762 ms, including one refused initialization attempt before success. A 55-second Hyperion observation contained 125 color samples with no all-black output. These measurements do not guarantee that prolonged LG capture unavailability or actual black video scenes cannot darken the LEDs.

## Configurable Hyperion/HyperHDR receiver (0.8.0)

The app can save receiver type, numeric IPv4/IPv6 address, JSON TCP port and an optional API token. Targets and tokens are validated before replacing the configuration or interrupting capture. The own container stores `target.json` atomically with mode 0600; status omits the token value. TCP connection attempts are bounded to one second. Authentication and remote layout validation happen before acquiring LG capture. The receiver's own authorization and LED-device settings are not changed. Auto PicCap remains limited to the default local target; custom targets use histogram capture rather than sending PicCap data to an independently configured receiver.

Native target tests passed on the G4, including invalid values, token retention/removal, private persistence and TCP success/refusal. Handover and input-field remote-navigation regression tests passed. HyperDRM 0.8.0 was installed and tested against the user's HyperHDR 22.0.0 on localhost:19444: acknowledged color-array updates for its 120-LED layout. The 0.8.0 test did not check per-LED output diversity; the later 0.8.1 investigation found that HyperHDR rendered these arrays as a solid color. A temporary test relay on another LAN host at port 19445 exercised synthetic token login and fragmented replies, forwarding actual layout/color/clear commands to HyperHDR. Omitting the token preserved it; an invalid target was rejected without changing the active target. A verified own-service process restart loaded the saved HyperHDR target. The test relay was stopped and the target restored to localhost:19444 with no saved token. Network tests used IPv4; IPv6 parsing is covered by unit tests, but an IPv6 receiver was not available for an end-to-end test. Cold boot remains untested.

The API compatibility was checked against official HyperHDR `sources/api/HyperAPI.cpp` and `sources/base/HyperHdrInstance.cpp`: serverinfo includes the LED layout, color accepts RGB arrays, and token authorization precedes API commands.

## Actual HyperHDR spatial output correction (0.8.1)

HyperHDR 22's `HyperHdrInstance::setColor` registers `ledColors[0]` as a solid color, despite the API accepting a longer array. Live output confirmed 120 LEDs with one unique color. The earlier 0.8.0 acknowledgment-only validation was insufficient to establish spatial output.

For HyperHDR selection, 0.8.1 sends an RGB JSON image generated from the calibrated histogram edge samples on a cached 160x90 geometry. The receiver samples the synthetic plane using its LED rectangles and image processing. Hyperion retains per-LED color-array output. No TV video pixels are captured or transmitted by the generated plane. Original saturation/brightness gains remain 1.0. Receiver configuration is not modified.

On the G4 with HyperHDR 22.0.0, a temporary spatial test input produced 120 unique colors for 120 LEDs and was cleared. Exact RGB/base64, image command and spatial geometry tests passed on ARM. After installation of 0.8.1, the real histogram source was visible as IMAGE at priority 149. Eleven LED-stream samples from actual playback contained 87-100 unique colors across the 120 LEDs, confirming spatial output rather than a solid color. This plane has approximate calibrated spatial resolution, not full video-frame detail; HyperHDR's configured mapping and calibration affect the output.

## Receiver diagnostics and remote PicCap (0.9.0)

The own service exposes a nonblocking read-only JSON connection test, separate from streaming/capture. The UI reports connection/layout/authentication results and capture/retry diagnostics without exposing saved API tokens. Auto PicCap now reads settings and updates only address, port and unix-socket through its existing API when required. Unlike older versions, endpoint configuration is saved by PicCap; other settings and its autostart preference are preserved. PicCap setSettings restarts its capture, so HyperDRM releases VT first and checks status before treating the source as active. Remote IPv6 keeps histogram capture because the tested PicCap network client is IPv4-only.

ARM tests on the G4 passed connection diagnostics, fragmented replies, invalid layout, rejected token, connection refusal/timeout, receiver setting persistence and remote PicCap controller transitions. The app receiver-field and remote-navigation tests passed. The installed local HyperHDR JSON test validated 120 LEDs in 224 ms; a remote relay with synthetic authentication and fragmented responses validated the same layout in 625 ms. The unprotected test video used PicCap through the PC address and custom FlatBuffer port, with connected:true, videoRunning:true and actual FlatBuffer bytes received by the relay. PicCap retained all non-endpoint settings, including autostart:false. These relays test a remote transport to the actual local HyperHDR instance, not a separately installed remote HyperHDR instance.

Switching the user playback back to DRM stopped PicCap automatically and resumed histogram output through the remote JSON relay. Seven actual HyperHDR LED-stream samples contained 89-91 distinct colors for 120 LEDs, with no all-black sample. Both test endpoints were restored to local defaults and the synthetic token was removed.

The remote FlatBuffer relay received over 411 MB from PicCap during the test session. The final port validation also rejects explicit JSON null while retaining the default for older preferences without the new field.

## Capture resilience, live layout and model profiles (0.10.0)

Managed PicCap requires three clear media samples over two seconds before starting. Active PicCap has a bounded 1.5-second grace for unknown state; secure playback, manual Stop, handover disable and endpoint changes take precedence. Five seconds of reported receiver disconnection trigger a confirmed PicCap stop and histogram fallback, followed by a 30-second cooldown. A successful reconnection clears the loss timer. ARM controller tests passed stable gating, pause grace expiry, immediate secure transition, reconnect/loss/cooldown, exclusive capture, custom endpoints and request failures.

A separate nonblocking authenticated receiver probe refreshes the LED mapping every five seconds. Invalid responses retain the good mapping. Tests passed 50-to-500 LED replacement, changed positions at the same count, unchanged mappings, obsolete profile-result cancellation and failure retention. The live relay test read 50 LEDs at frame 2, changed to 500 at frame 53, applied changed 500-LED positions at frame 101, retained output after an empty layout at frame 149, and restored the actual 120-LED layout at frame 195. Capture/output continued across these checks without resetting the frame counter. Receiver configuration and WLED settings were not modified.

The app automatically detected OLED65G42LW.DEUQLJP and selected the measured G4 profile. A live switch to Average produced one unique output color across four HyperHDR LED-stream samples; switching back to G4 produced 80-97 unique colors. Auto and the original receiver/handover preferences were restored afterward. Custom profiles validate 2-16 sorted rim positions, bounded region indices and model identifiers, persist privately, and are selected automatically only for the exact imported model identifier. No measured spatial profile for other hardware is claimed. Native tests passed profile parsing/persistence, model selection and average fallback. The synthetic-image test verifies geometry cache replacement after a custom-profile change.

The receiver UI and remote-navigation tests passed, including custom JSON input and profile controls. Diagnosis export was deliberately excluded from this release. Cold boot and other TV models remain unverified.

## Color continuity, calibration wizard and TV navigation (0.11.0)

Empty histogram populations and failed reads are distinguished from populated
black frames. A bounded 1.5-second hold expires into capture recovery; valid dark
frames bypass the 500 ms recovery blend without altering their RGB values. ARM
tests verify hold timing, black populations, recovery mixing and near-black codec
rounding. An independent nonblocking raw receiver LED subscription accepts old
result and current data envelopes, authenticates when required, handles fragmented
messages, validates array values/counts and rebuilds mappings on layout/profile
changes. Source bridge tests cover both directions, fresh-sample gating, midpoint
and completion, genuine black, stale samples, timeout, connection setup and Stop.

The bundled reproducible H.264 calibration video measures 32 edge positions.
PicCap handover and bar detection are suppressed only for the bounded calibration
session. The wizard starts the video plane before waiting for capture, requires
fresh samples, rejects global/multimodal responses and selects the most localized
anchor when nested regions overlap. The live G4 run produced ten sorted anchors
covering all four edges and preserved the saved Auto/G4 profile. One source
revocation recovered in 252 ms. Other TV hardware remains unverified; candidates
require user review/save and four-corner validation. Failed measurements and
cancellation preserve the saved profile.

Overview, Receiver and Calibration are separate sections. Start Auto Calibration
is directly visible in Calibration and linked from Overview. Focusable field
frames keep native inputs unfocused during highlighting; OK/click explicitly
starts keyboard editing. The documented keyboardStateChange event and Back
restore frame navigation. Tests cover section selection, hidden/disabled controls,
input focus only after activation, keyboard dismissal, wizard scope and rejection,
proposal versus saved state, JSON validation and root-repair retries/denials.

The root repair targets only org.webosbrew.histogramhyperion.service via Homebrew
Channel and restarts only this service. The TV's current root process and boot
symlink were verified. Actual autostart disable/enable requests succeeded, with
Autostart restored to On. No receiver color correction, WLED configuration or
PicCap autostart setting was changed. A local browser preview was unavailable
because the browser policy rejected file URLs; no alternate browser path was used.

## Version 0.12.0

Receiver stream processing now drains complete buffered messages when no new
bytes arrive. A regression fixture buffers seventeen updates and verifies all
three processing batches without additional socket writes. Action errors survive
status polling and are cleared by the next user action. Tests also cover delayed
calibration replies after cancellation and a new session.

Connection and bridge states are named enums; UI calibration phases and session
identity checks isolate asynchronous work. The native service is separated into
runtime bootstrap, Luna actions, calibration, status and capture-loop modules.
Production C and JavaScript are formatted consistently; include dependency order
is preserved explicitly.

VERSION generates app/package/runtime versions. release.py passes all JavaScript,
portable native and Python version-drift tests, cross-compiles the ARM service and
checks the IPK's embedded metadata, executable architecture, size and SHA-256.
Package timestamps are deterministic. No TV installation, application launch or
service restart was performed for this version while the user was watching a film.
Live LG capture and physical remote validation remain pending for this release.

## Version 0.13.0

The main receiver connection uses nonblocking sockets for connect, login, layout,
color commands, matching transaction acknowledgements and clear. The synchronous
connector and send/receive loops were removed from production. Unsent frames
coalesce and per-tick byte/message budgets and transaction deadlines bound work.
Stop cancels an in-progress connection and clears pending output asynchronously.
LG capture initialization itself still depends on synchronous vendor APIs.

Receiver, calibration and PicCap preferences share a private atomic writer with
unique temporary files, partial/EINTR write handling, file and directory sync,
rename, symlink rejection and explicit post-commit durability outcomes. Failure
before rename preserves the previous file; failure syncing the directory after
commit is logged as uncertain durability rather than incorrectly claiming rollback.

Protocol tests cover fragmented, truncated, malformed, oversized and embedded-NUL
responses, invalid types, stale transaction IDs, send backpressure, deadlines and
disconnects. A deterministic 5000-input parser mutation test supplements the fixed
cases. ASan/UBSan checks include protocol, file replacement, output and a simulated
full service lifecycle. The latter verifies Start/Stop during connect and responsive
Status/Stop while an ACK is delayed, with a fake LG capture implementation.

Capture, receiver/layout and calibration session state have explicit contexts;
bridge and layout-refresh functions receive their contexts directly. Shared LG/Luna
and persisted calibration/handover state remain scoped to the service. Generated
UI and native versions must match; settings/startup actions are blocked otherwise,
while Stop and status remain available. Reload service is an explicit user action,
never an automatic restart.

All host checks and the ARM package verification pass. No TV connection, install,
launch or service restart was performed. Real LG capture, flash-filesystem behavior,
receiver interoperability and physical remote validation remain pending on the TV.

## HyperHDR FlatBuffer output for every LED instance (unreleased)

No new capture path is involved: the same sixteen synthetic histogram regions are
encoded as a raw RGB plane instead of base64 JSON. The verification below was
performed on the development host only.

- `native/test_flatbuffer.c` and `native/test_flatbuffer_output.c` pass with
  ASan/UBSan, together with the existing native, Python and UI suites.
- The encoded requests were accepted by a live HyperHDR 22.0.0 FlatBuffer server
  (TV at 192.168.2.14:19400) using priority 249, an invisible test priority below
  the active PicCap input at 150. `serverinfo` reported `249 FLATBUFSERVER
  HyperDRM@...` while a frame was registered and no entry afterwards, and every
  Register, Image and Clear request returned an empty `Reply.error`. Nothing was
  installed, reconfigured or restarted on the TV.
- Not verified: end-to-end colors from the TV service over FlatBuffer, per-instance
  LED output, and behavior with more than two instances. Building the ARM package,
  installing it, elevation and a DRM playback check remain pending on the TV.
