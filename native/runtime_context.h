#ifndef HYPERDRM_RUNTIME_CONTEXT_H
#define HYPERDRM_RUNTIME_CONTEXT_H
#include <signal.h>
#include "histogram.h"
#include "continuity.h"
#include "stabilize_filter.h"
typedef struct {
    void *handle;
    char diagnostic[256];
    unsigned long frames;
    unsigned char rgb[16][3], sent_rgb[3];
    /* The plain-mean measurement used by the letterbox probe and the r14 bar
     * meter. `rgb` carries the display statistic, which may be a percentile, so
     * the two are deliberately separate: decisions must not move when the user
     * changes how the feed is summarised. */
    unsigned char detect_rgb[16][3];
    /* Exactly what the last frame carried: raw `rgb` is the measurement, this
     * is the measurement after stabilising, mapping and continuity, so the
     * difference between the two is the pipeline's own lag. */
    unsigned char rendered[16][3];
    unsigned hist_ids[16];
    /* Last id-sorted frame, served by the diagnostic `histograms` method.
     * Kept here rather than in the status reply: 24 KiB of bins would dominate
     * a method the app polls four times per second. */
    Histogram last_hist[16];
    bool hist_ready;
    StabilizeFilter output_filter;
    unsigned black_percent[16], preset, letterbox_mode, settling;
    int pending_letterbox;
    uint64_t candidate_since, next_border_probe, sample_at;
    unsigned sample_sequence, recoveries;
    uint64_t recovery_started, last_recovery_ms, last_output_at;
    bool color_valid;
    Continuity continuity;
    /* Recovery is driven by what the user asked for, not by the autostart hook:
     * a cast app revokes the LG handle for minutes at a time and the feed has
     * to come back on its own once that app releases VT_CAPTURE. */
    bool wanted;
    unsigned recovery_attempts;
    /* Timing probe, in microseconds for the stages and milliseconds for the
     * cadence, so the status reply can explain a feed that feels slow. */
    unsigned vt_get_us, plane_us, send_us, tick_us, sample_gap_ms, output_gap_ms;
    uint64_t last_output_us;
    volatile sig_atomic_t revoked;
} CaptureContext;
/* LedBlend is supplied by the layout module (or a portable test fixture). */
typedef struct {
    LedBlend *blends;
    unsigned count;
    bool interpolation;
} ReceiverLayoutContext;
#endif
