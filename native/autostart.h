#ifndef HISTOGRAM_AUTOSTART_H
#define HISTOGRAM_AUTOSTART_H
#ifndef BOOT_DIRECTORY
#define BOOT_DIRECTORY "/var/lib/webosbrew/init.d"
#endif
#ifndef BOOT_SCRIPT
#define BOOT_SCRIPT                                                                                \
    "/media/developer/apps/usr/palm/services/org.webosbrew.histogramhyperion.service/boot.sh"
#endif
#define BOOT_HOOK BOOT_DIRECTORY "/60-histogramhyperion"
/* Exact pre-0.6.1 hook; only this known own file may be migrated. */
static const char legacy_boot_script[] =
    "#!/bin/sh\n# Histogram Hyperion own boot hook v1\n(\n  attempt=0\n  while [ -x "
    "/var/lib/webosbrew/init.d/60-histogramhyperion ] && [ \"$attempt\" -lt 120 ]; do\n    "
    "result=$(timeout 12 luna-send -n 1 luna://org.webosbrew.histogramhyperion.service/start '{}' "
    "2>&1)\n    case \"$result\" in\n      *'\"running\":true'*) exit 0 ;;\n    esac\n    "
    "attempt=$((attempt + 1))\n    sleep 5\n  done\n  echo 'Histogram Hyperion boot start "
    "cancelled or timed out'\n) >/var/lib/webosbrew/histogramhyperion-boot.log 2>&1 </dev/null "
    "&\nexit 0\n";
/* 0: absent, 1: own legacy copy, 2: own symlink, -1: foreign/error. */
static int boot_hook_kind(void) {
    struct stat st;
    if (lstat(BOOT_HOOK, &st) != 0)
        return errno == ENOENT ? 0 : -1;
    if (S_ISLNK(st.st_mode)) {
        char target[sizeof(BOOT_SCRIPT) + 1];
        ssize_t n = readlink(BOOT_HOOK, target, sizeof(target));
        return n == (ssize_t)(sizeof(BOOT_SCRIPT) - 1) &&
                       memcmp(target, BOOT_SCRIPT, (size_t)n) == 0
                   ? 2
                   : -1;
    }
    if (!S_ISREG(st.st_mode))
        return -1;
    char data[sizeof(legacy_boot_script)];
    int fd = open(BOOT_HOOK, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, data, sizeof(data));
    close(fd);
    return n == (ssize_t)(sizeof(legacy_boot_script) - 1) &&
                   memcmp(data, legacy_boot_script, (size_t)n) == 0
               ? 1
               : -1;
}
static bool boot_enabled(void) {
    return boot_hook_kind() > 0 && access(BOOT_HOOK, X_OK) == 0;
}
static bool set_boot(bool enabled) {
    int kind = boot_hook_kind();
    if (kind < 0)
        return false;
    if (!enabled)
        return kind == 0 || unlink(BOOT_HOOK) == 0;
    if (access(BOOT_SCRIPT, X_OK) != 0)
        return false;
    if (kind == 2)
        return true;
    if (mkdir(BOOT_DIRECTORY, 0755) != 0 && errno != EEXIST)
        return false;
    /* Never overwrite another process's temporary file or a foreign hook. */
    if (symlink(BOOT_SCRIPT, BOOT_HOOK ".tmp") != 0)
        return false;
    bool ok = rename(BOOT_HOOK ".tmp", BOOT_HOOK) == 0;
    if (!ok)
        unlink(BOOT_HOOK ".tmp");
    return ok;
}
#endif
