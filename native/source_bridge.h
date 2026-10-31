#ifndef HYPERDRM_SOURCE_BRIDGE_H
#define HYPERDRM_SOURCE_BRIDGE_H
#include "runtime_states.h"
static BridgePhase bridge_phase;
static unsigned bridge_sequence;
static uint64_t bridge_started, bridge_clear_at, bridge_fade_at, bridge_last_sent;
static unsigned char bridge_from[16][3], bridge_to[16][3];
static CaptureSource bridge_source;
static char *region_command(const unsigned char colors[16][3], const LedBlend *mapping,
                            unsigned count, unsigned duration) {
    if (!count || !mapping)
        return NULL;
    if (!strcmp(target.receiver, "HyperHDR"))
        return spatial_image_command_duration(colors, PRIORITY, duration);
    size_t capacity = 256 + (size_t)count * 12;
    char *line = malloc(capacity);
    if (!line)
        return NULL;
    size_t n = (size_t)snprintf(line, capacity,
                                "{\"command\":\"color\",\"priority\":%d,\"duration\":%u,\"origin\":"
                                "\"HyperDRM\",\"color\":[",
                                PRIORITY, duration);
    for (unsigned i = 0; i < count; i++) {
        unsigned char value[3];
        LedBlend b = mapping[i];
        blend_color(colors[b.a], colors[b.b], b.weight, value);
        n += (size_t)snprintf(line + n, capacity - n, "%s%u,%u,%u", i ? "," : "", value[0],
                              value[1], value[2]);
    }
    snprintf(line + n, capacity - n, "]}\n");
    return line;
}
static bool bridge_clear(void) {
    return stream.stage == STREAM_SUBSCRIBED &&
           stream_queue("{\"command\":\"clear\",\"priority\":149}\n");
}
static bool bridge_send(const unsigned char colors[16][3]) {
    if (stream.stage != STREAM_SUBSCRIBED)
        return false;
    char *line = region_command(colors, stream.mapping, stream.count, 1500);
    if (!line)
        return false;
    bool ok = stream_queue(line);
    free(line);
    return ok;
}
static void bridge_tick(uint64_t now, CaptureContext *capture) {
    if (retries.paused || retries.blocked) {
        if (bridge_phase)
            bridge_clear();
        bridge_phase = BRIDGE_IDLE;
        bridge_source = capture_source;
        return;
    }
    if (capture_source != bridge_source) {
        /* PicCap is taking over, either because we started it after a takeover
         * or because it was already capturing and we handed the resource back.
         * Both transitions fade the last histogram colours into PicCap's own
         * frames instead of clearing the receiver first. */
        if (capture_source == SOURCE_PICCAP_STARTING ||
            (bridge_source == SOURCE_HISTOGRAM && capture_source == SOURCE_PICCAP)) {
            bridge_phase =
                capture->continuity.available && now - capture->continuity.valid_at < 2000
                    ? BRIDGE_HOLDING
                    : BRIDGE_IDLE;
            bridge_started = now;
            memcpy(bridge_from, capture->continuity.last, sizeof(bridge_from));
        } else if (capture_source == SOURCE_PICCAP_STOPPING ||
                   (bridge_source == SOURCE_PICCAP && capture_source == SOURCE_HISTOGRAM)) {
            bridge_phase = BRIDGE_IDLE;
            if (stream.valid && now - stream.updated < 1000) {
                continuity_seed(&capture->continuity, stream.colors, now);
                bridge_send(stream.colors);
            }
        }
        bridge_source = capture_source;
    }
    if (bridge_phase && now - bridge_started >= 3000) {
        if (bridge_clear())
            bridge_phase = BRIDGE_IDLE;
        return;
    }
    if (bridge_phase == BRIDGE_HOLDING && capture_source == SOURCE_PICCAP && piccap_connected &&
        stream.stage == STREAM_SUBSCRIBED) {
        if (bridge_clear()) {
            bridge_sequence = stream.sequence;
            bridge_clear_at = now;
            bridge_phase = BRIDGE_WAITING_FRAME;
        }
    } else if (bridge_phase == BRIDGE_HOLDING && now - bridge_last_sent >= 100 &&
               stream.stage == STREAM_SUBSCRIBED) {
        if (bridge_send(bridge_from))
            bridge_last_sent = now;
    } else if (bridge_phase == BRIDGE_WAITING_FRAME && stream.valid &&
               stream.sequence > bridge_sequence && stream.updated >= bridge_clear_at + 150) {
        memcpy(bridge_to, stream.colors, sizeof(bridge_to));
        bridge_fade_at = now;
        bridge_phase = BRIDGE_FADING;
        bool black = true;
        for (unsigned r = 0; r < 16; r++)
            for (unsigned c = 0; c < 3; c++)
                if (bridge_to[r][c] > 3)
                    black = false;
        if (black)
            bridge_phase = BRIDGE_IDLE;
    }
    if (bridge_phase == BRIDGE_FADING && now - bridge_last_sent >= 100) {
        if (now - bridge_fade_at >= 500) {
            if (bridge_clear())
                bridge_phase = BRIDGE_IDLE;
        } else {
            unsigned char colors[16][3];
            continuity_mix(bridge_from, bridge_to, (unsigned)(now - bridge_fade_at), colors);
            if (bridge_send(colors))
                bridge_last_sent = now;
        }
    }
}
#endif
