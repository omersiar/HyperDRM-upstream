#ifndef HYPERDRM_SERVICE_HISTOGRAMS_H
#define HYPERDRM_SERVICE_HISTOGRAMS_H
/* Raw 256-bin counts of the last captured frame.
 *
 * The status reply carries only the sixteen region means, which is all the
 * renderer needs, but that leaves most of the LG ABI unreachable: experiments
 * that ask where a region samples, or how its pixels are distributed, need the
 * counts. The payload is 16 regions x 3 channels x 256 bins of little-endian
 * uint16 = 24576 bytes, base64 encoded to 32768 characters.
 *
 * Region order is the record id, channel order is R,G,B. Read-only diagnostic
 * surface: it never changes capture policy. It is a separate method because the
 * app polls status every 250-500 ms and must not carry this payload. */
#include "base64.h"
#define HISTOGRAM_BINS_BYTES (16u * 3u * 256u * 2u)
#define HISTOGRAM_BINS_ENCODED ((HISTOGRAM_BINS_BYTES + 2u) / 3u * 4u)
_Static_assert(HISTOGRAM_BINS_ENCODED == 32768u, "histogram payload encoding size");
static bool histograms(void *h, void *m, void *ctx) {
    (void)ctx;
    (void)m;
    static char json[HISTOGRAM_BINS_ENCODED + 160u];
    if (!capture_ctx.hist_ready)
        return respond(h, m,
                       "{\"returnValue\":false,\"errorText\":\"No histogram frame captured yet\"}");
    int used = snprintf(json, sizeof(json),
                        "{\"returnValue\":true,\"sampleSequence\":%u,\"sampleAgeMs\":%lld,"
                        "\"width\":16,\"height\":768,\"bytes\":%u,\"bins\":\"",
                        capture_ctx.sample_sequence,
                        capture_ctx.sample_at ? (long long)(milliseconds() - capture_ctx.sample_at)
                                              : -1,
                        (unsigned)HISTOGRAM_BINS_BYTES);
    if (used < 0 || (size_t)used + HISTOGRAM_BINS_ENCODED + 4u > sizeof(json))
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Payload overflow\"}");
    /* sizeof(Histogram) is 0x604 with no padding, so the bins of the sixteen
     * cached records form one contiguous HISTOGRAM_BINS_BYTES run. */
    used += (int)base64_encode(capture_ctx.last_hist[0].bins, HISTOGRAM_BINS_BYTES, json + used);
    json[used++] = '"';
    json[used++] = '}';
    json[used] = 0;
    return respond(h, m, json);
}
#endif
