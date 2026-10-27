#ifndef HYPERDRM_ATOMIC_FILE_H
#define HYPERDRM_ATOMIC_FILE_H
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef ATOMIC_WRITE
#define ATOMIC_WRITE write
#endif
#ifndef ATOMIC_SYNC
#define ATOMIC_SYNC fsync
#endif
#ifndef ATOMIC_RENAME
#define ATOMIC_RENAME renameat
#endif
typedef enum { SAVE_UNCHANGED, SAVE_DURABLE, SAVE_COMMITTED } SaveResult;
/* Failure before rename preserves the previous file. After rename, a directory
 * sync failure cannot truthfully be called a rollback: report committed with
 * uncertain durability, so memory and disk never deliberately diverge. */
static inline SaveResult atomic_replace(const char *path, const void *data, size_t length) {
    char parent[512], temporary[544];
    if (!path || !data || strlen(path) >= sizeof(parent))
        return SAVE_UNCHANGED;
    strcpy(parent, path);
    char *slash = strrchr(parent, '/');
    const char *name = slash ? strrchr(path, '/') + 1 : path;
    if (!*name || !strcmp(name, ".") || !strcmp(name, ".."))
        return SAVE_UNCHANGED;
    if (slash) {
        if (slash == parent)
            slash[1] = 0;
        else
            *slash = 0;
    } else
        strcpy(parent, ".");
    int dir = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dir < 0)
        return SAVE_UNCHANGED;
    struct stat existing;
    int found = fstatat(dir, name, &existing, AT_SYMLINK_NOFOLLOW);
    if ((found < 0 && errno != ENOENT) || (found == 0 && !S_ISREG(existing.st_mode)) ||
        ATOMIC_SYNC(dir) != 0) {
        close(dir);
        return SAVE_UNCHANGED;
    }
    if (snprintf(temporary, sizeof(temporary), "%s/.%s.tmp.XXXXXX", parent, name) >=
        (int)sizeof(temporary)) {
        close(dir);
        return SAVE_UNCHANGED;
    }
    int fd = mkstemp(temporary);
    if (fd < 0) {
        close(dir);
        return SAVE_UNCHANGED;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    const unsigned char *bytes = data;
    size_t written = 0;
    bool ok = true;
    while (written < length) {
        ssize_t n = ATOMIC_WRITE(fd, bytes + written, length - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        written += (size_t)n;
    }
    if (ok)
        ok = ATOMIC_SYNC(fd) == 0;
    if (close(fd) != 0)
        ok = false;
    const char *tempname = strrchr(temporary, '/') + 1;
    if (ok)
        ok = ATOMIC_RENAME(dir, tempname, dir, name) == 0;
    SaveResult result = SAVE_UNCHANGED;
    if (ok) {
        result = ATOMIC_SYNC(dir) == 0 ? SAVE_DURABLE : SAVE_COMMITTED;
        if (result == SAVE_COMMITTED)
            fprintf(stderr, "Preference committed; directory sync failed\n");
    } else
        unlinkat(dir, tempname, 0);
    close(dir);
    return result;
}
#endif
