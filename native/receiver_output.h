#ifndef HYPERDRM_RECEIVER_OUTPUT_H
#define HYPERDRM_RECEIVER_OUTPUT_H
/* Main output connection. Every socket operation is nonblocking, with bounded
 * work per tick. One acknowledged command is in flight; only the newest unsent
 * color frame is retained. No Luna message or LG capture handle is owned here. */
typedef enum {
    OUTPUT_IDLE,
    OUTPUT_CONNECTING,
    OUTPUT_LOGIN,
    OUTPUT_LAYOUT,
    OUTPUT_READY,
    OUTPUT_FAILED
} OutputStage;
typedef struct {
    int fd;
    OutputStage stage;
    TargetConfig target;
    char *incoming, *request, *latest;
    size_t used, sent, length;
    uint64_t deadline;
    unsigned transaction, awaiting, count;
    LedBlend *mapping;
    unsigned long acknowledgements;
    char error[128];
    bool closing;
} ReceiverOutput;
static void output_close(ReceiverOutput *o) {
    if (o->fd >= 0)
        close(o->fd);
    free(o->incoming);
    free(o->request);
    free(o->latest);
    free(o->mapping);
    *o = (ReceiverOutput){.fd = -1};
}
static void output_fail(ReceiverOutput *o, const char *error) {
    output_close(o);
    o->stage = OUTPUT_FAILED;
    snprintf(o->error, sizeof(o->error), "%s", error);
}
static bool output_request(ReceiverOutput *o, const char *line, uint64_t now) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == ' '))
        n--;
    if (!n || line[n - 1] != '}' || o->request || o->awaiting)
        return false;
    o->request = malloc(n + 48);
    if (!o->request)
        return false;
    if (o->transaction >= 2147483647u)
        o->transaction = 0;
    o->transaction++;
    memcpy(o->request, line, n - 1);
    snprintf(o->request + n - 1, 48, ",\"tan\":%u}\n", o->transaction);
    o->length = strlen(o->request);
    o->sent = 0;
    o->awaiting = o->transaction;
    o->deadline = now + 3000;
    return true;
}
static bool output_begin(ReceiverOutput *o, const TargetConfig *target, uint64_t now) {
    output_close(o);
    o->target = *target;
    o->incoming = malloc(2 * 1024 * 1024 + 1);
    if (!o->incoming) {
        output_fail(o, "Cannot allocate receiver buffer");
        return false;
    }
    o->fd = target_open(target);
    if (o->fd < 0) {
        output_fail(o, "Cannot open receiver connection");
        return false;
    }
    o->stage = OUTPUT_CONNECTING;
    o->deadline = now + 3000;
    return true;
}
static bool output_frame(ReceiverOutput *o, const char *line) {
    if (o->stage != OUTPUT_READY || o->closing)
        return false;
    if (strlen(line) > 2 * 1024 * 1024)
        return false;
    char *next = strdup(line);
    if (!next)
        return false;
    free(o->latest);
    o->latest = next;
    return true;
}
static void output_stop(ReceiverOutput *o, uint64_t now, bool clear) {
    if (!clear || o->stage != OUTPUT_READY) {
        output_close(o);
        return;
    }
    free(o->latest);
    o->latest = strdup("{\"command\":\"clear\",\"priority\":149}\n");
    if (!o->latest) {
        output_close(o);
        return;
    }
    o->closing = true;
    /* Allow the current transaction and the clear to finish asynchronously. */
    if (!o->awaiting)
        o->deadline = now + 3000;
}
static bool output_flush(ReceiverOutput *o, size_t *budget) {
    if (!o->request || !*budget)
        return true;
    size_t amount = o->length - o->sent;
    if (amount > *budget)
        amount = *budget;
    ssize_t n = send(o->fd, o->request + o->sent, amount, MSG_NOSIGNAL);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        output_fail(o, "Receiver closed during send");
        return false;
    }
    if (n > 0) {
        o->sent += (size_t)n;
        *budget -= (size_t)n;
    }
    if (o->sent == o->length) {
        free(o->request);
        o->request = NULL;
    }
    return true;
}
static void output_tick(ReceiverOutput *o, PlaybackJson *json, uint64_t now) {
    if (o->stage == OUTPUT_IDLE || o->stage == OUTPUT_FAILED)
        return;
    if ((o->stage != OUTPUT_READY || o->awaiting || o->closing) && now >= o->deadline) {
        if (o->closing)
            output_close(o);
        else
            output_fail(o, "Receiver transaction timed out");
        return;
    }
    if (o->stage == OUTPUT_CONNECTING) {
        struct pollfd p = {o->fd, POLLOUT, 0};
        int error = 0;
        socklen_t size = sizeof(error);
        if (poll(&p, 1, 0) <= 0)
            return;
        if (getsockopt(o->fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) {
            output_fail(o, "Receiver connection refused or unreachable");
            return;
        }
        char line[384];
        if (o->target.token[0]) {
            snprintf(line, sizeof(line),
                     "{\"command\":\"authorize\",\"subcommand\":\"login\",\"token\":\"%s\"}\n",
                     o->target.token);
            o->stage = OUTPUT_LOGIN;
        } else {
            snprintf(line, sizeof(line), "{\"command\":\"serverinfo\"}\n");
            o->stage = OUTPUT_LAYOUT;
        }
        if (!output_request(o, line, now)) {
            output_fail(o, "Cannot allocate receiver request");
            return;
        }
    }
    if (o->stage == OUTPUT_READY && !o->awaiting && o->latest) {
        if (!output_request(o, o->latest, now)) {
            output_fail(o, "Cannot allocate color request");
            return;
        }
        free(o->latest);
        o->latest = NULL;
    }
    size_t send_budget = 65536;
    if (!output_flush(o, &send_budget))
        return;
    size_t space = 2 * 1024 * 1024 - o->used;
    if (space > 65536)
        space = 65536;
    if (!space && !memchr(o->incoming, '\n', o->used)) {
        output_fail(o, "Receiver response exceeds 2 MiB");
        return;
    }
    ssize_t n = space ? recv(o->fd, o->incoming + o->used, space, 0) : -1;
    if (!space)
        errno = EAGAIN;
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        output_fail(o, "Receiver closed during receive");
        return;
    }
    if (n > 0) {
        if (memchr(o->incoming + o->used, 0, (size_t)n)) {
            output_fail(o, "Receiver sent an embedded NUL");
            return;
        }
        o->used += (size_t)n;
    }
    o->incoming[o->used] = 0;
    for (unsigned budget = 0; budget < 8; budget++) {
        char *end = memchr(o->incoming, '\n', o->used);
        if (!end)
            break;
        *end = 0;
        void *root = playback_parse_exact(json, o->incoming);
        if (!root || json->type(root) != 4) {
            if (root)
                json->put(root);
            output_fail(o, "Receiver returned malformed JSON");
            return;
        }
        void *tan = playback_field(json, root, "tan");
        char *tail = NULL;
        unsigned long id = tan && json->type(tan) == 3 ? strtoul(json->string(tan), &tail, 10) : 0;
        bool matches = o->awaiting && tail && !*tail && id == o->awaiting;
        if (matches) {
            if (o->request || !playback_true(json, root, "success")) {
                json->put(root);
                output_fail(o, o->request ? "Receiver acknowledged an incomplete request"
                               : o->stage == OUTPUT_LOGIN
                                   ? "Receiver authentication failed; check API token"
                               : o->stage == OUTPUT_LAYOUT ? "Receiver rejected LED layout request"
                                                           : "Receiver rejected color request");
                return;
            }
            o->awaiting = 0;
            if (o->stage == OUTPUT_LOGIN) {
                o->stage = OUTPUT_LAYOUT;
                if (!output_request(o, "{\"command\":\"serverinfo\"}\n", now)) {
                    json->put(root);
                    output_fail(o, "Cannot allocate layout request");
                    return;
                }
            } else if (o->stage == OUTPUT_LAYOUT) {
                if (!layout_from_json(o->incoming, &o->mapping, &o->count)) {
                    json->put(root);
                    output_fail(o, "Receiver returned an invalid LED layout");
                    return;
                }
                o->stage = OUTPUT_READY;
            } else {
                o->acknowledgements++;
                if (o->closing && !o->latest) {
                    json->put(root);
                    output_close(o);
                    return;
                }
            }
        }
        json->put(root);
        size_t consumed = (size_t)(end - o->incoming) + 1;
        memmove(o->incoming, o->incoming + consumed, o->used - consumed);
        o->used -= consumed;
        o->incoming[o->used] = 0;
    }
    /* An ACK can free the slot after the first send pass. Use the remaining
     * byte budget to send the newest queued frame in this same tick. */
    if (o->stage == OUTPUT_READY && !o->awaiting && o->latest) {
        if (!output_request(o, o->latest, now)) {
            output_fail(o, "Cannot allocate color request");
            return;
        }
        free(o->latest);
        o->latest = NULL;
    }
    output_flush(o, &send_budget);
}
#endif
