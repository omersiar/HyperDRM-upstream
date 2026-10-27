#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include <sys/wait.h>
#include "target.h"
#include "layout.h"
#include "receiver_stream.h"
static void fixture(PlaybackJson *json, bool modern, bool login) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr = {htonl(INADDR_LOOPBACK)}};
    assert(!bind(server, (void *)&address, sizeof(address)) && !listen(server, 1));
    socklen_t size = sizeof(address);
    assert(!getsockname(server, (void *)&address, &size));
    target.port = ntohs(address.sin_port);
    snprintf(target.token, sizeof(target.token), "%s", login ? "fixture-token" : "");
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        int fd = accept(server, NULL, NULL);
        if (fd < 0)
            _exit(1);
        FILE *input = fdopen(dup(fd), "r");
        char request[512];
        if (login) {
            if (!fgets(request, sizeof(request), input) || !strstr(request, "authorize"))
                _exit(2);
            write(fd, "{\"success\":true}\n", 17);
        }
        if (!fgets(request, sizeof(request), input) || !strstr(request, "serverinfo"))
            _exit(3);
        const char *layout = "{\"success\":true,\"info\":{\"leds\":[{\"hmin\":0,\"hmax\":1,"
                             "\"vmin\":0,\"vmax\":0.1}]}}\n";
        write(fd, layout, strlen(layout));
        if (!fgets(request, sizeof(request), input) || !strstr(request, "ledstream-start"))
            _exit(4);
        const char *reply = modern ? "{\"success\":true,\"command\":\"ledcolors-ledstream-update\","
                                     "\"data\":{\"leds\":[20,30,40]}}\n"
                                   : "{\"success\":true,\"command\":\"ledcolors-ledstream-update\","
                                     "\"result\":{\"leds\":[20,30,40]}}\n";
        write(fd, reply, 13);
        usleep(20000);
        write(fd, reply + 13, strlen(reply) - 13);
        usleep(500000);
        close(fd);
        fclose(input);
        close(server);
        _exit(0);
    }
    stream_close(0);
    stream.retry_at = 0;
    for (unsigned i = 0; i < 500 && !stream.valid; i++) {
        stream_tick(json, 1000 + i * 5, true);
        usleep(1000);
    }
    assert(stream.valid && stream.sequence == 1 && stream.count == 1);
    for (unsigned r = 0; r < 16; r++) {
        assert(stream.colors[r][0] == 20 && stream.colors[r][1] == 30 && stream.colors[r][2] == 40);
    }
    /* More than the eight-message budget, with no further socket writes. */
    const char *buffered =
        "{\"command\":\"ledcolors-ledstream-update\",\"data\":{\"leds\":[50,60,70]}}\n";
    stream.used = 0;
    for (unsigned i = 0; i < 17; i++) {
        memcpy(stream.data + stream.used, buffered, strlen(buffered));
        stream.used += strlen(buffered);
    }
    stream.data[stream.used] = 0;
    for (unsigned i = 0; i < 3; i++)
        stream_tick(json, 1900 + i, true);
    assert(stream.sequence == 18 && stream.used == 0 && stream.colors[0][0] == 50);
    void *bad = json->parse("{\"result\":{\"leds\":[0,0,999]}}");
    assert(!stream_colors(json, bad, 2000));
    json->put(bad);
    assert(stream.colors[0][2] == 70);
    bad = json->parse("{\"data\":{\"leds\":[0,0,0]}}");
    assert(stream_colors(json, bad, 2000));
    json->put(bad);
    assert(stream.colors[0][2] == 0);
    bad = json->parse("{\"data\":{\"leds\":[1,2,3,4,5,6]}}");
    assert(!stream_colors(json, bad, 2000) && !stream.revision);
    json->put(bad);
    stream_tick(json, 2001, false);
    assert(stream.fd < 0 && !stream.valid);
    int status;
    waitpid(child, &status, 0);
    assert(WIFEXITED(status) && !WEXITSTATUS(status));
    close(server);
}
int main(void) {
    PlaybackJson json = {0};
    assert(playback_json_init(&json));
    fixture(&json, false, false);
    fixture(&json, true, true);
    stream_start(1000);
    stream_tick(&json, 5000, true);
    assert(stream.fd < 0);
    stream_close(0);
    TargetConfig copy = target;
    assert(target_valid(&copy));
    assert(target_piccap_compatible(&copy));
    assert(!target_piccap_local(&copy));
    assert(target_piccap_host(&copy));
    assert(!target_parse(&json, "{}", &copy));
    assert(!target_save("/no/such/profile", &copy));
    assert(!target_load(&json, "/no/such/profile", &copy));
    int fd = target_open(&copy);
    if (fd >= 0)
        close(fd);
    dlclose(json.lib);
    puts("PASS: fragmented raw LED subscription, token login, old/new envelopes, invalid samples, "
         "real black, timeout and cleanup");
    return 0;
}
