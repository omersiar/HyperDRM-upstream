/* Capture scheduler and histogram processing. */
static int tick(void *unused) {
    (void)unused;
    uint64_t now = milliseconds();
    unsigned long acknowledged = receiver_frames();
    receiver_tick(&playback_json, now);
    if (receiver_failed() && (capture_ctx.handle || receiver_runtime.starting)) {
        char error[128];
        snprintf(error, sizeof(error), "%s", receiver_error());
        release_capture(false);
        set_error(error);
        retry_update(&retries, boot_enabled() && geteuid() == 0, false, now);
        retry_failure(&retries, now);
    }
    if (receiver_failed() && receiver_runtime.clear_pending) {
        receiver_runtime.clear_pending = false;
        receiver_close();
    }
    if (receiver_runtime.starting && (piccap_owned || capture_source != SOURCE_HISTOGRAM)) {
        receiver_runtime.starting = false;
        receiver_close();
    }
    if (receiver_frames() > acknowledged && capture_ctx.handle) {
        capture_ctx.frames += receiver_frames() - acknowledged;
        capture_ctx.last_output_at = now;
        last_error[0] = 0;
    }
    if (receiver_runtime.clear_pending && receiver_ready()) {
        receiver_runtime.clear_pending = false;
        receiver_stop(now, true);
    } else if (receiver_runtime.starting && receiver_ready()) {
        free(receiver_layout.blends);
        receiver_layout.blends = receiver_runtime.output.mapping;
        receiver_runtime.output.mapping = NULL;
        receiver_layout.count = receiver_runtime.output.count;
        receiver_runtime.starting = false;
        initialize_capture(NULL, NULL);
        if (!capture_ctx.handle)
            retry_failure(&retries, now);
    }
    model_tick(now);
    test_tick(&playback_json, now);
    layout_refresh_tick(&playback_json, now, capture_ctx.handle || capture_source == SOURCE_PICCAP,
                        &receiver_layout);
    stream_tick(&playback_json, now, handover_enabled && !retries.paused && !retries.blocked);
    bool owned = handover_tick();
    bridge_tick(now, &capture_ctx);
    if (owned)
        return 1;
    if (!capture_ctx.handle) {
        if (receiver_runtime.starting || receiver_runtime.clear_pending || receiver_closing())
            return 1;
        retry_update(&retries, boot_enabled() && geteuid() == 0, false, milliseconds());
        if (retry_take(&retries, milliseconds())) {
            start(NULL, NULL, NULL);
            if (!capture_ctx.handle && !receiver_runtime.starting)
                retry_failure(&retries, milliseconds());
        }
        return 1;
    }
    if (capture_ctx.revoked) {
        set_error("LG revoked capture; recovering after source transition");
        capture_ctx.recoveries++;
        capture_ctx.recovery_started = milliseconds();
        release_capture(false);
        retry_source_transition(&retries, milliseconds());
        return 1;
    }
    if (capture_ctx.settling) {
        capture_ctx.settling--;
        return 1;
    }
    if (!CALIBRATION_ACTIVE && capture_ctx.letterbox_mode && capture_ctx.preset == 1 &&
        milliseconds() >= capture_ctx.next_border_probe) {
        if (vt_preset(capture_ctx.handle, 0) != 0) {
            set_error("Border probe preset failed");
            stop_capture();
            return 1;
        }
        capture_ctx.preset = 0;
        capture_ctx.settling = 2;
        return 1;
    }
    if (capture_ctx.sample_at && now - capture_ctx.sample_at < target_capture_interval(&target))
        return 1;
    Histogram hist[16] = {0};
    int rc = vt_get(capture_ctx.handle, hist);
    bool populated = !rc && histogram_populated(hist);
    if (!populated) {
        unsigned char held[16][3];
        capture_ctx.color_valid = false;
        if (!continuity_frame(&capture_ctx.continuity, false, capture_ctx.rgb, now, held)) {
            set_error("Histogram data unavailable beyond hold window; recovering capture");
            release_capture(false);
            retry_source_transition(&retries, now);
            return 1;
        }
        unsigned hold_duration =
            (unsigned)(1500 - (now - capture_ctx.continuity.missing_at));
        OutputFrame frame =
            frame_build(held, receiver_layout.blends, receiver_layout.count, hold_duration);
        bool failed = !frame_send(&frame, hold_duration, now);
        frame_release(&frame);
        if (failed) {
            set_error("Receiver connection lost while holding colors");
            stop_capture();
            return 1;
        }
        return 1;
    }
    capture_ctx.sample_at = now;
    capture_ctx.sample_sequence++;
    unsigned aggregate[3] = {0};
    for (int i = 0; i < 16; i++) {
        capture_ctx.black_percent[i] = 100;
        for (int c = 0; c < 3; c++) {
            uint64_t total = 0, black = 0;
            for (int b = 0; b < 256; b++) {
                total += hist[i].bins[c][b];
                if (b <= 12)
                    black += hist[i].bins[c][b];
            }
            unsigned fraction = total ? (unsigned)(100 * black / total) : 0;
            if (fraction < capture_ctx.black_percent[i])
                capture_ctx.black_percent[i] = fraction;
        }
    }
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 3; c++) {
            capture_ctx.rgb[i][c] = histogram_mean(hist[i].bins[c]);
            aggregate[c] += capture_ctx.rgb[i][c];
        }
    if (!CALIBRATION_ACTIVE && capture_ctx.preset == 0) {
        int candidate = letterbox_candidate(capture_ctx.black_percent, capture_ctx.rgb);
        uint64_t now = milliseconds();
        if (candidate < 0)
            capture_ctx.pending_letterbox = -1;
        else if (candidate != capture_ctx.pending_letterbox) {
            capture_ctx.pending_letterbox = candidate;
            capture_ctx.candidate_since = now;
        } else if ((unsigned)candidate != capture_ctx.letterbox_mode &&
                   now - capture_ctx.candidate_since >= 2000)
            capture_ctx.letterbox_mode = (unsigned)candidate;
        if (capture_ctx.letterbox_mode) {
            if (vt_preset(capture_ctx.handle, 1) != 0) {
                set_error("Letterbox preset failed");
                stop_capture();
                return 1;
            }
            capture_ctx.preset = 1;
            capture_ctx.settling = 2;
            capture_ctx.next_border_probe = now + 2000;
            return 1;
        }
    }
    for (int c = 0; c < 3; c++)
        capture_ctx.sent_rgb[c] = (unsigned char)(aggregate[c] / 16);
    dark_color_filter(capture_ctx.sent_rgb);
    unsigned char boosted_mean[3];
    boost_color(capture_ctx.sent_rgb, boosted_mean);
    memcpy(capture_ctx.sent_rgb, boosted_mean, 3);
    unsigned char mapped_rgb[16][3], stable_rgb[16][3];
    calibration_colors(capture_ctx.rgb, mapped_rgb);
    for (unsigned i = 0; i < 16; i++)
        dark_color_filter(mapped_rgb[i]);
    continuity_frame(&capture_ctx.continuity, true, mapped_rgb, now, stable_rgb);
    receiver_layout.interpolation = receiver_layout.count && calibration_effective() != CAL_AVERAGE;
    OutputFrame frame = frame_build(stable_rgb, receiver_layout.blends, receiver_layout.count, 1500);
    int failed = !frame_send(&frame, 1500, now);
    frame_release(&frame);
    if (failed) {
        set_error("Receiver connection lost");
        stop_capture();
        return 1;
    }
    capture_ctx.color_valid = true;
    return 1;
}
