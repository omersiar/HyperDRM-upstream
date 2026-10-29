#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdarg.h>
#include "histogram.h"
/* Preset enumeration probe.
 *
 * `probe [preset] [frames]` selects one histogram preset and prints the sixteen
 * records as JSON lines. LG exposes `vtCapture_setPresetHistogram` with an
 * integer selector and never documents what the presets are, so the only way to
 * learn whether they describe different sampling windows is to read them back.
 *
 * The per-region bin total is the area evidence: every pixel the window covers
 * lands in some bin, so the total is proportional to the window area whatever the
 * picture shows. Identical totals across two presets mean identical windows.
 * Run one process per preset, so a rejected selector cannot leave the handle in a
 * state that affects the next one.
 *
 * Measured 2026-10-29: LG refuses this standalone binary. `vtCapture_initHistogram`
 * is gated on both /var/luna/preferences/vt_devmode and on the calling process, so
 * the probe reports "no permissions" even when run from the service directory.
 * Kept as the ABI reference; use the `presetScan` luna method for real work. */
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
int main(int argc, char **argv) {
    int preset_id = argc > 1 ? atoi(argv[1]) : 0;
    int frames = argc > 2 ? atoi(argv[2]) : 4;
    if (frames < 1)
        frames = 1;
    if (frames > 200)
        frames = 200;
    void *lib = dlopen("libvtcapture.so.1", RTLD_NOW);
    if (!lib) {
        printf("{\"preset\":%d,\"dlopen\":false}\n", preset_id);
        return 1;
    }
    void *(*init)(bool (*)(void)) = dlsym(lib, "vtCapture_initHistogram");
    int (*preset)(void *, int) = dlsym(lib, "vtCapture_setPresetHistogram");
    int (*get)(void *, Histogram *) = dlsym(lib, "vtCapture_getHistogram");
    int (*finish)(void *) = dlsym(lib, "vtCapture_finalizeHistogram");
    if (!init || !preset || !get || !finish) {
        printf("{\"preset\":%d,\"symbols\":false}\n", preset_id);
        return 2;
    }
    void *handle = init(policy);
    if (!handle) {
        printf("{\"preset\":%d,\"init\":false}\n", preset_id);
        return 3;
    }
    int rc = preset(handle, preset_id);
    printf("{\"preset\":%d,\"setPreset\":%d}\n", preset_id, rc);
    fflush(stdout);
    if (rc == 0) {
        /* Let the window change settle before the first read. */
        usleep(400000);
        for (int frame = 0; frame < frames; frame++) {
            Histogram hist[16];
            memset(hist, 0, sizeof(hist));
            int get_rc = get(handle, hist);
            if (get_rc) {
                printf("{\"preset\":%d,\"frame\":%d,\"get\":%d}\n", preset_id, frame, get_rc);
                fflush(stdout);
                usleep(150000);
                continue;
            }
            printf("{\"preset\":%d,\"frame\":%d,\"regions\":[", preset_id, frame);
            for (int i = 0; i < 16; i++) {
                unsigned long long count[3];
                unsigned mean[3];
                for (int c = 0; c < 3; c++) {
                    unsigned long long sum = 0;
                    for (int b = 0; b < 256; b++)
                        sum += hist[i].bins[c][b];
                    count[c] = sum;
                    mean[c] = histogram_mean(hist[i].bins[c]);
                }
                printf("%s{\"id\":%u,\"count\":[%llu,%llu,%llu],\"mean\":[%u,%u,%u]}",
                       i ? "," : "", hist[i].id, count[0], count[1], count[2], mean[0], mean[1],
                       mean[2]);
            }
            printf("]}\n");
            fflush(stdout);
            usleep(150000);
        }
    }
    int final_rc = finish(handle);
    printf("{\"preset\":%d,\"finalize\":%d}\n", preset_id, final_rc);
    return 0;
}
