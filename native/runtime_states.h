#ifndef HYPERDRM_RUNTIME_STATES_H
#define HYPERDRM_RUNTIME_STATES_H
/* Connection and source transitions are independent state machines. */
typedef enum {
    STREAM_DISCONNECTED,
    STREAM_CONNECTING,
    STREAM_AUTHENTICATING,
    STREAM_LAYOUT,
    STREAM_SUBSCRIBED
} StreamStage;
typedef enum { BRIDGE_IDLE, BRIDGE_HOLDING, BRIDGE_WAITING_FRAME, BRIDGE_FADING } BridgePhase;
#endif
