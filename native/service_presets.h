#ifndef HYPERDRM_SERVICE_PRESETS_H
#define HYPERDRM_SERVICE_PRESETS_H
/* Histogram preset scan.
 *
 * LG selects the sampling window set with an integer preset and documents none
 * of them. `presetScan {"preset":N}` switches the live handle, waits for the new
 * windows to settle, reads one frame and returns the sixteen bin totals, then
 * restores preset 0 for the renderer.
 *
 * The total is the area evidence: every pixel a window covers lands in some bin,
 * so a total is proportional to the window area whatever the picture shows.
 * Identical vectors for two presets mean identical windows; a different vector is
 * a different geometry.
 *
 * Runs on the luna thread between ticks, so it cannot race the capture loop. The
 * blocking wait is deliberate: a diagnostic may stall the service for 0.4 s. */
#include "preset_payload.h"
#define PRESET_SCAN_JSON 6144
static bool preset_scan(void *h, void *m, void *ctx) {
    (void)ctx;
    if (!capture_ctx.handle)
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Capture is not running\"}");
    int requested = preset_from_payload(m ? payload(m) : NULL);
    if (requested < 0)
        return respond(
            h, m, "{\"returnValue\":false,\"errorText\":\"Expected {\\\"preset\\\":0..15}\"}");
    int rc = vt_preset(capture_ctx.handle, requested);
    if (rc != 0) {
        char json[160];
        snprintf(json, sizeof(json),
                 "{\"returnValue\":false,\"preset\":%d,\"setPreset\":%d}", requested, rc);
        return respond(h, m, json);
    }
    usleep(400000);
    Histogram hist[16];
    memset(hist, 0, sizeof(hist));
    int get_rc = vt_get(capture_ctx.handle, hist);
    /* The letterbox logic expects preset 0; leave the renderer as we found it. */
    int restored = vt_preset(capture_ctx.handle, 0);
    capture_ctx.preset = 0;
    capture_ctx.settling = 2;
    if (get_rc != 0) {
        char json[200];
        snprintf(json, sizeof(json),
                 "{\"returnValue\":false,\"preset\":%d,\"get\":%d,\"restore\":%d}", requested,
                 get_rc, restored);
        return respond(h, m, json);
    }
    static char json[PRESET_SCAN_JSON];
    int used = snprintf(json, sizeof(json),
                        "{\"returnValue\":true,\"preset\":%d,\"restore\":%d,\"regions\":[", requested,
                        restored);
    for (int i = 0; i < 16 && used > 0 && used < (int)sizeof(json) - 96; i++) {
        unsigned long long count[3];
        unsigned mean[3];
        for (int c = 0; c < 3; c++) {
            unsigned long long sum = 0;
            for (int b = 0; b < 256; b++)
                sum += hist[i].bins[c][b];
            count[c] = sum;
            mean[c] = histogram_mean(hist[i].bins[c]);
        }
        used += snprintf(json + used, sizeof(json) - used,
                         "%s{\"id\":%u,\"count\":[%llu,%llu,%llu],\"mean\":[%u,%u,%u]}",
                         i ? "," : "", hist[i].id, count[0], count[1], count[2], mean[0],
                         mean[1], mean[2]);
    }
    if (used > 0 && used < (int)sizeof(json) - 2)
        used += snprintf(json + used, sizeof(json) - used, "]}");
    if (used < 0 || used >= (int)sizeof(json))
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Payload overflow\"}");
    return respond(h, m, json);
}
#endif
