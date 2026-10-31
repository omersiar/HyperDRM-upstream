#ifndef HYPERDRM_HANDOVER_H
#define HYPERDRM_HANDOVER_H
#include "playback.h"
#include "atomic_file.h"
#ifndef CALIBRATION_ACTIVE
#define CALIBRATION_ACTIVE false
#endif
#ifndef PICCAP_BINARY
#define PICCAP_BINARY                                                                              \
    "/media/developer/apps/usr/palm/services/org.webosbrew.piccap.service/hyperion-webos"
#endif
#define HANDOVER_PREF                                                                              \
    "/media/developer/apps/usr/palm/services/org.webosbrew.histogramhyperion.service/"             \
    "handover.json"
static PlaybackJson playback_json;
/* Auto PicCap is the documented setup: HyperDRM runs, PicCap's own autostart is
 * off, and this service owns the capture source. PicCap captures better than the
 * histogram, so it runs whenever the TV plays unprotected content; the histogram
 * takes over for protected playback, where a framebuffer grab is only black.
 * VT_CAPTURE is exclusive, so exactly one of the two holds it at a time. */
static bool handover_enabled, piccap_busy, piccap_connected, media_busy;
static bool piccap_running, piccap_restore, piccap_owned;
static PlaybackState playback;
static uint64_t playback_at, next_media_probe, next_piccap_probe, handover_cooldown,
    piccap_action_at;
typedef enum {
    SOURCE_HISTOGRAM,       /* we capture and send */
    SOURCE_YIELDED,         /* PicCap has the capture; we stay quiet */
    SOURCE_PICCAP_STARTING, /* starting PicCap again, waiting for it */
    SOURCE_PICCAP,          /* PicCap is capturing again */
    SOURCE_PICCAP_STOPPING  /* we asked PicCap to stop, waiting for confirmation */
} CaptureSource;
static CaptureSource capture_source;
static void *service_handle;
static bool (*call_once)(void *, const char *, const char *, Callback, void *, unsigned long *,
                         LSError *);
static bool (*cancel_call)(void *, unsigned long, LSError *);
static unsigned long piccap_token, media_token;
static uint64_t piccap_deadline, media_deadline;
static unsigned piccap_fallbacks;
static uint64_t piccap_disconnected_since;
#define HANDOVER_POLL_MS 1000
/* A playback report is trusted only while it is fresh. A reply that stops
 * arriving means "unknown", never "unprotected". */
#define HANDOVER_PLAYBACK_FRESH_MS 3000
/* Starting PicCap needs a settled answer, not one lucky poll: three clear
 * replies over two seconds. */
#define HANDOVER_CLEAR_SAMPLES 3
#define HANDOVER_CLEAR_STABLE_MS 2000
/* After taking the capture from PicCap, give it five seconds before handing it
 * back, so a pause in the protected stream cannot bounce the source. */
#define HANDOVER_DWELL_MS 5000
/* If the histogram produces nothing this long after a takeover, LG is not going
 * to give it to us during this stream. Give the capture back instead of leaving
 * the feed glitching. */
#define HANDOVER_TAKEOVER_GIVEUP_MS 15000
#define HANDOVER_GIVEUP_COOLDOWN_MS 60000
#define PICCAP_START_GRACE_MS 2000
#define PICCAP_LOSS_MS 5000
#define PICCAP_FAILURE_COOLDOWN_MS 30000
static unsigned clear_samples;
static uint64_t clear_since, histogram_since, transition_at;
static TargetConfig piccap_target;
static char piccap_error[128];
static bool piccap_request(int command);
/* True while PicCap is expected to hold VT_CAPTURE. The histogram path then
 * neither retries nor sends, so the two services cannot take the resource from
 * each other in a loop. */
static inline bool handover_yield(void) {
    return capture_source != SOURCE_HISTOGRAM;
}
static inline bool piccap_installed(void) {
    return access(PICCAP_BINARY, X_OK) == 0;
}
static bool handover_save(bool enabled) {
    const char *data = enabled ? "{\"piccapHandover\":true}\n" : "{\"piccapHandover\":false}\n";
    return atomic_replace(HANDOVER_PREF, data, strlen(data)) != SAVE_UNCHANGED;
}
static void handover_load(void) {
    int fd = open(HANDOVER_PREF, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return;
    char text[128];
    ssize_t n = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (n > 0) {
        text[n] = 0;
        void *root = playback_json.parse(text);
        handover_enabled = playback_true(&playback_json, root, "piccapHandover");
        if (root)
            playback_json.put(root);
    }
}
/* Only the three resource states are used, and an error reply says nothing at all
 * -- reading one as "unprotected" would start PicCap on a dropped call. */
static bool media_reply(void *h, void *m, void *ctx) {
    (void)h;
    (void)ctx;
    media_busy = false;
    media_token = 0;
    void *root = playback_json.parse(payload(m));
    bool list = root && playback_json.type(root) == 5;
    if (root)
        playback_json.put(root);
    if (!list)
        return true;
    uint64_t now = milliseconds();
    playback = playback_state(&playback_json, payload(m));
    playback_at = now;
    if (playback == PLAYBACK_CLEAR) {
        if (!clear_samples)
            clear_since = now;
        if (clear_samples < HANDOVER_CLEAR_SAMPLES)
            clear_samples++;
    } else {
        clear_samples = 0;
        clear_since = 0;
    }
    return true;
}
/* Commands: 0 status, 1 start, 2 stop, 3 getSettings, 4 setSettings. Confirm stopped status before
 * VT reuse. */
static bool piccap_reply(void *h, void *m, void *ctx) {
    (void)h;
    int command = (int)(intptr_t)ctx;
    piccap_busy = false;
    piccap_token = 0;
    void *root = playback_json.parse(payload(m));
    bool ok = playback_true(&playback_json, root, "returnValue");
    if (command == 0) {
        void *running = playback_field(&playback_json, root, "isRunning");
        if (ok && running && playback_json.type(running) == 1) {
            uint64_t now = milliseconds();
            bool active = playback_json.boolean(running);
            piccap_running = active;
            if (active) {
                piccap_connected = playback_true(&playback_json, root, "connected");
                if (capture_source == SOURCE_PICCAP_STARTING) {
                    capture_source = SOURCE_PICCAP;
                    piccap_restore = false;
                }
                if (piccap_connected)
                    piccap_disconnected_since = 0;
                else if (!piccap_disconnected_since)
                    piccap_disconnected_since = now;
            } else {
                piccap_connected = false;
                piccap_disconnected_since = 0;
                bool starting = capture_source == SOURCE_PICCAP_STARTING &&
                                now - piccap_action_at < PICCAP_START_GRACE_MS;
                if (!starting) {
                    /* The capture resource is free. Take it -- our own retry
                     * path initialises immediately, so the receiver's last
                     * frame is replaced by the first histogram frame. */
                    if (capture_source != SOURCE_HISTOGRAM) {
                        /* A takeover keeps its own explanation; any other stop
                         * is worth reporting because nobody asked for it. */
                        if (capture_source == SOURCE_PICCAP_STOPPING && !piccap_restore)
                            snprintf(piccap_error, sizeof(piccap_error),
                                     "PicCap stopped; histogram resumed");
                        piccap_owned = false;
                        capture_source = SOURCE_HISTOGRAM;
                        histogram_since = now;
                        if (!retries.paused && !retries.blocked) {
                            retries.quick_retries = 2;
                            retries.due = now;
                        }
                    }
                }
            }
        } else if (!piccap_installed()) {
            piccap_running = false;
            piccap_restore = false;
            capture_source = SOURCE_HISTOGRAM;
            piccap_connected = false;
        }
    } else if (command == 3 && capture_source == SOURCE_PICCAP_STARTING) {
        void *port = playback_field(&playback_json, root, "port"),
             *socket = playback_field(&playback_json, root, "unix-socket");
        bool same =
            ok &&
            playback_is(&playback_json, root, "address", target_piccap_host(&piccap_target)) &&
            port && playback_json.type(port) == 3 &&
            strtoul(playback_json.string(port), NULL, 10) == piccap_target.piccap_port && socket &&
            playback_json.type(socket) == 1 && !playback_json.boolean(socket);
        if (ok) {
            if (!piccap_request(same ? 1 : 4)) {
                capture_source = SOURCE_HISTOGRAM;
                piccap_owned = false;
                snprintf(piccap_error, sizeof(piccap_error), "PicCap endpoint request failed");
            }
        } else {
            capture_source = SOURCE_HISTOGRAM;
            piccap_owned = false;
            snprintf(piccap_error, sizeof(piccap_error), "Cannot read PicCap settings");
        }
    } else if (command == 4) {
        if (!ok) {
            capture_source = SOURCE_HISTOGRAM;
            piccap_owned = false;
            snprintf(piccap_error, sizeof(piccap_error), "PicCap rejected receiver settings");
        }
        /* setSettings restarts PicCap itself; status confirms ownership. */
    } else if (command == 1 && !ok) {
        capture_source = SOURCE_HISTOGRAM;
        piccap_owned = false;
        piccap_restore = false;
        handover_cooldown = milliseconds() + PICCAP_FAILURE_COOLDOWN_MS;
        snprintf(piccap_error, sizeof(piccap_error), "PicCap refused to start");
    }
    if (root)
        playback_json.put(root);
    next_piccap_probe = milliseconds() + 500;
    if (command == 2)
        piccap_request(0);
    return true;
}
static bool piccap_request(int command) {
    if (piccap_busy)
        return false;
    const char *uri = command == 3   ? "luna://org.webosbrew.piccap.service/getSettings"
                      : command == 4 ? "luna://org.webosbrew.piccap.service/setSettings"
                      : command == 1 ? "luna://org.webosbrew.piccap.service/start"
                      : command == 2 ? "luna://org.webosbrew.piccap.service/stop"
                                     : "luna://org.webosbrew.piccap.service/status";
    LSError e;
    error_init(&e);
    char data[160];
    snprintf(data, sizeof(data), "{\"address\":\"%s\",\"port\":%u,\"unix-socket\":false}",
             target_piccap_host(&piccap_target), piccap_target.piccap_port);
    piccap_busy = call_once(service_handle, uri, command == 4 ? data : "{}", piccap_reply,
                            (void *)(intptr_t)command, &piccap_token, &e);
    piccap_deadline = milliseconds() + 3000;
    error_free(&e);
    if (command)
        piccap_action_at = milliseconds();
    return piccap_busy;
}
/* Decide who owns VT_CAPTURE. `capture_ok` is our own histogram session.
 *
 * PicCap is the primary capture: it is what the user runs, it has the higher
 * resolution, and it is registered at a higher receiver priority. We therefore
 * stay quiet whenever it is capturing, and only step in for protected playback,
 * where its frames are black. Both directions hold the last delivered picture on
 * the receiver until the other service's frames arrive, so the LEDs do not
 * blink while the source changes. */
static bool handover_tick(bool capture_ok, uint64_t last_output_at) {    uint64_t now = milliseconds();
    if (media_busy && now >= media_deadline) {
        LSError e;
        error_init(&e);
        cancel_call(service_handle, media_token, &e);
        error_free(&e);
        media_busy = false;
        media_token = 0;
        playback_at = 0;
    }
    if (piccap_busy && now >= piccap_deadline) {
        snprintf(piccap_error, sizeof(piccap_error), "PicCap request timed out");
        LSError e;
        error_init(&e);
        cancel_call(service_handle, piccap_token, &e);
        error_free(&e);
        piccap_busy = false;
        piccap_token = 0;
        next_piccap_probe = 0;
    }
    bool session = !CALIBRATION_ACTIVE && handover_enabled && target_piccap_compatible(&target) &&
                   piccap_installed() && !retries.paused && !retries.blocked;
    bool fresh = playback_at && now - playback_at < HANDOVER_PLAYBACK_FRESH_MS;
    bool secure = fresh && playback == PLAYBACK_PROTECTED;
    bool settled = fresh && playback == PLAYBACK_CLEAR &&
                   clear_samples >= HANDOVER_CLEAR_SAMPLES && clear_since &&
                   now - clear_since >= HANDOVER_CLEAR_STABLE_MS;

    /* 1. Protected playback: PicCap can only deliver black, so stop it and take
     *    the capture. */
    if (session && secure && now >= handover_cooldown &&
        capture_source != SOURCE_PICCAP_STOPPING &&
        capture_source != SOURCE_PICCAP_STARTING) {
        if (piccap_running) {
            piccap_restore = true;
            capture_source = SOURCE_PICCAP_STOPPING;
            next_piccap_probe = 0;
            snprintf(piccap_error, sizeof(piccap_error), "Protected playback; taking capture");
        } else if (capture_source != SOURCE_HISTOGRAM) {
            capture_source = SOURCE_HISTOGRAM;
            histogram_since = now;
            if (!retries.paused && !retries.blocked) {
                retries.quick_retries = 2;
                retries.due = now;
            }
        }
    }

    /* 2. Unprotected playback: PicCap is the better source, so run it. This is
     *    the documented setup -- PicCap's own autostart off, HyperDRM starts and
     *    stops it. Every other screen keeps whichever source already runs, which
     *    is what stops the source flapping whenever a player hides its decoder. */
    bool dwell = !histogram_since || now - histogram_since >= HANDOVER_DWELL_MS;
    if (session && !secure && settled && dwell && now >= handover_cooldown &&
        now >= transition_at && capture_source == SOURCE_HISTOGRAM) {
        piccap_target = target;
        piccap_error[0] = 0;
        release_capture_hold(now);
        if (piccap_running) {
            capture_source = SOURCE_PICCAP;
        } else {
            capture_source = SOURCE_PICCAP_STARTING;
            piccap_owned = true;
            if (!piccap_request(3)) {
                /* A poll was in flight. Nothing is wrong: stay on the histogram
                 * and ask again shortly instead of dropping the transition. */
                capture_source = SOURCE_HISTOGRAM;
                piccap_owned = false;
                transition_at = now + HANDOVER_POLL_MS;
            }
        }
    }

    /* 3. A PicCap this service started owns the resource: follow it when we are
     *    not capturing. Re-initialising would revoke it and start the tug of war. */
    if (!capture_ok && capture_source == SOURCE_HISTOGRAM && piccap_running &&
        !piccap_restore && now >= handover_cooldown) {
        capture_source = SOURCE_YIELDED;
        piccap_error[0] = 0;
    }

    /* 4. The histogram is not producing under this stream. Keeping PicCap
     *    stopped and retrying forever only glitches the LEDs, so give the
     *    capture back and stay out of the way for a while. */
    bool producing = capture_ok && histogram_since && last_output_at >= histogram_since;
    if (piccap_restore && capture_source == SOURCE_HISTOGRAM && !producing && histogram_since &&
        now - histogram_since >= HANDOVER_TAKEOVER_GIVEUP_MS && now >= transition_at) {
        piccap_target = target;
        release_capture_hold(now);
        capture_source = SOURCE_PICCAP_STARTING;
        if (piccap_request(3)) {
            piccap_restore = false;
            piccap_owned = true;
            handover_cooldown = now + HANDOVER_GIVEUP_COOLDOWN_MS;
            snprintf(piccap_error, sizeof(piccap_error),
                     "Histogram unreadable while protected; PicCap restored");
        } else {
            capture_source = SOURCE_HISTOGRAM;
            transition_at = now + HANDOVER_POLL_MS;
        }
    }

    /* 5. PicCap went away while we were relying on it: take the capture back. */
    bool lost = (capture_source == SOURCE_PICCAP || capture_source == SOURCE_YIELDED) &&
                piccap_running && piccap_disconnected_since &&
                now - piccap_disconnected_since >= PICCAP_LOSS_MS;
    if (lost) {
        snprintf(piccap_error, sizeof(piccap_error),
                 "PicCap receiver disconnected; histogram fallback");
        piccap_fallbacks++;
        /* PicCap is still running, so remember that the capture is ours now --
         * the yield branch must not hand it straight back. */
        piccap_restore = true;
        handover_cooldown = now + PICCAP_FAILURE_COOLDOWN_MS;
        capture_source = SOURCE_HISTOGRAM;
        histogram_since = now;
        if (!retries.paused && !retries.blocked) {
            retries.quick_retries = 2;
            retries.due = now;
        }
    }

    /* 6. Stop the PicCap this service started once the feed is stopped or Auto
     *    PicCap is turned off. A PicCap the user runs is left alone. */
    if ((retries.paused || !handover_enabled) && piccap_owned &&
        capture_source != SOURCE_PICCAP_STOPPING &&
        capture_source != SOURCE_PICCAP_STARTING) {
        capture_source = SOURCE_PICCAP_STOPPING;
        piccap_restore = false;
        next_piccap_probe = 0;
    }

    /* 5. Drive whatever request is pending. */
    if ((capture_source == SOURCE_PICCAP_STOPPING || capture_source == SOURCE_PICCAP_STARTING ||
         capture_source == SOURCE_PICCAP) &&
        !piccap_busy && now >= next_piccap_probe) {
        piccap_request(capture_source == SOURCE_PICCAP_STOPPING ? 2 : 0);
        next_piccap_probe = now + HANDOVER_POLL_MS;
    }
    /* 6. Poll last. A transition above always gets the bus first; a poll that
     *    was already in flight used to make the next transition fail.
     *
     * PicCap's own state is polled while Auto PicCap is on, and whenever we are
     * not the capturing service: a PicCap the user started must be handed the
     * resource instead of being revoked over and over. */
    if ((session || !capture_ok) && piccap_installed() && playback_json.lib && !piccap_busy &&
        now >= next_piccap_probe) {
        piccap_request(0);
        next_piccap_probe = now + HANDOVER_POLL_MS;
    }
    if (session && playback_json.lib && !media_busy && now >= next_media_probe) {
        LSError e;
        error_init(&e);
        media_busy = call_once(service_handle, "luna://com.webos.media/getActivePipelines", "{}",
                               media_reply, NULL, &media_token, &e);
        media_deadline = now + 3000;
        error_free(&e);
        next_media_probe = now + HANDOVER_POLL_MS;
    }
    /* Hand the capture back when Auto PicCap is turned off mid-session. */
    if (!session && piccap_restore && capture_source == SOURCE_HISTOGRAM) {
        piccap_restore = false;
    }
    return handover_yield();
}
#endif
