#define _DEFAULT_SOURCE
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include "flatbuffer_output.h"

enum { WIDTH = 160, HEIGHT = 90, PIXELS = WIDTH * HEIGHT * 3, CAPACITY = PIXELS + 512 };

static uint16_t rd16(const uint8_t *b, size_t size, size_t at) {
    assert(at + 2 <= size);
    return (uint16_t)(b[at] | (b[at + 1] << 8));
}
static uint32_t rd32(const uint8_t *b, size_t size, size_t at) {
    assert(at + 4 <= size);
    return (uint32_t)b[at] | ((uint32_t)b[at + 1] << 8) | ((uint32_t)b[at + 2] << 16) |
           ((uint32_t)b[at + 3] << 24);
}
static size_t field(const uint8_t *b, size_t size, size_t table, unsigned id) {
    size_t vtable = (size_t)((int64_t)table - (int32_t)rd32(b, size, table));
    unsigned vtable_size = rd16(b, size, vtable);
    if (4 + 2 * id + 2 > vtable_size)
        return 0;
    unsigned offset = rd16(b, size, vtable + 4 + 2 * id);
    return offset ? table + offset : 0;
}
static size_t child(const uint8_t *b, size_t size, size_t table, unsigned id) {
    size_t pos = field(b, size, table, id);
    assert(pos);
    return pos + rd32(b, size, pos);
}
static size_t root(const uint8_t *b, size_t size) {
    size_t at = rd32(b, size, 0);
    assert(at + 4 <= size);
    return at;
}
/* Replies are length-prefixed big-endian, exactly like requests. */
static void reply_send(int fd, const uint8_t *body, size_t size) {
    uint8_t frame[64];
    assert(size + 4 <= sizeof(frame));
    frame[0] = (uint8_t)(size >> 24);
    frame[1] = (uint8_t)(size >> 16);
    frame[2] = (uint8_t)(size >> 8);
    frame[3] = (uint8_t)size;
    memcpy(frame + 4, body, size);
    /* The client closes right after a clear, so a refused reply is expected. */
    (void)send(fd, frame, size + 4, MSG_NOSIGNAL);
}
/* Reply without any field: vtable(4) + table(4), no error text. */
static void reply_ok(int fd) {
    static const uint8_t message[] = {12, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0, 8, 0, 0, 0};
    reply_send(fd, message, sizeof(message));
}
/* Reply with an error string at 24: server rejects the request. */
static void reply_error(int fd, const char *text) {
    uint8_t message[64] = {0};
    size_t n = strlen(text);
    message[0] = 12;                        /* root -> table at 12 */
    message[4] = 6;                         /* vtable size */
    message[6] = 8;                         /* table size */
    message[8] = 4;                         /* error field offset */
    message[12] = 8;                        /* soffset -> vtable at 4 */
    message[16] = 8;                        /* error uoffset -> string at 16+8 */
    message[24] = (uint8_t)n;               /* string length */
    memcpy(message + 28, text, n);
    reply_send(fd, message, 28 + n + 1);
}
static size_t read_frame(int fd, uint8_t *body, size_t capacity) {
    uint8_t header[4];
    size_t got = 0;
    while (got < 4) {
        ssize_t n = recv(fd, header + got, 4 - got, 0);
        if (n <= 0)
            return 0;
        got += (size_t)n;
    }
    size_t size = ((size_t)header[0] << 24) | ((size_t)header[1] << 16) |
                  ((size_t)header[2] << 8) | (size_t)header[3];
    assert(size && size <= capacity);
    got = 0;
    while (got < size) {
        ssize_t n = recv(fd, body + got, size - got, 0);
        if (n <= 0)
            return 0;
        got += (size_t)n;
    }
    return size;
}
static void run_fixture(PlaybackJson *json, int mode) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr = {htonl(INADDR_LOOPBACK)}};
    assert(!bind(server, (void *)&address, sizeof(address)) && !listen(server, 1));
    socklen_t length = sizeof(address);
    assert(!getsockname(server, (void *)&address, &length));
    TargetConfig config = target;
    strcpy(config.receiver, "HyperHDR");
    strcpy(config.output, "flatbuffer");
    config.piccap_port = ntohs(address.sin_port);
    pid_t worker = fork();
    assert(worker >= 0);
    if (!worker) {
        signal(SIGPIPE, SIG_IGN);
        int fd = accept(server, NULL, NULL);
        assert(fd >= 0);
        static uint8_t body[CAPACITY];
        /* 1. Registration: command type Register, origin and priority. */
        size_t size = read_frame(fd, body, sizeof(body));
        assert(size > 16);
        size_t request = root(body, size);
        size_t type = field(body, size, request, 0);
        assert(type && body[type] == 4);
        size_t reg = child(body, size, request, 1);
        size_t priority = field(body, size, reg, 1);
        assert(priority && (int32_t)rd32(body, size, priority) == 149);
        size_t origin = field(body, size, reg, 0);
        assert(origin);
        size_t text = origin + rd32(body, size, origin);
        assert(rd32(body, size, text) == 8 && !memcmp(body + text + 4, "HyperDRM", 8));
        reply_ok(fd);
        /* 2. Two frames queued before a tick: only the newest is sent. */
        size = read_frame(fd, body, sizeof(body));
        assert(size > 80);
        request = root(body, size);
        type = field(body, size, request, 0);
        assert(type && body[type] == 2);
        size_t image = child(body, size, request, 1);
        size_t duration = field(body, size, image, 2);
        assert(duration && (int32_t)rd32(body, size, duration) == 1500);
        size_t raw = child(body, size, image, 1);
        size_t width = field(body, size, raw, 1), height = field(body, size, raw, 2);
        assert(width && height);
        assert((int32_t)rd32(body, size, width) == WIDTH);
        assert((int32_t)rd32(body, size, height) == HEIGHT);
        size_t data = child(body, size, raw, 0);
        assert(rd32(body, size, data) == (uint32_t)PIXELS);
        assert(body[data + 4] == 0x42); /* newest frame wins */
        assert(data + 4 + PIXELS == size);
        if (mode == 0) {
            reply_ok(fd);
            /* 3. Shutdown sends a clear for the same priority. */
            size = read_frame(fd, body, sizeof(body));
            assert(size > 16);
            request = root(body, size);
            type = field(body, size, request, 0);
            assert(type && body[type] == 3);
            size_t clear = child(body, size, request, 1);
            size_t cleared = field(body, size, clear, 0);
            assert(cleared && (int32_t)rd32(body, size, cleared) == 149);
            reply_ok(fd);
        } else {
            reply_error(fd, "synthetic rejection");
        }
        usleep(20000);
        close(fd);
        close(server);
        _exit(0);
    }
    FlatbufferOutput output = {.fd = -1};
    assert(fb_out_begin(&output, &config, 149, CAPACITY, 1000));
    uint8_t *plane = malloc(PIXELS);
    assert(plane);
    memset(plane, 0x41, PIXELS);
    bool sent = false, stopped = false;
    for (unsigned step = 0; step < 4000; step++) {
        uint64_t now = 1000 + step;
        fb_out_tick(&output, now);
        if (output.stage == FB_READY && !sent) {
            sent = true;
            assert(fb_out_frame(&output, plane, WIDTH, HEIGHT, 1500, now));
            plane[0] = 0x42; /* replaces the queued frame before it was sent */
            assert(fb_out_frame(&output, plane, WIDTH, HEIGHT, 1500, now));
            fb_out_tick(&output, now);
        }
        if (mode == 0 && output.frames >= 1 && !stopped) {
            stopped = true;
            fb_out_stop(&output, now, true);
        }
        if (output.stage == FB_FAILED || (stopped && output.stage == FB_IDLE))
            break;
        usleep(500);
    }
    if (mode == 0) {
        assert(sent && stopped && output.frames == 1 && output.stage == FB_IDLE);
    } else {
        assert(sent && output.stage == FB_FAILED);
        assert(strstr(output.error, "synthetic rejection"));
    }
    free(plane);
    fb_out_release(&output);
    int status;
    waitpid(worker, &status, 0);
    assert(WIFEXITED(status) && !WEXITSTATUS(status));
    close(server);
    (void)json;
}
int main(void) {
    (void)target_open;
    (void)target_parse;
    (void)target_load;
    (void)target_save;
    PlaybackJson json = {0};
    assert(playback_json_init(&json));
    run_fixture(&json, 0);
    run_fixture(&json, 1);
    dlclose(json.lib);
    puts("PASS: flatbuffer register/image/clear framing, newest-frame coalescing, server error "
         "propagation");
    return 0;
}
