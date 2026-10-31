/* Capture scheduler and histogram processing. */
#define RECOVERY_LATCH_MS 5000
/* The loop now wakes every few milliseconds, so the gate only needs to absorb
 * that jitter. A large tolerance was quietly raising the configured rate. */
#define SAMPLE_TOLERANCE_MS 2
static int tick(void *unused) {
    (void)unused;
    uint64_t now = milliseconds();
    uint64_t tick_started = microseconds();
    /* Preset 1 is the letterboxed window set, so the picture-relative windows
     * move even though the screen coordinates do not. Whether a letterbox exists
     * stays the service's own multi-region detection: a dark scene looks like a
     * bar to a single probe. When there is one, r14 measures its height. */
    if (capture_ctx.letterbox_mode) {
        double bar = layout_bar_height(capture_ctx.detect_rgb[14][1]);
        if (bar >= 0.0)
            layout_set_bar(bar);
        else
            layout_set_letterbox(true);
    } else {
        layout_set_bar(0.0);
    }
    unsigned long acknowledged = receiver_frames();
    receiver_tick(&playback_json, now);
    if (receiver_failed() && (capture_ctx.handle || receiver_runtime.starting)) {
        char error[128];
        snprintf(error, sizeof(error), "%s", receiver_error());
        release_capture(false);
        set_error(error);
        /* A failed connection must also drop the start latch. That latch gates
         * the recovery branch below, so leaving it set stranded the service
         * until someone called start by hand. */
        receiver_runtime.starting = false;
        receiver_runtime.clear_pending = false;
        receiver_runtime.starting_since = receiver_runtime.closing_since = 0;
        retry_update(&retries, capture_ctx.wanted, false, now);
        retry_failure(&retries, now);
    }
    if (receiver_failed() && receiver_runtime.clear_pending) {
        receiver_runtime.clear_pending = false;
        receiver_close();
    }
    if (receiver_runtime.starting && capture_source != SOURCE_HISTOGRAM) {
        receiver_runtime.starting = false;
        receiver_runtime.starting_since = 0;
        receiver_close();
    }
    /* Liveness backstop. A half-open socket can leave a latch set for good; the
     * feed then looks alive while nothing is captured, which is exactly the
     * silent stall this replaced. */
    if (receiver_runtime.starting || receiver_closing()) {
        uint64_t *since = receiver_runtime.starting ? &receiver_runtime.starting_since
                                                    : &receiver_runtime.closing_since;
        if (latch_expired(since, now, RECOVERY_LATCH_MS)) {
            receiver_runtime.starting = false;
            receiver_runtime.clear_pending = false;
            receiver_close();
            *since = 0;
            set_error("Receiver connection stalled; reconnecting");
            retry_failure(&retries, now);
        }
    } else {
        receiver_runtime.starting_since = receiver_runtime.closing_since = 0;
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
        receiver_runtime.starting_since = 0;
        /* A live handle is never re-initialised: doing so would drop the capture
         * session this feed is running on. */
        if (!capture_ctx.handle)
            initialize_capture(NULL, NULL);
        if (!capture_ctx.handle)
            retry_failure(&retries, now);
    }
    model_tick(now);
    test_tick(&playback_json, now);
    layout_refresh_tick(&playback_json, now, capture_ctx.handle || capture_source == SOURCE_PICCAP,
                        &receiver_layout);
    stream_tick(&playback_json, now, handover_enabled && !retries.paused && !retries.blocked);
    bool owned = handover_tick(capture_ctx.handle != NULL, capture_ctx.last_output_at);
    bridge_tick(now, &capture_ctx);
    /* The held frame has served its purpose: PicCap is sending, or our own
     * capture came back. Only then may the connection be dropped. */
    if (output_hold_until && now >= output_hold_until) {
        output_hold_until = 0;
        if (!capture_ctx.handle && handover_yield())
            receiver_close();
    }
    if (owned)
        return 1;
    if (!capture_ctx.handle) {
        if (receiver_runtime.starting || receiver_runtime.clear_pending || receiver_closing())
            return 1;
        /* Another capture service holds VT_CAPTURE. Retrying revokes it, and
         * revoking each other once a second left neither service with a stable
         * capture; handover_tick polls it instead and resumes by itself. */
        if (handover_yield()) {
            retries.due = 0;
            return 1;
        }
        /* Retry because the feed is supposed to be running, not because the
         * autostart hook happens to be installed. A cast app holds VT_CAPTURE
         * for as long as it plays, so a bounded attempt count gave up long
         * before the app released it. */
        retry_update(&retries, capture_ctx.wanted, false, milliseconds());
        if (retry_take(&retries, milliseconds())) {
            capture_ctx.recovery_attempts++;
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
    /* Sampling gate. The timer fires every 25 ms, so a gate that demands the
     * full interval loses the whole slot to a millisecond of jitter: a 25 ms
     * setting ran at 30 samples/s instead of 40, which is 8 ms of extra latency
     * on average. The tolerance keeps the configured rate. */
    if (capture_ctx.sample_at &&
        now - capture_ctx.sample_at + SAMPLE_TOLERANCE_MS < target_capture_interval(&target))
        return 1;
    Histogram hist[16] = {0};
    uint64_t stage = microseconds();
    int rc = vt_get(capture_ctx.handle, hist);
    capture_ctx.vt_get_us = (unsigned)(microseconds() - stage);
    bool populated = !rc && histogram_populated(hist);
    if (!populated) {        unsigned char held[16][3];
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
    if (capture_ctx.sample_at) {
        unsigned long long gap = (unsigned long long)(now - capture_ctx.sample_at);
        capture_ctx.sample_gap_ms = gap > 100000u ? 100000u : (unsigned)gap;
    }
    capture_ctx.sample_at = now;
    capture_ctx.sample_sequence++;
    /* The ABI carries a region id in every record. Place samples by that id so
     * a reordered reply cannot swap two regions between frames. Fall back to
     * array order only if the ids are not a complete 0..15 permutation. */
    Histogram ordered[16];
    bool by_id = true, seen[16] = {false};
    for (unsigned i = 0; i < 16; i++) {
        unsigned id = hist[i].id;
        capture_ctx.hist_ids[i] = id;
        if (id >= 16 || seen[id]) {
            by_id = false;
            break;
        }
        seen[id] = true;
        ordered[id] = hist[i];
    }
    if (!by_id)
        memcpy(ordered, hist, sizeof(ordered));
    memcpy(capture_ctx.last_hist, ordered, sizeof(ordered));
    capture_ctx.hist_ready = true;
    unsigned aggregate[3] = {0};
    for (int i = 0; i < 16; i++) {
        capture_ctx.black_percent[i] = 100;
        for (int c = 0; c < 3; c++) {
            uint64_t total = 0, black = 0;
            for (int b = 0; b < 256; b++) {
                total += ordered[i].bins[c][b];
                if (b <= 12)
                    black += ordered[i].bins[c][b];
            }
            unsigned fraction = total ? (unsigned)(100 * black / total) : 0;
            if (fraction < capture_ctx.black_percent[i])
                capture_ctx.black_percent[i] = fraction;
        }
    }
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 3; c++) {
            unsigned threshold = calibration.has_reference ? calibration.black[i][c] : 16;
            double light;
            if (!CALIBRATION_ACTIVE && calibration.color_stat == CAL_STAT_ACTIVE)
                light = histogram_active_linear_mean(ordered[i].bins[c], threshold);
            else if (!CALIBRATION_ACTIVE && calibration.color_stat == CAL_STAT_P50)
                light = histogram_linear_percentile(ordered[i].bins[c], 500);
            else if (!CALIBRATION_ACTIVE && calibration.color_stat == CAL_STAT_P90)
                light = histogram_linear_percentile(ordered[i].bins[c], 900);
            else if (!CALIBRATION_ACTIVE && calibration.color_stat == CAL_STAT_PEAK)
                light = histogram_linear_percentile(ordered[i].bins[c], 1000);
            else if (!CALIBRATION_ACTIVE && calibration.color_stat == CAL_STAT_VIVID)
                light = histogram_vivid_linear_mean(ordered[i].bins[c]);
            else
                light = histogram_linear_mean(ordered[i].bins[c]);
            /* Calibration always samples the raw panel: references must not be
             * normalised by an older reference set. */
            if (!CALIBRATION_ACTIVE && calibration.compensate && calibration.has_reference) {
                double black = color_light(calibration.black[i][c]);
                double white = color_light(calibration.white[i][c]);
                if (white - black > 0.001)
                    light = (light - black) / (white - black);
                if (light < 0)
                    light = 0;
                else if (light > 1)
                    light = 1;
            }
            /* LG's region means carry a one-code temporal dither even on a
             * static frame. Keep measurements raw for calibration/status and
             * smooth only the rendered plane below. */
            capture_ctx.rgb[i][c] = color_encode(light);
            aggregate[c] += capture_ctx.rgb[i][c];
        }
    /* Detection reads the mean whatever the display statistic is. Reuse the
     * rendered value when it already is the mean, otherwise compute it. */
    if (CALIBRATION_ACTIVE || calibration.color_stat == CAL_STAT_MEAN)
        memcpy(capture_ctx.detect_rgb, capture_ctx.rgb, sizeof(capture_ctx.detect_rgb));
    else
        histogram_detection_colors(ordered, capture_ctx.detect_rgb);
    if (!CALIBRATION_ACTIVE && capture_ctx.preset == 0) {
        int candidate = letterbox_candidate(capture_ctx.black_percent, capture_ctx.detect_rgb);
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
    /* The single-colour output is graded too, or a receiver in that mode would
     * show a different picture from the strip. */
    ColorGrade aggregate_grade = color_grade_preset(calibration.grade);
    color_grade_apply(&aggregate_grade, capture_ctx.sent_rgb);
    unsigned char mapped_rgb[16][3], stable_rgb[16][3];
    unsigned char render_rgb[16][3];
    /* A cut is the frame the picture became a different picture, so it is the
     * one change the filter must not ease into. */
    bool cut = scene_cut_frame(&capture_ctx.scene_cut, ordered, &capture_ctx.scene_cut_distance);
    if (cut) {
        capture_ctx.scene_cuts++;
        stabilize_snap(&capture_ctx.output_filter, capture_ctx.rgb);
    }
    stabilize_push(&capture_ctx.output_filter, capture_ctx.rgb, render_rgb);
    calibration_colors(render_rgb, mapped_rgb);
    /* Brightness and colour are decided after the measurement and after the
     * temporal filter, so nothing downstream has to know about them. */
    ColorGrade grade = color_grade_preset(calibration.grade);
    for (unsigned i = 0; i < 16; i++) {
        dark_color_filter(mapped_rgb[i]);
        color_grade_apply(&grade, mapped_rgb[i]);
    }
    continuity_frame(&capture_ctx.continuity, true, mapped_rgb, now, stable_rgb);
    memcpy(capture_ctx.rendered, stable_rgb, sizeof(capture_ctx.rendered));
    receiver_layout.interpolation = receiver_layout.count && calibration_effective() != CAL_AVERAGE;
    stage = microseconds();
    OutputFrame frame = frame_build(stable_rgb, receiver_layout.blends, receiver_layout.count, 1500);
    uint64_t after_build = microseconds();
    int failed = !frame_send(&frame, 1500, now);
    frame_release(&frame);
    uint64_t after_send = microseconds();
    if (after_build > stage)
        capture_ctx.plane_us = (unsigned)(after_build - stage);
    if (after_send > after_build)
        capture_ctx.send_us = (unsigned)(after_send - after_build);
    if (capture_ctx.last_output_us && after_send > capture_ctx.last_output_us)
        capture_ctx.output_gap_ms = (unsigned)((after_send - capture_ctx.last_output_us) / 1000);
    capture_ctx.last_output_us = after_send;
    capture_ctx.tick_us = (unsigned)(after_send - tick_started);
    if (failed) {
        set_error("Receiver connection lost");
        stop_capture();
        return 1;
    }
    capture_ctx.color_valid = true;
    return 1;
}
