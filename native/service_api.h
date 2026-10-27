/* Luna API handlers. Included after the shared runtime context. */
#include "service_calibration.h"
static bool start_failure(void *h, void *m, const char *message) {
    retry_update(&retries, boot_enabled() && geteuid() == 0, false, milliseconds());
    char json[512];
    snprintf(
        json, sizeof(json),
        "{\"returnValue\":false,\"errorText\":\"%s\",\"retryScheduled\":%s,\"retryBlocked\":%s}",
        message, retries.due ? "true" : "false", retries.blocked ? "true" : "false");
    return respond(h, m, json);
}
static bool start(void *h, void *m, void *ctx) {
    (void)ctx;
    const char *p = m ? payload(m) : NULL;
    if (p && strcmp(p, "{}") != 0)
        return respond(h, m,
                       "{\"returnValue\":false,\"errorText\":\"Start expects an empty object; use "
                       "settings to configure the receiver\"}");
    if (m)
        retry_reset(&retries);
    if (piccap_owned)
        return respond(h, m, "{\"returnValue\":true,\"running\":true,\"activeSource\":\"PicCap\"}");
    if (capture_ctx.handle)
        return respond(h, m, "{\"returnValue\":true,\"running\":true}");
    if (!playback_json.lib) {
        set_error("JSON-C is unavailable; cannot start receiver output");
        return start_failure(h, m, "JSON-C is unavailable");
    }
    capture_ctx.frames = 0;
    capture_ctx.revoked = 0;
    last_error[0] = 0;
    capture_ctx.diagnostic[0] = 0;
    if (receiver_runtime.starting)
        return respond(h, m, "{\"returnValue\":true,\"connecting\":true}");
    receiver_runtime.clear_pending = false;
    if (!receiver_begin(milliseconds())) {
        set_error(receiver_error());
        return start_failure(h, m, receiver_error());
    }
    receiver_runtime.starting = true;
    return respond(h, m, "{\"returnValue\":true,\"connecting\":true}");
}
static bool initialize_capture(void *h, void *m) {
    if (!enter_developer_mode()) {
        stop_capture();
        set_error("Developer initialization needs root. Elevate this own service through Homebrew "
                  "Channel.");
        return start_failure(h, m,
                             "Root access is required for temporary LG developer initialization");
    }
    capture_ctx.handle = vt_init(policy);
    if (!leave_developer_mode()) {
        stop_capture();
        retries.blocked = true;
        set_error("Developer marker cleanup failed; manual intervention required");
        return start_failure(h, m, "Developer marker cleanup failed");
    }
    if (!capture_ctx.handle) {
        stop_capture();
        set_error("LG refused histogram initialization");
        return start_failure(
            h, m,
            "LG refused histogram initialization. Another capture source may hold VT_CAPTURE.");
    }
    if (vt_preset(capture_ctx.handle, 0) != 0) {
        stop_capture();
        set_error("LG histogram preset initialization failed");
        return start_failure(h, m, "Histogram preset failed");
    }
    capture_ctx.preset = capture_ctx.letterbox_mode = capture_ctx.settling = 0;
    capture_ctx.pending_letterbox = -1;
    capture_ctx.next_border_probe = 0;
    retries.due = 0;
    retries.attempts = 0;
    retries.quick_retries = 0;
    if (capture_ctx.recovery_started) {
        capture_ctx.last_recovery_ms = milliseconds() - capture_ctx.recovery_started;
        capture_ctx.recovery_started = 0;
    }
    return respond(h, m, "{\"returnValue\":true,\"running\":true}");
}
static bool stop(void *h, void *m, void *ctx) {
    (void)ctx;
    calibration_ctx.until = 0;
    retry_pause(&retries);
    bool bridging = bridge_phase != BRIDGE_IDLE;
    stream_close(milliseconds());
    bridge_phase = BRIDGE_IDLE;
    memset(&capture_ctx.continuity, 0, sizeof(capture_ctx.continuity));
    stop_capture();
    if (bridging && !receiver_ready()) {
        receiver_begin(milliseconds());
        receiver_runtime.clear_pending = true;
    }
    return respond(h, m, "{\"returnValue\":true,\"running\":false}");
}
static bool autostart(void *h, void *m, void *ctx) {
    (void)ctx;
    const char *p = m ? payload(m) : NULL;
    bool enabled;
    if (p && strcmp(p, "{\"enabled\":true}") == 0)
        enabled = true;
    else if (p && strcmp(p, "{\"enabled\":false}") == 0)
        enabled = false;
    else
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Expected enabled boolean\"}");
    if (geteuid() != 0)
        return respond(
            h, m,
            "{\"returnValue\":false,\"rootRequired\":true,\"errorText\":\"Root access is required "
            "to change autostart. Select Enable Root Access in Overview.\"}");
    if (!set_boot(enabled))
        return respond(
            h, m,
            "{\"returnValue\":false,\"errorText\":\"Cannot update the HyperDRM boot hook. Check "
            "the installed boot script and whether another file occupies the hook path.\"}");
    if (enabled)
        retry_reset(&retries);
    retry_update(&retries, enabled, capture_ctx.handle != NULL, milliseconds());
    return respond(h, m,
                   enabled ? "{\"returnValue\":true,\"autostart\":true}"
                           : "{\"returnValue\":true,\"autostart\":false}");
}
static bool restart_service(void *h, void *m, void *ctx) {
    (void)ctx;
    if (strcmp(payload(m), "{}"))
        return respond(h, m,
                       "{\"returnValue\":false,\"errorText\":\"Restart expects an empty object\"}");
    bool ok = respond(h, m, "{\"returnValue\":true,\"restarting\":true}");
    quitting = 1;
    return ok;
}
static bool handover(void *h, void *m, void *ctx) {
    (void)ctx;
    const char *p = payload(m);
    bool enabled;
    if (strcmp(p, "{\"enabled\":true}") == 0)
        enabled = true;
    else if (strcmp(p, "{\"enabled\":false}") == 0)
        enabled = false;
    else
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Expected enabled boolean\"}");
    if (!playback_json.lib || geteuid() != 0)
        return respond(
            h, m, "{\"returnValue\":false,\"errorText\":\"Root service and JSON-C are required\"}");
    if (enabled && !piccap_installed())
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"PicCap is not installed\"}");
    if (!handover_save(enabled))
        return respond(
            h, m,
            "{\"returnValue\":false,\"errorText\":\"Could not save PicCap handover preference\"}");
    handover_enabled = enabled;
    protection = PLAYBACK_UNKNOWN;
    clear_samples = 0;
    clear_since = unknown_since = 0;
    next_media_probe = 0;
    return respond(h, m, "{\"returnValue\":true}");
}
static bool settings(void *h, void *m, void *ctx) {
    (void)ctx;
    TargetConfig next = target;
    if (!playback_json.lib || !target_parse(&playback_json, payload(m), &next))
        return respond(h, m,
                       "{\"returnValue\":false,\"errorText\":\"Enter a valid IPv4/IPv6 address, "
                       "integer port (1-65535), receiver and optional API token\"}");
    if (!target_save(TARGET_PREF, &next))
        return respond(
            h, m, "{\"returnValue\":false,\"errorText\":\"Could not save receiver settings\"}");
    bool active = capture_ctx.handle || receiver_runtime.starting || piccap_owned || retries.due;
    stop_capture();
    stream_close(milliseconds());
    bridge_phase = BRIDGE_IDLE;
    memset(&capture_ctx.continuity, 0, sizeof(capture_ctx.continuity));
    test_close();
    connection_test.stage = TEST_IDLE;
    target = next;
    last_error[0] = 0;
    protection = PLAYBACK_UNKNOWN;
    clear_samples = 0;
    clear_since = unknown_since = 0;
    if (active && !retries.paused && !retries.blocked)
        retries.due = milliseconds();
    return respond(h, m, "{\"returnValue\":true}");
}
static bool test_connection(void *h, void *m, void *ctx) {
    (void)ctx;
    if (strcmp(payload(m), "{}") || !playback_json.lib)
        return respond(
            h, m,
            "{\"returnValue\":false,\"errorText\":\"Test requires an empty object and JSON-C\"}");
    if (!test_begin(&target, milliseconds()))
        return respond(
            h, m, "{\"returnValue\":false,\"errorText\":\"A connection test is already running\"}");
    return respond(h, m, "{\"returnValue\":true,\"testing\":true}");
}
#include "service_status.h"
