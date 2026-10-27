/* Calibration session, proposals, saved profiles and model discovery. */
static bool calibration_session(void *h, void *m, void *ctx) {
    (void)ctx;
    const char *p = payload(m);
    if (!strcmp(p, "{\"enabled\":true}")) {
        calibration_ctx.until = milliseconds() + 110000;
        if (capture_ctx.handle && vt_preset(capture_ctx.handle, 0) != 0) {
            calibration_ctx.until = 0;
            return respond(h, m,
                           "{\"returnValue\":false,\"errorText\":\"Cannot select full-screen "
                           "calibration capture\"}");
        }
        capture_ctx.preset = capture_ctx.letterbox_mode = 0;
        capture_ctx.settling = 2;
        capture_ctx.pending_letterbox = -1;
    } else if (!strcmp(p, "{\"enabled\":false}"))
        calibration_ctx.until = 0;
    else
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Expected enabled boolean\"}");
    return respond(h, m, "{\"returnValue\":true}");
}
static bool report_calibration(void *h, void *m, void *ctx) {
    if (!playback_json.lib)
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"JSON-C is unavailable\"}");
    (void)ctx;
    void *root = playback_json.parse(payload(m)),
         *message = playback_field(&playback_json, root, "message");
    bool valid = message && playback_json.type(message) == 6 &&
                 strlen(playback_json.string(message)) < sizeof(calibration_ctx.message);
    if (valid)
        snprintf(calibration_ctx.message, sizeof(calibration_ctx.message), "%s",
                 playback_json.string(message));
    if (root)
        playback_json.put(root);
    return respond(
        h, m,
        valid ? "{\"returnValue\":true}"
              : "{\"returnValue\":false,\"errorText\":\"Invalid calibration result message\"}");
}
static bool draft_calibration(void *h, void *m, void *ctx) {
    (void)ctx;
    Calibration next = {0};
    if (!playback_json.lib || !calibration_parse(&playback_json, payload(m), &next) ||
        next.kind != CAL_CUSTOM || next.count < 4 || strcmp(next.model, tv_model))
        return respond(h, m,
                       "{\"returnValue\":false,\"errorText\":\"Invalid measured model profile\"}");
    calibration_ctx.draft = next;
    return respond(h, m, "{\"returnValue\":true}");
}
static bool model_reply(void *h, void *m, void *ctx) {
    (void)h;
    (void)ctx;
    calibration_ctx.model_busy = false;
    calibration_ctx.model_token = 0;
    void *root = playback_json.parse(payload(m)),
         *model = playback_field(&playback_json, root, "modelName");
    if (playback_true(&playback_json, root, "returnValue") && model &&
        playback_json.type(model) == 6 && calibration_model_valid(playback_json.string(model))) {
        snprintf(tv_model, sizeof(tv_model), "%s", playback_json.string(model));
        calibration_generation++;
        layout_refresh_reset(milliseconds());
    }
    if (root)
        playback_json.put(root);
    return true;
}
static void model_tick(uint64_t now) {
    if (calibration_ctx.model_busy && now >= calibration_ctx.model_deadline) {
        LSError e;
        error_init(&e);
        cancel_call(service_handle, calibration_ctx.model_token, &e);
        error_free(&e);
        calibration_ctx.model_busy = false;
        calibration_ctx.model_token = 0;
    }
    if (!tv_model[0] && !calibration_ctx.model_busy && now >= calibration_ctx.next_model_probe) {
        LSError e;
        error_init(&e);
        calibration_ctx.model_busy = call_once(
            service_handle, "luna://com.webos.service.tv.systemproperty/getSystemInfo",
            "{\"keys\":[\"modelName\"]}", model_reply, NULL, &calibration_ctx.model_token, &e);
        error_free(&e);
        calibration_ctx.model_deadline = now + 3000;
        calibration_ctx.next_model_probe = now + 60000;
    }
}
static bool set_calibration(void *h, void *m, void *ctx) {
    (void)ctx;
    Calibration next = calibration;
    if (!playback_json.lib || !calibration_parse(&playback_json, payload(m), &next))
        return respond(
            h, m,
            "{\"returnValue\":false,\"errorText\":\"Select a valid profile; custom profiles need a "
            "model and 2-16 sorted rim points (position 0-4, region 0-15). Optional rectangles "
            "need region 0-15, x0<x1, y0<y1 inside 0-1, and no duplicate region\"}");
    if (!calibration_save(CALIBRATION_PREF, &next))
        return respond(h, m,
                       "{\"returnValue\":false,\"errorText\":\"Cannot save calibration profile\"}");
    calibration = next;
    calibration_generation++;
    layout_refresh_reset(milliseconds());
    return respond(h, m, "{\"returnValue\":true}");
}
