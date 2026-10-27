#ifndef HYPERDRM_TARGET_H
#define HYPERDRM_TARGET_H
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "playback.h"
#include "atomic_file.h"
#define TARGET_PREF                                                                                \
    "/media/developer/apps/usr/palm/services/org.webosbrew.histogramhyperion.service/target.json"
typedef struct {
    char host[46], receiver[16], token[257];
    unsigned port, piccap_port, capture_interval;
} TargetConfig;
static TargetConfig target = {
    .host = "127.0.0.1", .receiver = "Hyperion", .port = 19444, .piccap_port = 19400,
    .capture_interval = 50};
static inline unsigned target_capture_interval(const TargetConfig *t) {
    return t->capture_interval ? t->capture_interval : 50;
}
static inline bool target_piccap_local(const TargetConfig *t) {
    return t->port == 19444 && (!strcmp(t->host, "127.0.0.1") || !strcmp(t->host, "::1"));
}
static inline bool target_piccap_compatible(const TargetConfig *t) {
    unsigned char address[4];
    return !strcmp(t->host, "::1") || inet_pton(AF_INET, t->host, address) == 1;
}
static inline const char *target_piccap_host(const TargetConfig *t) {
    return !strcmp(t->host, "::1") ? "127.0.0.1" : t->host;
}
static bool target_valid(const TargetConfig *t) {
    unsigned char address[16];
    unsigned interval = target_capture_interval(t);
    if (interval < 25 || interval > 1000 || interval % 25)
        return false;
    if (inet_pton(AF_INET, t->host, address) != 1 && inet_pton(AF_INET6, t->host, address) != 1)
        return false;
    if (!t->piccap_port || t->piccap_port > 65535)
        return false;
    if (!t->port || t->port > 65535 ||
        (strcmp(t->receiver, "Hyperion") && strcmp(t->receiver, "HyperHDR")))
        return false;
    for (const unsigned char *p = (const unsigned char *)t->token; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == '_' || *p == '.'))
            return false;
    return true;
}
static bool target_parse(PlaybackJson *j, const char *text, TargetConfig *out) {
    void *root = playback_parse_exact(j, text);
    if (!root)
        return false;
    TargetConfig next = *out;
    bool valid = j->type(root) == 4;
    const char *names[] = {"host", "receiver", "token"};
    char *fields[] = {next.host, next.receiver, next.token};
    size_t sizes[] = {sizeof(next.host), sizeof(next.receiver), sizeof(next.token)};
    for (unsigned i = 0; valid && i < 3; i++) {
        void *v = playback_field(j, root, names[i]);
        if (!v) {
            if (i < 2)
                valid = false;
            continue;
        }
        if (j->type(v) != 6) {
            valid = false;
            break;
        }
        const char *s = j->string(v);
        if (strlen(s) >= sizes[i]) {
            valid = false;
            break;
        }
        strcpy(fields[i], s);
    }
    void *port = playback_field(j, root, "port");
    if (!port || j->type(port) != 3)
        valid = false;
    else {
        char *end = NULL;
        errno = 0;
        long n = strtol(j->string(port), &end, 10);
        if (errno || !end || *end || n < 1 || n > 65535)
            valid = false;
        else
            next.port = (unsigned)n;
    }
    void *flat = NULL;
    if (j->type(root) == 4 && j->get(root, "piccapPort", &flat)) {
        if (j->type(flat) != 3)
            valid = false;
        else {
            char *end = NULL;
            errno = 0;
            long n = strtol(j->string(flat), &end, 10);
            if (errno || !end || *end || n < 1 || n > 65535)
                valid = false;
            else
                next.piccap_port = (unsigned)n;
        }
    }
    void *interval = NULL;
    if (j->type(root) == 4 && j->get(root, "captureIntervalMs", &interval)) {
        if (j->type(interval) != 3)
            valid = false;
        else {
            char *end = NULL;
            errno = 0;
            long n = strtol(j->string(interval), &end, 10);
            if (errno || !end || *end || n < 25 || n > 1000 || n % 25)
                valid = false;
            else
                next.capture_interval = (unsigned)n;
        }
    }
    void *clear = playback_field(j, root, "clearToken");
    if (clear) {
        if (j->type(clear) != 1)
            valid = false;
        else if (j->boolean(clear))
            next.token[0] = 0;
    }
    valid = valid && target_valid(&next);
    j->put(root);
    if (valid)
        *out = next;
    return valid;
}
static bool target_save(const char *path, const TargetConfig *t) {
    if (!target_valid(t))
        return false;
    char data[512];
    int n = snprintf(
        data, sizeof(data),
        "{\"host\":\"%s\",\"port\":%u,\"piccapPort\":%u,\"receiver\":\"%s\",\"token\":\"%s\",\"captureIntervalMs\":%u}\n",
        t->host, t->port, t->piccap_port, t->receiver, t->token, target_capture_interval(t));
    return n > 0 && (size_t)n < sizeof(data) &&
           atomic_replace(path, data, (size_t)n) != SAVE_UNCHANGED;
}
static bool target_load(PlaybackJson *j, const char *path, TargetConfig *t) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return false;
    char data[1024];
    ssize_t n = read(fd, data, sizeof(data) - 1);
    close(fd);
    if (n <= 0 || n == (ssize_t)sizeof(data) - 1)
        return false;
    data[n] = 0;
    return target_parse(j, data, t);
}
static int target_open(const TargetConfig *t) {
    if (!target_valid(t))
        return -1;
    struct sockaddr_storage storage = {0};
    socklen_t length;
    int family;
    struct sockaddr_in *v4 = (void *)&storage;
    struct sockaddr_in6 *v6 = (void *)&storage;
    if (inet_pton(AF_INET, t->host, &v4->sin_addr) == 1) {
        family = AF_INET;
        v4->sin_family = family;
        v4->sin_port = htons(t->port);
        length = sizeof(*v4);
    } else {
        family = AF_INET6;
        v6->sin6_family = family;
        v6->sin6_port = htons(t->port);
        length = sizeof(*v6);
    }
    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
    if (connect(fd, (void *)&storage, length) != 0) {
        if (errno != EINPROGRESS) {
            close(fd);
            return -1;
        }
    }
    return fd;
}
#endif
