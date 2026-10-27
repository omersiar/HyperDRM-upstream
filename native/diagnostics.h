#ifndef HYPERDRM_DIAGNOSTICS_H
#define HYPERDRM_DIAGNOSTICS_H
/* An independent nonblocking connection. Tests never send colors or acquire
 * capture. Work per timer iteration is bounded to keep output responsive. */
typedef enum {
    TEST_IDLE,
    TEST_CONNECT,
    TEST_SEND,
    TEST_LOGIN,
    TEST_LAYOUT,
    TEST_DONE,
    TEST_FAILED
} TestStage;
typedef struct {
    int fd;
    TestStage stage, after_send;
    TargetConfig target;
    uint64_t started, deadline, elapsed;
    unsigned leds;
    char request[384], error[128];
    size_t sent, used;
    char *response;
    LedBlend *mapping;
} ReceiverProbe;
static ReceiverProbe connection_test = {.fd = -1};
static const char *probe_stage_name(const ReceiverProbe *probe) {
    static const char *names[] = {
        "Not tested",         "Connecting", "Sending request", "Checking authentication",
        "Reading LED layout", "Passed",     "Failed"};
    return names[probe->stage];
}
static bool probe_busy(const ReceiverProbe *probe) {
    return probe->stage >= TEST_CONNECT && probe->stage <= TEST_LAYOUT;
}
static void probe_close(ReceiverProbe *probe) {
    if (probe->fd >= 0)
        close(probe->fd);
    probe->fd = -1;
    free(probe->mapping);
    probe->mapping = NULL;
    free(probe->response);
    probe->response = NULL;
    memset(probe->request, 0, sizeof(probe->request));
    memset(probe->target.token, 0, sizeof(probe->target.token));
}
static void probe_finish(ReceiverProbe *probe, const char *error, uint64_t now) {
    probe_close(probe);
    probe->elapsed = now - probe->started;
    probe->stage = error ? TEST_FAILED : TEST_DONE;
    snprintf(probe->error, sizeof(probe->error), "%s", error ? error : "");
}
static void probe_request(ReceiverProbe *probe, bool login, uint64_t now) {
    if (login)
        snprintf(probe->request, sizeof(probe->request),
                 "{\"command\":\"authorize\",\"subcommand\":\"login\",\"token\":\"%s\"}\n",
                 probe->target.token);
    else
        snprintf(probe->request, sizeof(probe->request), "{\"command\":\"serverinfo\"}\n");
    probe->sent = probe->used = 0;
    probe->after_send = login ? TEST_LOGIN : TEST_LAYOUT;
    probe->stage = TEST_SEND;
    probe->deadline = now + 3000;
}
static bool probe_begin(ReceiverProbe *probe, const TargetConfig *target, uint64_t now) {
    if (probe_busy(probe))
        return false;
    probe_close(probe);
    memset(probe, 0, sizeof(*probe));
    probe->fd = -1;
    probe->target = *target;
    probe->started = now;
    probe->response = malloc(2 * 1024 * 1024 + 1);
    if (!probe->response) {
        probe_finish(probe, "Cannot allocate connection test buffer", now);
        return true;
    }
    probe->fd = target_open(target);
    if (probe->fd < 0) {
        probe_finish(probe, "Cannot open receiver connection", now);
        return true;
    }
    probe->stage = TEST_CONNECT;
    probe->deadline = now + 3000;
    return true;
}
static void probe_tick(ReceiverProbe *probe, PlaybackJson *json, uint64_t now) {
    if (!probe_busy(probe))
        return;
    if (now >= probe->deadline) {
        probe_finish(probe, "Receiver timed out", now);
        return;
    }
    int fd = probe->fd;
    if (probe->stage == TEST_CONNECT) {
        struct pollfd p = {fd, POLLOUT, 0};
        int error = 0;
        socklen_t size = sizeof(error);
        if (poll(&p, 1, 0) <= 0)
            return;
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error) {
            probe_finish(probe, "Receiver connection refused or unreachable", now);
            return;
        }
        probe_request(probe, probe->target.token[0] != 0, now);
        return;
    }
    if (probe->stage == TEST_SEND) {
        size_t length = strlen(probe->request);
        ssize_t n = send(fd, probe->request + probe->sent, length - probe->sent, MSG_NOSIGNAL);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return;
        if (n <= 0) {
            probe_finish(probe, "Receiver closed the test connection", now);
            return;
        }
        probe->sent += (size_t)n;
        if (probe->sent == length) {
            probe->stage = probe->after_send;
            probe->deadline = now + 3000;
            memset(probe->request, 0, sizeof(probe->request));
        }
        return;
    }
    size_t available = 2 * 1024 * 1024 - probe->used;
    if (available > 65536)
        available = 65536;
    if (!available) {
        probe_finish(probe, "Receiver response exceeds 2 MiB", now);
        return;
    }
    ssize_t n = recv(fd, probe->response + probe->used, available, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return;
    if (n <= 0) {
        probe_finish(probe, "Receiver closed the test connection", now);
        return;
    }
    if (memchr(probe->response + probe->used, 0, (size_t)n)) {
        probe_finish(probe, "Receiver sent an embedded NUL", now);
        return;
    }
    probe->used += (size_t)n;
    probe->response[probe->used] = 0;
    char *end = strchr(probe->response, '\n');
    if (!end)
        return;
    *end = 0;
    void *root = playback_parse_exact(json, probe->response);
    bool success = playback_true(json, root, "success");
    if (root)
        json->put(root);
    if (!success) {
        probe_finish(probe,
                     probe->stage == TEST_LOGIN
                         ? "Receiver authentication failed"
                         : "Receiver rejected serverinfo; check authorization",
                     now);
        return;
    }
    if (probe->stage == TEST_LOGIN) {
        probe_request(probe, false, now);
        return;
    }
    LedBlend *mapping = NULL;
    unsigned count = 0;
    if (!layout_from_json(probe->response, &mapping, &count)) {
        probe_finish(probe, "Receiver returned an invalid or empty LED layout", now);
        return;
    }
    probe->leds = count;
    probe_finish(probe, NULL, now);
    probe->mapping = mapping;
}
static inline const char *test_stage_name(void) {
    return probe_stage_name(&connection_test);
}
static inline bool test_busy(void) {
    return probe_busy(&connection_test);
}
static inline void test_close(void) {
    probe_close(&connection_test);
}
static inline bool test_begin(const TargetConfig *target, uint64_t now) {
    return probe_begin(&connection_test, target, now);
}
static inline void test_tick(PlaybackJson *json, uint64_t now) {
    probe_tick(&connection_test, json, now);
}
#endif
