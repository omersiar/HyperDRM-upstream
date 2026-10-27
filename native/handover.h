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
static bool handover_enabled, piccap_owned, piccap_busy, media_busy, piccap_connected;
static PlaybackProtection protection;
static uint64_t protection_at, next_media_probe, next_piccap_probe, handover_cooldown,
    piccap_action_at;
typedef enum {
    SOURCE_HISTOGRAM,
    SOURCE_PICCAP_STARTING,
    SOURCE_PICCAP,
    SOURCE_PICCAP_STOPPING
} CaptureSource;
static CaptureSource capture_source;
static void *service_handle;
static bool (*call_once)(void *, const char *, const char *, Callback, void *, unsigned long *,
                         LSError *);
static bool (*cancel_call)(void *, unsigned long, LSError *);
static unsigned long piccap_token, media_token;
static uint64_t piccap_deadline, media_deadline;
static unsigned clear_samples, piccap_fallbacks;
static uint64_t clear_since, unknown_since, piccap_disconnected_since;
#define CLEAR_STABLE_MS 2000
#define UNKNOWN_GRACE_MS 1500
#define PICCAP_LOSS_MS 5000
#define PICCAP_FAILURE_COOLDOWN_MS 30000
static TargetConfig piccap_target;
static char piccap_error[128];
static bool piccap_request(int command);
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
static bool media_reply(void *h, void *m, void *ctx) {
    (void)h;
    (void)ctx;
    media_busy = false;
    media_token = 0;
    protection = playback_classify(&playback_json, payload(m));
    protection_at = milliseconds();
    if (protection == PLAYBACK_CLEAR) {
        if (!clear_samples)
            clear_since = protection_at;
        if (clear_samples < 3)
            clear_samples++;
        unknown_since = 0;
    } else {
        clear_samples = 0;
        clear_since = 0;
        if (protection == PLAYBACK_UNKNOWN && !unknown_since)
            unknown_since = protection_at;
        else if (protection == PLAYBACK_PROTECTED)
            unknown_since = 0;
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
            bool active = playback_json.boolean(running);
            piccap_connected = playback_true(&playback_json, root, "connected");
            if (active) {
                if (capture_source == SOURCE_PICCAP_STARTING)
                    capture_source = SOURCE_PICCAP;
                if (piccap_connected)
                    piccap_disconnected_since = 0;
                else if (!piccap_disconnected_since)
                    piccap_disconnected_since = milliseconds();
            } else if (capture_source != SOURCE_PICCAP_STARTING ||
                       milliseconds() - piccap_action_at >= 2000) {
                if (capture_source != SOURCE_PICCAP_STOPPING)
                    handover_cooldown = milliseconds() + 10000;
                piccap_owned = false;
                capture_source = SOURCE_HISTOGRAM;
                piccap_connected = false;
                piccap_disconnected_since = 0;
                if (handover_cooldown < milliseconds() + 3000)
                    handover_cooldown = milliseconds() + 3000;
                if (!retries.paused)
                    retries.due = milliseconds();
            }
        } else if (!piccap_installed()) {
            piccap_owned = false;
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
                capture_source = SOURCE_PICCAP_STOPPING;
                snprintf(piccap_error, sizeof(piccap_error), "PicCap endpoint request failed");
            }
        } else {
            capture_source = SOURCE_PICCAP_STOPPING;
            snprintf(piccap_error, sizeof(piccap_error), "Cannot read PicCap settings");
        }
    } else if (command == 4) {
        if (!ok) {
            capture_source = SOURCE_PICCAP_STOPPING;
            snprintf(piccap_error, sizeof(piccap_error), "PicCap rejected receiver settings");
        }
        /* setSettings restarts PicCap itself; status confirms ownership. */
    } else if (command == 1 && !ok) {
        capture_source = SOURCE_PICCAP_STOPPING;
        handover_cooldown = milliseconds() + 10000;
    }
    if (command >= 3 && capture_source == SOURCE_PICCAP_STOPPING)
        handover_cooldown = milliseconds() + 10000;
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
/* Return true while PicCap owns capture or a stop confirmation is pending. */
static bool handover_tick(void) {
    uint64_t now = milliseconds();
    if (media_busy && now >= media_deadline) {
        LSError e;
        error_init(&e);
        cancel_call(service_handle, media_token, &e);
        error_free(&e);
        media_busy = false;
        media_token = 0;
        protection = PLAYBACK_UNKNOWN;
        clear_samples = 0;
        clear_since = 0;
        if (!unknown_since)
            unknown_since = now;
    }
    if (piccap_busy && now >= piccap_deadline) {
        snprintf(piccap_error, sizeof(piccap_error), "PicCap request timed out");
        LSError e;
        error_init(&e);
        cancel_call(service_handle, piccap_token, &e);
        error_free(&e);
        piccap_busy = false;
        piccap_token = 0;
        capture_source = SOURCE_PICCAP_STOPPING;
        next_piccap_probe = 0;
    }
    if (handover_enabled && piccap_installed() && !retries.paused && playback_json.lib &&
        !media_busy && now >= next_media_probe) {
        LSError e;
        error_init(&e);
        media_busy = call_once(service_handle, "luna://com.webos.media/getActivePipelines", "{}",
                               media_reply, NULL, &media_token, &e);
        media_deadline = now + 3000;
        error_free(&e);
        next_media_probe = now + 1000;
    }
    if (protection == PLAYBACK_CLEAR && now - protection_at >= 3000) {
        protection = PLAYBACK_UNKNOWN;
        clear_samples = 0;
        clear_since = 0;
        if (!unknown_since)
            unknown_since = now;
    }
    bool session = !CALIBRATION_ACTIVE && handover_enabled && target_piccap_compatible(&target) &&
                   piccap_installed() && !retries.paused && !retries.blocked;
    bool clear = session && protection == PLAYBACK_CLEAR && now - protection_at < 3000;
    bool stable = clear && clear_samples >= 3 && now - clear_since >= CLEAR_STABLE_MS &&
                  now >= handover_cooldown;
    if (capture_source == SOURCE_HISTOGRAM && stable) {
        piccap_target = target;
        piccap_error[0] = 0;
        piccap_disconnected_since = now;
        release_capture(false);
        retries.due = 0;
        piccap_owned = true;
        capture_source = SOURCE_PICCAP_STARTING;
        if (!piccap_request(3)) {
            capture_source = SOURCE_PICCAP_STOPPING;
            handover_cooldown = now + PICCAP_FAILURE_COOLDOWN_MS;
        }
    }
    bool grace = protection == PLAYBACK_UNKNOWN && unknown_since &&
                 now - unknown_since < UNKNOWN_GRACE_MS && capture_source == SOURCE_PICCAP;
    bool lost = piccap_owned && capture_source == SOURCE_PICCAP && piccap_disconnected_since &&
                now - piccap_disconnected_since >= PICCAP_LOSS_MS;
    if (lost) {
        snprintf(piccap_error, sizeof(piccap_error),
                 "PicCap receiver disconnected; histogram fallback");
        piccap_fallbacks++;
        handover_cooldown = now + PICCAP_FAILURE_COOLDOWN_MS;
    }
    if (piccap_owned &&
        (!session || (!clear && !grace) || lost || strcmp(piccap_target.host, target.host) ||
         piccap_target.piccap_port != target.piccap_port) &&
        capture_source != SOURCE_PICCAP_STOPPING) {
        capture_source = SOURCE_PICCAP_STOPPING;
        next_piccap_probe = 0;
    }
    if (piccap_owned && !piccap_busy && now >= next_piccap_probe) {
        if (capture_source == SOURCE_PICCAP_STOPPING)
            piccap_request(2);
        else
            piccap_request(0);
        next_piccap_probe = now + 1000;
    }
    return piccap_owned;
}
#endif
