#ifndef HYPERDRM_PRESET_PAYLOAD_H
#define HYPERDRM_PRESET_PAYLOAD_H
#include <stdlib.h>
#include <string.h>
/* Read `{"preset":N}` without pulling in the JSON library: the diagnostic takes
 * one bounded integer and a hand-written scan cannot outlive its bounds. */
#define PRESET_LIMIT 16
static inline int preset_from_payload(const char *text) {
    if (!text)
        return -1;
    const char *key = strstr(text, "\"preset\"");
    if (!key)
        return -1;
    key = strchr(key, ':');
    if (!key)
        return -1;
    char *end = NULL;
    long value = strtol(key + 1, &end, 10);
    if (end == key + 1 || value < 0 || value >= PRESET_LIMIT)
        return -1;
    return (int)value;
}
#endif
