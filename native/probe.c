#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdarg.h>
#include "histogram.h"
/* Process-local diagnostic sink. It changes logging only, not capture policy. */
void printLog(const char *context, int level, const char *message_id, const char *file,
              const char *function, int line, const char *format, ...) {
    (void)context;
    (void)level;
    (void)message_id;
    (void)file;
    fprintf(stderr, "LG %s:%d: ", function ? function : "?", line);
    if (format) {
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }
    fputc('\n', stderr);
}
static bool policy(void) {
    fprintf(stderr, "Capture revoked by LG policy\n");
    return true;
}
int main(void) {
    void *lib = dlopen("libvtcapture.so.1", RTLD_NOW);
    if (!lib) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    void *(*init)(bool (*)(void)) = dlsym(lib, "vtCapture_initHistogram");
    int (*preset)(void *, int) = dlsym(lib, "vtCapture_setPresetHistogram");
    int (*get)(void *, Histogram *) = dlsym(lib, "vtCapture_getHistogram");
    int (*finish)(void *) = dlsym(lib, "vtCapture_finalizeHistogram");
    if (!init || !preset || !get || !finish)
        return 2;
    void *handle = init(policy);
    fprintf(stderr, "handle=%p\n", handle);
    if (!handle)
        return 3;
    int rc = preset(handle, 0);
    fprintf(stderr, "preset=%d\n", rc);
    Histogram hist[16];
    for (int frame = 0; rc == 0 && frame < 3; frame++) {
        usleep(100000);
        rc = get(handle, hist);
        printf("frame=%d rc=%d\n", frame, rc);
        if (rc == 0)
            for (int i = 0; i < 16; i++) {
                printf("region=%u", hist[i].id);
                for (int c = 0; c < 3; c++) {
                    uint64_t sum = 0;
                    for (int b = 0; b < 256; b++)
                        sum += hist[i].bins[c][b];
                    printf(" ch%d_count=%llu mean=%u", c, (unsigned long long)sum,
                           histogram_mean(hist[i].bins[c]));
                }
                puts("");
            }
    }
    fprintf(stderr, "finalize=%d\n", finish(handle));
    return rc ? 4 : 0;
}
