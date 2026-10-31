/* Read-only runtime status serialization. */
static bool status(void *h, void *m, void *ctx) {
    (void)ctx;
    char json[12288], safe[512], endpoint[80];
    size_t n = 0;
    snprintf(endpoint, sizeof(endpoint), strchr(target.host, ':') ? "[%s]:%u" : "%s:%u",
             target.host, target.port);
    for (size_t i = 0; last_error[i] && n + 2 < sizeof(safe); i++) {
        char c = last_error[i];
        if (c == '"' || c == '\\')
            safe[n++] = '\\';
        if ((unsigned char)c >= 32)
            safe[n++] = c;
    }
    safe[n] = 0;
    uint64_t now = milliseconds();
    char report[512];
    size_t k = 0;
    for (size_t i = 0; calibration_ctx.message[i] && k + 2 < sizeof(report); i++) {
        char c = calibration_ctx.message[i];
        if (c == '"' || c == '\\')
            report[k++] = '\\';
        if ((unsigned char)c >= 32)
            report[k++] = c;
    }
    report[k] = 0;
    unsigned spatial_rectangles = 0;
    if (calibration_rectangles())
        calibration_blocks(&spatial_rectangles);
    int used = snprintf(
        json, sizeof(json),
        "{\"receiverConnecting\":%s,\"calibrationProfile\":\"%s\",\"effectiveCalibration\":\"%s\","
        "\"tvModel\":\"%s\",\"customModel\":\"%s\",\"customPoints\":[",
        receiver_runtime.starting ? "true" : "false", calibration_key(calibration.kind),
        calibration_key(calibration_effective()), tv_model, calibration.model);
    for (unsigned i = 0; i < calibration.count; i++)
        used +=
            snprintf(json + used, sizeof(json) - used, "%s{\"position\":%.12g,\"region\":%u}",
                     i ? "," : "", calibration.points[i].position, calibration.points[i].region);
    used += snprintf(json + used, sizeof(json) - used, "],\"customRectangles\":[");
    for (unsigned i = 0; i < calibration.rect_count && used + 96 < (int)sizeof(json); i++)
        used += snprintf(json + used, sizeof(json) - used,
                         "%s{\"region\":%u,\"x0\":%.6g,\"y0\":%.6g,\"x1\":%.6g,\"y1\":%.6g}",
                         i ? "," : "", calibration.rects[i].region, calibration.rects[i].x0,
                         calibration.rects[i].y0, calibration.rects[i].x1, calibration.rects[i].y1);
    used += snprintf(json + used, sizeof(json) - used, "],\"customRectangleCount\":%u,\"spatialRectangleCount\":%u,"
                                                             "\"customGeometry\":\"%s\","
                     "\"spatialGeometry\":\"%s\",\"customCompensate\":%s,"
                     "\"colorStat\":\"%s\",\"grade\":\"%s\",\"customHasReference\":%s,"
                     "\"layoutUpdates\":%u,"
                     "\"lastLayoutCheckAgeMs\":%lld,\"layoutError\":\"%s\","
                     "\"piccapFallbacks\":%u,\"piccapCooldownMs\":%llu,",
                     calibration.rect_count, spatial_rectangles,
                     calibration_geometry_name(),
                     calibration_spatial_geometry_name(),
                     calibration.compensate ? "true" : "false",
                     calibration_stat_key(calibration.color_stat),
                     color_grade_key(calibration.grade),
                     calibration.has_reference ? "true" : "false", layout_updates,
                     last_layout_check ? (long long)(now - last_layout_check) : -1, layout_error,
                     piccap_fallbacks,
                     (unsigned long long)(handover_cooldown > now ? handover_cooldown - now : 0));
    used += snprintf(json + used, sizeof(json) - used, "\"customBlack\":[");
    for (unsigned i = 0; calibration.has_reference && i < 16 && used + 24 < (int)sizeof(json); i++)
        used += snprintf(json + used, sizeof(json) - used, "%s[%u,%u,%u]", i ? "," : "",
                         calibration.black[i][0], calibration.black[i][1], calibration.black[i][2]);
    used += snprintf(json + used, sizeof(json) - used, "],\"customWhite\":[");
    for (unsigned i = 0; calibration.has_reference && i < 16 && used + 24 < (int)sizeof(json); i++)
        used += snprintf(json + used, sizeof(json) - used, "%s[%u,%u,%u]", i ? "," : "",
                         calibration.white[i][0], calibration.white[i][1], calibration.white[i][2]);
    used += snprintf(json + used, sizeof(json) - used, "],");
    used += snprintf(json + used, sizeof(json) - used, "\"histIds\":[");
    for (unsigned i = 0; i < 16 && used + 12 < (int)sizeof(json); i++)
        used += snprintf(json + used, sizeof(json) - used, "%s%u", i ? "," : "",
                         capture_ctx.hist_ids[i]);
    used += snprintf(json + used, sizeof(json) - used, "],");
    used += snprintf(json + used, sizeof(json) - used, "\"calibrationMessage\":\"%s\",", report);
    used += snprintf(json + used, sizeof(json) - used,
                     "\"calibrationDraft\":{\"model\":\"%s\",\"geometry\":\"%s\",\"points\":[",
                     calibration_ctx.draft.model,
                     calibration_ctx.draft.smooth_render
                         ? "smooth"
                         : calibration_ctx.draft.rect_geometry ? "rectangles" : "rim");
    for (unsigned i = 0; i < calibration_ctx.draft.count; i++)
        used += snprintf(json + used, sizeof(json) - used, "%s{\"position\":%.12g,\"region\":%u}",
                         i ? "," : "", calibration_ctx.draft.points[i].position,
                         calibration_ctx.draft.points[i].region);
    used += snprintf(json + used, sizeof(json) - used, "],\"rectangles\":[");
    for (unsigned i = 0; i < calibration_ctx.draft.rect_count && used + 96 < (int)sizeof(json); i++)
        used += snprintf(json + used, sizeof(json) - used,
                         "%s{\"region\":%u,\"x0\":%.6g,\"y0\":%.6g,\"x1\":%.6g,\"y1\":%.6g}",
                         i ? "," : "", calibration_ctx.draft.rects[i].region,
                         calibration_ctx.draft.rects[i].x0, calibration_ctx.draft.rects[i].y0,
                         calibration_ctx.draft.rects[i].x1, calibration_ctx.draft.rects[i].y1);
    used += snprintf(json + used, sizeof(json) - used, "],\"compensate\":%s",
                     calibration_ctx.draft.compensate ? "true" : "false");
    used += snprintf(json + used, sizeof(json) - used, ",\"colorStat\":\"%s\"",
                     calibration_stat_key(calibration_ctx.draft.color_stat));
    if (calibration_ctx.draft.has_reference) {
        used += snprintf(json + used, sizeof(json) - used, ",\"black\":[");
        for (unsigned i = 0; i < 16 && used + 24 < (int)sizeof(json); i++)
            used += snprintf(json + used, sizeof(json) - used, "%s[%u,%u,%u]", i ? "," : "",
                             calibration_ctx.draft.black[i][0], calibration_ctx.draft.black[i][1],
                             calibration_ctx.draft.black[i][2]);
        used += snprintf(json + used, sizeof(json) - used, "],\"white\":[");
        for (unsigned i = 0; i < 16 && used + 24 < (int)sizeof(json); i++)
            used += snprintf(json + used, sizeof(json) - used, "%s[%u,%u,%u]", i ? "," : "",
                             calibration_ctx.draft.white[i][0], calibration_ctx.draft.white[i][1],
                             calibration_ctx.draft.white[i][2]);
        used += snprintf(json + used, sizeof(json) - used, "]");
    }
    used += snprintf(json + used, sizeof(json) - used, "},\"receiverStreamConnected\":%s,",
                     stream.stage == STREAM_SUBSCRIBED ? "true" : "false");
    used += snprintf(json + used, sizeof(json) - used,
                     "\"sampleSequence\":%u,\"sampleAgeMs\":%lld,\"holdingColors\":%s,"
                     "\"sourceFade\":%s,\"calibrating\":%s,",
                     capture_ctx.sample_sequence,
                     capture_ctx.sample_at ? (long long)(now - capture_ctx.sample_at) : -1,
                     capture_ctx.continuity.holding ? "true" : "false",
                     (capture_ctx.continuity.fading || bridge_phase == BRIDGE_FADING) ? "true"
                                                                                      : "false",
                     CALIBRATION_ACTIVE ? "true" : "false");
    used +=
        snprintf(json + used, sizeof(json) - used,
                 "\"returnValue\":true,\"captureIntervalMs\":%u,\"piccapPort\":%u,\"connectionTest\":{\"state\":\"%s\","
                 "\"ledCount\":%u,\"elapsedMs\":%llu,\"error\":\"%s\"},\"lastOutputAgeMs\":%"
                 "lld,\"retryInMs\":%llu,\"captureState\":\"%s\",\"piccapError\":\"%s\",",
                 target_capture_interval(&target), target.piccap_port, test_stage_name(), connection_test.leds,
                 (unsigned long long)connection_test.elapsed, connection_test.error,
                 capture_ctx.last_output_at ? (long long)(now - capture_ctx.last_output_at) : -1,
                 (unsigned long long)(retries.due > now ? retries.due - now : 0),
                 capture_source == SOURCE_PICCAP_STARTING   ? "Starting PicCap"
                 : capture_source == SOURCE_PICCAP_STOPPING ? "Stopping PicCap"
                 : capture_source != SOURCE_HISTOGRAM       ? "PicCap"
                 : capture_ctx.handle                       ? "Histogram"
                                                            : "Stopped",
                 piccap_error);
    used += snprintf(
        json + used, sizeof(json) - used,
        "\"version\":\"" HYPERDRM_VERSION
        "\",\"running\":%s,\"colorValid\":%s,\"frames\":%lu,\"captureRecoveries\":%u,"
        "\"lastRecoveryMs\":%llu,\"endpoint\":\"%s\",\"host\":\"%s\",\"port\":%u,"
        "\"receiver\":\"%s\",\"output\":\"%s\",\"flatbufferConnected\":%s,"
        "\"tokenSet\":%s,\"piccapTargetCompatible\":%s,"
        "\"priority\":%d,\"uid\":%u,"
        "\"outputColor\":[%u,%u,%u],\"error\":\"%s\",\"regions\":[",
        (capture_ctx.handle || capture_source == SOURCE_PICCAP) ? "true" : "false",
        capture_ctx.color_valid ? "true" : "false", capture_ctx.frames, capture_ctx.recoveries,
        (unsigned long long)capture_ctx.last_recovery_ms, endpoint, target.host, target.port,
        target.receiver, target.output,
        target_flatbuffer(&target) && receiver_runtime.flatbuffer.stage == FB_READY ? "true"
                                                                                   : "false",
        target.token[0] ? "true" : "false",
        target_piccap_compatible(&target) ? "true" : "false", PRIORITY, (unsigned)geteuid(),
        capture_ctx.sent_rgb[0], capture_ctx.sent_rgb[1], capture_ctx.sent_rgb[2], safe);
    for (int i = 0; i < 16; i++)
        used += snprintf(json + used, sizeof(json) - used, "%s[%u,%u,%u]", i ? "," : "",
                         capture_ctx.rgb[i][0], capture_ctx.rgb[i][1], capture_ctx.rgb[i][2]);
    used += snprintf(json + used, sizeof(json) - used,
                     "],\"ledCount\":%u,\"spatialOutput\":%s,\"blackPercent\":[",
                     receiver_layout.count, receiver_layout.count ? "true" : "false");
    for (int i = 0; i < 16; i++)
        used += snprintf(json + used, sizeof(json) - used, "%s%u", i ? "," : "",
                         capture_ctx.black_percent[i]);
    /* Which measurement each plane cell came from. A zero here means that window
     * never wins its own area, which is what a superset window does. */
    used += snprintf(json + used, sizeof(json) - used, "],\"edgeOwnership\":[");
    if (used > 0 && used < (int)sizeof(json)) {
        const unsigned *ownership = edge_ownership_cached();
        for (int i = 0; i < 16; i++)
            used += snprintf(json + used, sizeof(json) - used, "%s%u", i ? "," : "",
                             ownership[i]);
    }
    used += snprintf(json + used, sizeof(json) - used,
             "],\"preset\":%u,"
             "\"spatialInterpolation\":%s,\"blackBarDetection\":true,\"autostart\":%s,"
             "\"retryScheduled\":%s,\"retryPaused\":%s,\"retryBlocked\":%s,\"retryAttempts\":%u,"
             "\"piccapInstalled\":%s,\"piccapHandover\":%s,\"piccapConnected\":%s,\"activeSource\":"
             "\"%s\",\"captureHold\":\"%s\",\"playback\":\"%s\","
             "\"playbackReplyAgeMs\":%lld,\"letterboxDetected\":%s,"
             /* Requested versus actual feed cadence, and where a slow tick goes:
              * vtGet is LG's histogram read, render is plane build, send is the
              * socket write. A feed that feels late shows up here first. */
             "\"captureWanted\":%s,\"recoveryAttempts\":%u,\"vtGetMs\":%u,\"renderMs\":%u,"
             "\"sendMs\":%u,\"tickMs\":%u,\"sampleGapMs\":%u,\"outputGapMs\":%u,"
             "\"sceneCuts\":%u,\"sceneCutDistance\":%u,"
             "\"sampleFps\":%u,\"outputFps\":%u,\"receiverStage\":\"%s\",\"outputRegions\":[",
             capture_ctx.preset, receiver_layout.interpolation ? "true" : "false",
             boot_enabled() ? "true" : "false", retries.due ? "true" : "false",
             retries.paused ? "true" : "false", retries.blocked ? "true" : "false",
             retries.attempts, piccap_installed() ? "true" : "false",
             handover_enabled ? "true" : "false", piccap_connected ? "true" : "false",
             handover_yield() ? "PicCap" : "HyperDRM",
             piccap_running ? "PicCap" : "",
             playback == PLAYBACK_PROTECTED ? "protected"
             : playback == PLAYBACK_CLEAR   ? "clear"
                                            : "unknown",
             playback_at ? (long long)(now - playback_at) : -1,
             capture_ctx.handle && capture_ctx.letterbox_mode ? "true" : "false",
             capture_ctx.wanted ? "true" : "false", capture_ctx.recovery_attempts,
             capture_ctx.vt_get_us / 1000, capture_ctx.plane_us / 1000,
             capture_ctx.send_us / 1000, capture_ctx.tick_us / 1000, capture_ctx.sample_gap_ms,
             capture_ctx.output_gap_ms,
             capture_ctx.scene_cuts, capture_ctx.scene_cut_distance,
             capture_ctx.sample_gap_ms ? 1000 / capture_ctx.sample_gap_ms : 0,
             capture_ctx.output_gap_ms ? 1000 / capture_ctx.output_gap_ms : 0,
             target_flatbuffer(&target)
                 ? (receiver_runtime.flatbuffer.stage == FB_READY        ? "ready"
                    : receiver_runtime.flatbuffer.stage == FB_CONNECTING ? "connecting"
                    : receiver_runtime.flatbuffer.stage == FB_REGISTERING
                        ? "registering"
                    : receiver_runtime.flatbuffer.stage == FB_FAILED ? "failed"
                                                                     : "idle")
                 : (receiver_runtime.output.stage == OUTPUT_READY  ? "ready"
                    : receiver_runtime.output.stage == OUTPUT_FAILED ? "failed"
                                                                     : "connecting"));
    for (int i = 0; i < 16 && used > 0 && used < (int)sizeof(json); i++)
        used += snprintf(json + used, sizeof(json) - used, "%s[%u,%u,%u]", i ? "," : "",
                         capture_ctx.rendered[i][0], capture_ctx.rendered[i][1],
                         capture_ctx.rendered[i][2]);
    used += snprintf(json + used, sizeof(json) - used, "]}");
    return respond(h, m, json);
}
