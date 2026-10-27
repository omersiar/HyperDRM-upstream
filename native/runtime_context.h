#ifndef HYPERDRM_RUNTIME_CONTEXT_H
#define HYPERDRM_RUNTIME_CONTEXT_H
#include <signal.h>
#include "histogram.h"
#include "continuity.h"
typedef struct {
    void *handle;
    char diagnostic[256];
    unsigned long frames;
    unsigned char rgb[16][3], sent_rgb[3];
    unsigned black_percent[16], preset, letterbox_mode, settling;
    int pending_letterbox;
    uint64_t candidate_since, next_border_probe, sample_at;
    unsigned sample_sequence, recoveries;
    uint64_t recovery_started, last_recovery_ms, last_output_at;
    bool color_valid;
    Continuity continuity;
    volatile sig_atomic_t revoked;
} CaptureContext;
/* LedBlend is supplied by the layout module (or a portable test fixture). */
typedef struct {
    LedBlend *blends;
    unsigned count;
    bool interpolation;
} ReceiverLayoutContext;
#endif
