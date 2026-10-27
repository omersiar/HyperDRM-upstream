#ifndef HYPERDRM_RETRY_H
#define HYPERDRM_RETRY_H
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#define RETRY_DELAY_MS 5000
/* Continuous autostart retries and bounded manual source recovery. */
typedef struct {
    uint64_t due;
    unsigned attempts, quick_retries;
    bool automatic, paused, blocked;
} RetryState;
static void retry_reset(RetryState *state) {
    *state = (RetryState){0};
}
static void retry_pause(RetryState *state) {
    retry_reset(state);
    state->paused = true;
}
static void retry_update(RetryState *state, bool enabled, bool running, uint64_t now) {
    bool previous = state->automatic;
    state->automatic = enabled && !state->paused && !state->blocked;
    if (previous && !state->automatic) {
        state->due = 0;
        state->attempts = 0;
    }
    if (state->automatic && !running && !state->due)
        state->due = now + RETRY_DELAY_MS;
}
static bool retry_take(RetryState *state, uint64_t now) {
    if (!state->due || now < state->due || state->paused || state->blocked)
        return false;
    state->due = 0;
    if (state->attempts < UINT_MAX)
        state->attempts++;
    return true;
}
static void retry_failure(RetryState *state, uint64_t now) {
    if (!state->paused && !state->blocked && (state->automatic || state->attempts < 6)) {
        if (state->quick_retries) {
            state->quick_retries--;
            state->due = now + 400;
        } else
            state->due = now + RETRY_DELAY_MS;
    }
}
static inline void retry_source_transition(RetryState *state, uint64_t now) {
    if (!state->paused && !state->blocked) {
        state->attempts = 0;
        state->quick_retries = 2;
        state->due = now + 200;
    }
}
#endif
