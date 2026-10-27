#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include "target.h"
#include "layout.h"
int main(void) {
    (void)target_open;
    (void)target_save;
    (void)target_load;
    (void)layout_from_json;
    PlaybackJson json = {0};
    assert(!playback_parse_exact(&json, "{}"));
    assert(playback_json_init(&json));
    const char *invalid[] = {"{",
                             "{\"success\":true",
                             "{}garbage",
                             "{} {}",
                             "[1,",
                             "{\"success\":tru}",
                             "{\"a\":\"unterminated}"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
        assert(!playback_parse_exact(&json, invalid[i]));
    void *root = playback_parse_exact(&json, "  {\"success\":true} \t\r");
    assert(root && playback_true(&json, root, "success"));
    json.put(root);
    const char *wrong[] = {
        "[]", "true", "1", "\"success\"", "{\"success\":1}", "{\"success\":\"true\"}"};
    for (unsigned i = 0; i < sizeof(wrong) / sizeof(*wrong); i++) {
        root = playback_parse_exact(&json, wrong[i]);
        assert(!playback_true(&json, root, "success"));
        if (root)
            json.put(root);
    }
    TargetConfig original = target, next = original;
    assert(!target_parse(
        &json, "{\"host\":\"127.0.0.1\",\"receiver\":\"Hyperion\",\"port\":19444}garbage", &next));
    assert(!memcmp(&next, &original, sizeof(next)));
    Calibration profile = calibration;
    assert(!calibration_parse(&json, "{\"profile\":\"average\"}{}", &profile));
    assert(!memcmp(&profile, &calibration, sizeof(profile)));
    /* Deterministic malformed-input mutations; every successful parse is freed.
     * This is regression coverage, not a claim of exhaustive fuzzing. */
    const char seed[] = "{\"success\":true,\"tan\":1,\"data\":{\"leds\":[0,255,128]}}";
    uint32_t random = 0x12345678;
    for (unsigned i = 0; i < 5000; i++) {
        char text[sizeof(seed) + 4];
        memcpy(text, seed, sizeof(seed));
        random = random * 1664525u + 1013904223u;
        unsigned position = random % (sizeof(seed) - 1);
        text[position] = (char)(1 + (random >> 16) % 126);
        if (i % 3 == 0)
            text[position] = 0;
        root = playback_parse_exact(&json, text);
        if (root)
            json.put(root);
    }
    dlclose(json.lib);
    puts("PASS: exact JSON framing, truncated/trailing data, unexpected types, unchanged settings "
         "on rejection and 5000 deterministic parser mutations");
}
