#ifndef HYPERDRM_FLATBUFFER_OUTPUT_H
#define HYPERDRM_FLATBUFFER_OUTPUT_H
/* HyperHDR's FlatBuffer server decodes one request and broadcasts the frame to
 * every running LED hardware instance (GlobalSignals::SignalSetGlobalImage), so
 * one connection reaches all instances where the JSON API would need one bound
 * connection each. Requests are length-prefixed big-endian; each reply carries
 * an error string that is empty when the request was accepted. */
#include "flatbuffer.h"
#include "target.h"

#define FB_REPLY_LIMIT 65536
typedef enum { FB_IDLE, FB_CONNECTING, FB_REGISTERING, FB_READY, FB_FAILED } FlatbufferStage;
typedef struct {
    int fd;
    FlatbufferStage stage;
    uint64_t deadline;
    uint8_t *replies, *queue, *pending;
    size_t replies_used, queue_length, queue_sent, pending_length;
    uint8_t *scratch;
    size_t scratch_capacity;
    unsigned priority;
    unsigned long frames;
    bool queue_image, pending_image, closing;
    char error[128];
} FlatbufferOutput;

static void fb_out_close(FlatbufferOutput *o) {
    if (o->fd >= 0)
        close(o->fd);
    free(o->replies);
    free(o->queue);
    free(o->pending);
    o->fd = -1;
    o->replies = o->queue = o->pending = NULL;
    o->replies_used = o->queue_length = o->queue_sent = o->pending_length = 0;
    o->queue_image = o->pending_image = false;
    o->deadline = 0;
}
/* The scratch buffer is reused between sessions; release it once at shutdown. */
static void fb_out_release(FlatbufferOutput *o) {
    fb_out_close(o);
    free(o->scratch);
    o->scratch = NULL;
    o->scratch_capacity = 0;
    o->stage = FB_IDLE;
}
static void fb_out_fail(FlatbufferOutput *o, const char *error) {    fb_out_close(o);
    o->stage = FB_FAILED;
    snprintf(o->error, sizeof(o->error), "%s", error);
}
static bool fb_out_begin(FlatbufferOutput *o, const TargetConfig *target, unsigned priority,
                         size_t capacity, uint64_t now) {
    fb_out_close(o);
    o->stage = FB_IDLE;
    o->error[0] = 0;
    o->frames = 0;
    o->closing = false;
    o->priority = priority;
    if (!o->scratch || o->scratch_capacity < capacity) {
        free(o->scratch);
        o->scratch = malloc(capacity);
        o->scratch_capacity = o->scratch ? capacity : 0;
        if (!o->scratch)
            return false;
    }
    o->replies = malloc(FB_REPLY_LIMIT);
    if (!o->replies)
        return false;
    o->fd = target_open_port(target, target->piccap_port);
    if (o->fd < 0) {
        o->error[0] = 0;
        snprintf(o->error, sizeof(o->error), "Cannot open the FlatBuffer connection");
        o->stage = FB_FAILED;
        return false;
    }
    o->stage = FB_CONNECTING;
    o->deadline = now + 3000;
    return true;
}
/* Encode and frame one request, then hand it to the single send slot. */
static bool fb_out_queue_request(FlatbufferOutput *o, size_t size, bool image, uint64_t now) {
    if (size > o->scratch_capacity) {
        fb_out_fail(o, "FlatBuffer frame exceeds the buffer");
        return false;
    }
    uint8_t *framed = malloc(size + 4);
    if (!framed) {
        fb_out_fail(o, "Cannot allocate FlatBuffer frame");
        return false;
    }
    framed[0] = (uint8_t)(size >> 24);
    framed[1] = (uint8_t)(size >> 16);
    framed[2] = (uint8_t)(size >> 8);
    framed[3] = (uint8_t)size;
    memcpy(framed + 4, o->scratch + o->scratch_capacity - size, size);
    free(o->queue);
    o->queue = framed;
    o->queue_length = size + 4;
    o->queue_sent = 0;
    o->queue_image = image;
    o->deadline = now + 3000;
    return true;
}
/* The newest frame replaces any waiting one; sent frames are never rewound. */
static bool fb_out_frame(FlatbufferOutput *o, const void *rgb, int width, int height,
                         int duration, uint64_t now) {
    if (o->stage == FB_FAILED)
        return false;
    FlatBuffer builder = {.buf = o->scratch, .capacity = o->scratch_capacity};
    size_t size = fb_encode_image(&builder, rgb, width, height, duration);
    if (builder.overflow) {
        fb_out_fail(o, "FlatBuffer frame exceeds the buffer");
        return false;
    }
    if (size > o->scratch_capacity)
        return false;
    uint8_t *framed = malloc(size + 4);
    if (!framed) {
        fb_out_fail(o, "Cannot allocate FlatBuffer frame");
        return false;
    }
    framed[0] = (uint8_t)(size >> 24);
    framed[1] = (uint8_t)(size >> 16);
    framed[2] = (uint8_t)(size >> 8);
    framed[3] = (uint8_t)size;
    memcpy(framed + 4, o->scratch + o->scratch_capacity - size, size);
    free(o->pending);
    o->pending = framed;
    o->pending_length = size + 4;
    o->pending_image = true;
    (void)now;
    return true;
}
static void fb_out_drain_replies(FlatbufferOutput *o) {
    while (o->replies_used >= 4) {
        size_t size = ((size_t)o->replies[0] << 24) | ((size_t)o->replies[1] << 16) |
                      ((size_t)o->replies[2] << 8) | (size_t)o->replies[3];
        if (size > FB_REPLY_LIMIT) {
            fb_out_fail(o, "FlatBuffer reply exceeds the buffer");
            return;
        }
        if (o->replies_used < size + 4)
            return;
        char error[128];
        if (!fb_reply_error(o->replies + 4, size, error, sizeof(error))) {
            fb_out_fail(o, "FlatBuffer reply cannot be parsed");
            return;
        }
        if (error[0]) {
            char message[sizeof(o->error)];
            snprintf(message, sizeof(message), "Receiver rejected: %.100s", error);
            fb_out_fail(o, message);
            return;
        }
        if (o->stage == FB_REGISTERING) {
            o->stage = FB_READY;
            o->deadline = 0;
        }
        memmove(o->replies, o->replies + size + 4, o->replies_used - size - 4);
        o->replies_used -= size + 4;
    }
}
static void fb_out_tick(FlatbufferOutput *o, uint64_t now) {
    if (o->stage == FB_IDLE || o->stage == FB_FAILED)
        return;
    if (o->deadline && now >= o->deadline) {
        fb_out_fail(o, o->stage == FB_CONNECTING   ? "FlatBuffer connection timed out"
                       : o->stage == FB_REGISTERING ? "FlatBuffer registration timed out"
                                                    : "FlatBuffer send timed out");
        return;
    }
    if (o->stage == FB_CONNECTING) {
        struct pollfd p = {o->fd, POLLOUT, 0};
        int error = 0;
        socklen_t size = sizeof(error);
        if (poll(&p, 1, 0) <= 0)
            return;
        if (getsockopt(o->fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) {
            fb_out_fail(o, "FlatBuffer connection refused or unreachable");
            return;
        }
        FlatBuffer builder = {.buf = o->scratch, .capacity = o->scratch_capacity};
        size_t length = fb_encode_register(&builder, "HyperDRM", (int)o->priority);
        if (builder.overflow || !fb_out_queue_request(o, length, false, now)) {
            fb_out_fail(o, "Cannot encode the FlatBuffer registration");
            return;
        }
        o->stage = FB_REGISTERING;
    }
    if (o->queue) {
        size_t budget = 65536;
        while (o->queue_sent < o->queue_length && budget) {
            size_t amount = o->queue_length - o->queue_sent;
            if (amount > budget)
                amount = budget;
            ssize_t sent = send(o->fd, o->queue + o->queue_sent, amount, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    break;
                fb_out_fail(o, "FlatBuffer connection closed during send");
                return;
            }
            o->queue_sent += (size_t)sent;
            budget -= (size_t)sent;
        }
        if (o->queue_sent == o->queue_length) {
            if (o->queue_image)
                o->frames++;
            free(o->queue);
            o->queue = NULL;
            o->queue_length = o->queue_sent = 0;
            o->deadline = o->closing ? now + 1000 : 0;
            if (o->closing) {
                fb_out_close(o);
                o->stage = FB_IDLE;
                return;
            }
        }
    }
    size_t available = FB_REPLY_LIMIT - o->replies_used;
    if (available) {
        ssize_t got = recv(o->fd, o->replies + o->replies_used, available, 0);
        if (got == 0) {
            fb_out_fail(o, "FlatBuffer receiver closed the connection");
            return;
        }
        if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            fb_out_fail(o, "FlatBuffer receive failed");
            return;
        }
        if (got > 0) {
            o->replies_used += (size_t)got;
            fb_out_drain_replies(o);
            if (o->stage == FB_FAILED)
                return;
        }
    }
    if (o->stage == FB_READY && !o->queue && o->pending) {
        free(o->queue);
        o->queue = o->pending;
        o->queue_length = o->pending_length;
        o->queue_sent = 0;
        o->queue_image = o->pending_image;
        o->pending = NULL;
        o->pending_length = 0;
        o->pending_image = false;
        o->deadline = now + 3000;
    }
}
/* Drop the frame from every instance. A disconnect also clears the priority, so
 * `clear` only controls the explicit Clear request before closing. */
static void fb_out_stop(FlatbufferOutput *o, uint64_t now, bool clear) {
    free(o->pending);
    o->pending = NULL;
    o->pending_length = 0;
    o->pending_image = false;
    if (clear && o->stage == FB_READY && !o->closing) {
        FlatBuffer builder = {.buf = o->scratch, .capacity = o->scratch_capacity};
        size_t length = fb_encode_clear(&builder, (int)o->priority);
        if (!builder.overflow && !o->queue &&
            fb_out_queue_request(o, length, false, now)) {
            o->closing = true;
            return;
        }
    }
    fb_out_close(o);
    o->stage = FB_IDLE;
    o->closing = false;
}
#endif
