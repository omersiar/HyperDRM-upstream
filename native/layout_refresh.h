#ifndef HYPERDRM_LAYOUT_REFRESH_H
#define HYPERDRM_LAYOUT_REFRESH_H
#define LAYOUT_REFRESH_MS 5000
static ReceiverProbe layout_probe = {.fd = -1};
static uint64_t next_layout_check, last_layout_check;
static unsigned layout_updates, layout_probe_generation;
static char layout_error[128];
static void layout_refresh_reset(uint64_t due) {
    probe_close(&layout_probe);
    layout_probe.stage = TEST_IDLE;
    next_layout_check = due;
}
static bool layout_mapping_changed(const ReceiverLayoutContext *layout, const LedBlend *next,
                                   unsigned count) {
    if (count != layout->count || !layout->blends)
        return true;
    for (unsigned i = 0; i < count; i++)
        if (next[i].a != layout->blends[i].a || next[i].b != layout->blends[i].b ||
            next[i].weight != layout->blends[i].weight)
            return true;
    return false;
}
static void layout_refresh_tick(PlaybackJson *json, uint64_t now, bool running,
                                ReceiverLayoutContext *layout) {
    if (!running) {
        layout_refresh_reset(now + LAYOUT_REFRESH_MS);
        return;
    }
    probe_tick(&layout_probe, json, now);
    if (layout_probe.stage == TEST_DONE) {
        if (layout_probe_generation == calibration_generation) {
            if (layout_mapping_changed(layout, layout_probe.mapping, layout_probe.leds)) {
                free(layout->blends);
                layout->blends = layout_probe.mapping;
                layout_probe.mapping = NULL;
                layout->count = layout_probe.leds;
                layout_updates++;
            }
            last_layout_check = now;
            layout_error[0] = 0;
        } else
            next_layout_check = now;
        probe_close(&layout_probe);
        layout_probe.stage = TEST_IDLE;
    } else if (layout_probe.stage == TEST_FAILED) {
        snprintf(layout_error, sizeof(layout_error), "%s", layout_probe.error);
        layout_probe.stage = TEST_IDLE;
    }
    if (!probe_busy(&layout_probe) && now >= next_layout_check) {
        layout_probe_generation = calibration_generation;
        probe_begin(&layout_probe, &target, now);
        next_layout_check = now + LAYOUT_REFRESH_MS;
    }
}
#endif
