#ifndef HYPERDRM_SERVICE_CONTEXT_H
#define HYPERDRM_SERVICE_CONTEXT_H
#include "runtime_context.h"
#include "receiver_output.h"
#include "flatbuffer_output.h"
/* Runtime ownership is separate from the saved calibration configuration. */
typedef struct {
    uint64_t until, model_deadline, next_model_probe;
    unsigned long model_token;
    bool model_busy;
    Calibration draft;
    char message[256];
} CalibrationSessionContext;
typedef struct {
    ReceiverOutput output;
    FlatbufferOutput flatbuffer;
    bool starting, clear_pending;
    /* Age of the latch that is currently holding capture back. A latch with a
     * deadline cannot strand the service the way an unbounded one did. */
    uint64_t starting_since, closing_since;
} ReceiverRuntime;
#endif
