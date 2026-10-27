#define _DEFAULT_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool interrupt_write, short_write, fail_write, fail_sync, fail_rename, fail_commit_sync;
static unsigned directory_syncs;
static ssize_t fixture_write(int fd, const void *data, size_t n) {
    if (interrupt_write) {
        interrupt_write = false;
        errno = EINTR;
        return -1;
    }
    if (fail_write) {
        errno = ENOSPC;
        return -1;
    }
    return write(fd, data, short_write && n > 2 ? 2 : n);
}
static int fixture_sync(int fd) {
    struct stat info;
    assert(!fstat(fd, &info));
    if (S_ISDIR(info.st_mode)) {
        directory_syncs++;
        if (fail_commit_sync && directory_syncs == 2) {
            errno = EIO;
            return -1;
        }
    } else if (fail_sync) {
        errno = EIO;
        return -1;
    }
    return fsync(fd);
}
static int fixture_rename(int from, const char *a, int to, const char *b) {
    if (fail_rename) {
        errno = EACCES;
        return -1;
    }
    return renameat(from, a, to, b);
}
#define ATOMIC_WRITE fixture_write
#define ATOMIC_SYNC fixture_sync
#define ATOMIC_RENAME fixture_rename
#include "atomic_file.h"
static void contents(const char *path, const char *expected) {
    char data[64] = {0};
    int fd = open(path, O_RDONLY);
    assert(fd >= 0);
    assert(read(fd, data, sizeof(data) - 1) == (ssize_t)strlen(expected));
    close(fd);
    assert(!strcmp(data, expected));
}
int main(void) {
    char directory[] = "/tmp/hyperdrm-save-XXXXXX";
    assert(mkdtemp(directory));
    char path[128], stale[128], link[128];
    snprintf(path, sizeof(path), "%s/settings.json", directory);
    snprintf(stale, sizeof(stale), "%s/settings.json.tmp", directory);
    snprintf(link, sizeof(link), "%s/link", directory);
    int fd = open(stale, O_CREAT | O_WRONLY, 0600);
    assert(fd >= 0);
    close(fd);
    assert(atomic_replace(path, "old", 3) == SAVE_DURABLE);
    interrupt_write = short_write = true;
    assert(atomic_replace(path, "partial writes handled", 22) == SAVE_DURABLE);
    contents(path, "partial writes handled");
    short_write = false;
    fail_write = true;
    assert(atomic_replace(path, "new", 3) == SAVE_UNCHANGED);
    fail_write = false;
    contents(path, "partial writes handled");
    fail_sync = true;
    assert(atomic_replace(path, "new", 3) == SAVE_UNCHANGED);
    fail_sync = false;
    fail_rename = true;
    assert(atomic_replace(path, "new", 3) == SAVE_UNCHANGED);
    fail_rename = false;
    contents(path, "partial writes handled");
    directory_syncs = 0;
    fail_commit_sync = true;
    assert(atomic_replace(path, "committed", 9) == SAVE_COMMITTED);
    contents(path, "committed");
    fail_commit_sync = false;
    assert(!symlink(path, link));
    assert(atomic_replace(link, "bad", 3) == SAVE_UNCHANGED);
    contents(path, "committed");
    struct stat info;
    assert(!stat(path, &info) && (info.st_mode & 0777) == 0600);
    assert(!access(stale, F_OK));
    unlink(link);
    unlink(path);
    unlink(stale);
    assert(!rmdir(directory));
    puts("PASS: interrupted/partial writes, write/sync/rename failures, atomic replacement, commit "
         "durability, private permissions, symlinks and stale files");
}
