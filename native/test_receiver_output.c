#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include "target.h"
#include "layout.h"
#include "receiver_output.h"
static unsigned request_tan(const char *request) {
    const char *p = strstr(request, "\"tan\":");
    assert(p);
    return (unsigned)strtoul(p + 6, NULL, 10);
}
static void ack(int fd, unsigned id, bool layout) {
    char data[256];
    int n = snprintf(data, sizeof(data),
                     layout ? "{\"success\":true,\"tan\":%u,\"info\":{\"leds\":[{\"hmin\":0,"
                              "\"hmax\":1,\"vmin\":0,\"vmax\":0.1}]}}\n"
                            : "{\"success\":true,\"tan\":%u}\n",
                     id);
    assert(send(fd, data, 7, MSG_NOSIGNAL) == 7);
    usleep(1000);
    assert(send(fd, data + 7, n - 7, MSG_NOSIGNAL) == n - 7);
}
static void fixture(PlaybackJson *json, int mode) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr = {htonl(INADDR_LOOPBACK)}};
    assert(!bind(server, (void *)&address, sizeof(address)) && !listen(server, 1));
    socklen_t size = sizeof(address);
    assert(!getsockname(server, (void *)&address, &size));
    TargetConfig config = target;
    config.port = ntohs(address.sin_port);
    strcpy(config.token, "fixture");
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        signal(SIGPIPE, SIG_IGN);
        int fd = accept(server, NULL, NULL);
        assert(fd >= 0);
        FILE *input = fdopen(dup(fd), "r");
        char line[1024];
        assert(fgets(line, sizeof(line), input));
        assert(strstr(line, "authorize"));
        ack(fd, request_tan(line), false);
        assert(fgets(line, sizeof(line), input) && strstr(line, "serverinfo"));
        unsigned id = request_tan(line);
        if (mode == 1) {
            send(fd, "{\"success\":true", 15, MSG_NOSIGNAL);
            goto done;
        }
        if (mode == 2) {
            send(fd, "{} garbage\n", 11, MSG_NOSIGNAL);
            goto done;
        }
        if (mode == 3) {
            send(fd, "[]\n", 3, MSG_NOSIGNAL);
            goto done;
        }
        if (mode == 4) {
            char *huge = malloc(2 * 1024 * 1024 + 64);
            assert(huge);
            memset(huge, 'x', 2 * 1024 * 1024 + 64);
            size_t sent = 0;
            while (sent < 2 * 1024 * 1024 + 64) {
                ssize_t n = send(fd, huge + sent, 2 * 1024 * 1024 + 64 - sent, MSG_NOSIGNAL);
                if (n <= 0)
                    break;
                sent += (size_t)n;
            }
            free(huge);
            goto done;
        }
        if (mode == 6) {
            const char bytes[] = "{}\0ignored\n";
            send(fd, bytes, sizeof(bytes) - 1, MSG_NOSIGNAL);
            goto done;
        }
        if (mode == 5) {
            char bad[128];
            int n = snprintf(bad, sizeof(bad), "{\"success\":\"true\",\"tan\":%u}\n", id);
            send(fd, bad, n, MSG_NOSIGNAL);
            goto done;
        }
        ack(fd, id, true);
        assert(fgets(line, sizeof(line), input) && strstr(line, "[1,2,3]"));
        id = request_tan(line);
        /* Unrelated/stale acknowledgements must not release the in-flight frame. */
        ack(fd, id + 99, false);
        usleep(50000);
        ack(fd, id, false);
        assert(fgets(line, sizeof(line), input) && strstr(line, "[7,8,9]"));
        ack(fd, request_tan(line), false);
        assert(fgets(line, sizeof(line), input) && strstr(line, "clear"));
        ack(fd, request_tan(line), false);
    done:
        usleep(20000);
        fclose(input);
        close(fd);
        close(server);
        _exit(0);
    }
    ReceiverOutput output = {.fd = -1};
    assert(output_begin(&output, &config, 1000));
    bool first = false, queued = false, stopping = false;
    unsigned iterations = 0;
    for (; iterations < 5000; iterations++) {
        output_tick(&output, json, 1000 + iterations);
        if (output.acknowledgements == 1 && queued && !stopping)
            assert(output.awaiting && !output.latest);
        if (output.stage == OUTPUT_FAILED || (stopping && output.stage == OUTPUT_IDLE))
            break;
        if (output.stage == OUTPUT_READY && !first) {
            first = true;
            assert(output_frame(&output, "{\"command\":\"color\",\"color\":[1,2,3]}\n"));
        } else if (first && output.awaiting && !queued) {
            queued = true;
            assert(output_frame(&output, "{\"command\":\"color\",\"color\":[4,5,6]}\n"));
            assert(output_frame(&output, "{\"command\":\"color\",\"color\":[7,8,9]}\n"));
        }
        if (output.acknowledgements == 2 && !stopping) {
            stopping = true;
            output_stop(&output, 1000 + iterations, true);
        }
        usleep(1000);
    }
    assert(iterations < 5000);
    if (mode)
        assert(output.stage == OUTPUT_FAILED);
    else
        assert(first && queued && stopping && output.stage == OUTPUT_IDLE);
    output_close(&output);
    int status;
    waitpid(child, &status, 0);
    assert(WIFEXITED(status) && !WEXITSTATUS(status));
    close(server);
}
int main(void) {
    (void)target_open;
    (void)target_parse;
    (void)target_load;
    (void)target_save;
    PlaybackJson json = {0};
    assert(playback_json_init(&json));
    for (int mode = 0; mode <= 6; mode++)
        fixture(&json, mode);
    ReceiverOutput output = {.fd = -1};
    int sockets[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    output.fd = sockets[0];
    output.stage = OUTPUT_READY;
    output.incoming = malloc(2 * 1024 * 1024 + 1);
    assert(output.incoming);
    assert(!fcntl(sockets[0], F_SETFL, O_NONBLOCK));
    char block[4096] = {0};
    while (send(sockets[0], block, sizeof(block), MSG_NOSIGNAL) > 0) {
    }
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    assert(output_frame(&output, "{\"command\":\"color\",\"color\":[0,0,0]}\n"));
    output_tick(&output, &json, 1000);
    assert(output.stage == OUTPUT_READY && output.request && output.sent == 0);
    output_tick(&output, &json, 4000);
    assert(output.stage == OUTPUT_FAILED);
    close(sockets[1]);
    output_close(&output);
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    output.fd = sockets[0];
    output.stage = OUTPUT_READY;
    output.incoming = malloc(2 * 1024 * 1024 + 1);
    assert(output.incoming);
    close(sockets[1]);
    assert(output_frame(&output, "{\"command\":\"clear\"}\n"));
    output_tick(&output, &json, 5000);
    assert(output.stage == OUTPUT_FAILED);
    output_close(&output);
    dlclose(json.lib);
    puts("PASS: asynchronous login/layout, fragmented and unrelated replies, latest-frame "
         "coalescing, clear/stop, truncation, invalid JSON/types, oversized responses, send "
         "backpressure, timeout and disconnect");
}
