#ifndef HYPERDRM_PLAYBACK_H
#define HYPERDRM_PLAYBACK_H
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <dlfcn.h>
typedef struct {
    void *lib;
    void *(*parse)(const char *);
    int (*get)(const void *, const char *, void **);
    int (*type)(const void *);
    size_t (*length)(const void *);
    void *(*at)(const void *, size_t);
    const char *(*string)(const void *);
    int (*boolean)(const void *);
    int (*put)(void *);
    void *(*tokener_new)(void);
    void *(*tokener_parse)(void *, const char *, int);
    int (*tokener_error)(void *);
    size_t (*tokener_end)(void *);
    void (*tokener_free)(void *);
} PlaybackJson;
static inline bool playback_json_init(PlaybackJson *j) {
    j->lib = dlopen("libjson-c.so.5", RTLD_NOW);
    if (!j->lib)
        return false;
    j->parse = dlsym(j->lib, "json_tokener_parse");
    j->get = dlsym(j->lib, "json_object_object_get_ex");
    j->type = dlsym(j->lib, "json_object_get_type");
    j->length = dlsym(j->lib, "json_object_array_length");
    j->at = dlsym(j->lib, "json_object_array_get_idx");
    j->string = dlsym(j->lib, "json_object_get_string");
    j->boolean = dlsym(j->lib, "json_object_get_boolean");
    j->put = dlsym(j->lib, "json_object_put");
    j->tokener_new = dlsym(j->lib, "json_tokener_new");
    j->tokener_parse = dlsym(j->lib, "json_tokener_parse_ex");
    j->tokener_error = dlsym(j->lib, "json_tokener_get_error");
    j->tokener_end = dlsym(j->lib, "json_tokener_get_parse_end");
    j->tokener_free = dlsym(j->lib, "json_tokener_free");
    if (!j->parse || !j->get || !j->type || !j->length || !j->at || !j->string || !j->boolean ||
        !j->put || !j->tokener_new || !j->tokener_parse || !j->tokener_error || !j->tokener_end ||
        !j->tokener_free) {
        dlclose(j->lib);
        j->lib = NULL;
        return false;
    }
    return true;
}
/* Require exactly one complete JSON value. json_tokener_parse alone can accept
 * an otherwise valid prefix followed by unrelated or truncated content. */
static inline void *playback_parse_exact(PlaybackJson *j, const char *text) {
    if (!j || !text || !j->tokener_new || !j->tokener_parse || !j->tokener_error ||
        !j->tokener_end || !j->tokener_free || !j->put)
        return NULL;
    size_t length = strlen(text);
    if (length > 2 * 1024 * 1024)
        return NULL;
    void *tokener = j->tokener_new();
    if (!tokener)
        return NULL;
    void *root = j->tokener_parse(tokener, text, (int)length + 1);
    size_t end = j->tokener_end(tokener);
    bool valid = j->tokener_error(tokener) == 0;
    while (end < length &&
           (text[end] == ' ' || text[end] == '\t' || text[end] == '\r' || text[end] == '\n'))
        end++;
    valid = valid && end == length;
    j->tokener_free(tokener);
    if (!valid && root) {
        j->put(root);
        root = NULL;
    }
    return root;
}
static inline void *playback_field(PlaybackJson *j, void *o, const char *key) {
    void *v = NULL;
    return o && j->type(o) == 4 && j->get(o, key, &v) ? v : NULL;
}
static inline bool playback_true(PlaybackJson *j, void *o, const char *key) {
    void *v = playback_field(j, o, key);
    return v && j->type(v) == 1 && j->boolean(v);
}
static inline bool playback_is(PlaybackJson *j, void *o, const char *key, const char *value) {
    void *v = playback_field(j, o, key);
    return v && j->type(v) == 6 && strcmp(j->string(v), value) == 0;
}
typedef enum { PLAYBACK_UNKNOWN, PLAYBACK_CLEAR, PLAYBACK_PROTECTED } PlaybackProtection;
/* G4 resource-state classification, not black-frame or app-name guessing.
 * Any active Secure Video Path wins. Only a foreground media decoder without
 * a secure resource is eligible; HDMI, idle and malformed replies are unknown. */
static inline PlaybackProtection playback_classify(PlaybackJson *j, const char *text) {
    void *root = j->parse(text);
    if (!root)
        return PLAYBACK_UNKNOWN;
    bool clear = false, invalid = j->type(root) != 5, secure = false;
    size_t n = invalid ? 0 : j->length(root);
    for (size_t i = 0; i < n; i++) {
        void *p = j->at(root, i);
        if (!p || j->type(p) != 4) {
            invalid = true;
            continue;
        }
        void *resources = playback_field(j, p, "resource");
        if (!resources)
            resources = playback_field(j, p, "resources");
        if (!resources || j->type(resources) != 5) {
            invalid = true;
            continue;
        }
        bool decoder = false;
        for (size_t k = 0; k < j->length(resources); k++) {
            void *resource = j->at(resources, k);
            void *name = playback_field(j, resource, "resource");
            if (!name || j->type(name) != 6) {
                invalid = true;
                continue;
            }
            const char *s = j->string(name);
            if (strcmp(s, "SVP_CPB") == 0)
                secure = true;
            if (strcmp(s, "VDEC") == 0)
                decoder = true;
        }
        if (decoder && playback_true(j, p, "is_foreground") && playback_is(j, p, "type", "media"))
            clear = true;
    }
    j->put(root);
    return secure ? PLAYBACK_PROTECTED : clear && !invalid ? PLAYBACK_CLEAR : PLAYBACK_UNKNOWN;
}
#endif
