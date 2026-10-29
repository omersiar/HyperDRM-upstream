#ifndef HYPERDRM_BASE64_H
#define HYPERDRM_BASE64_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Standard Base64 with padding. The histogram payload is 24576 bytes of binary
 * counts; as JSON numbers that is roughly three times larger and costs one
 * snprintf per bin on the TV's CPU. One pass over the bytes is much cheaper. */
static inline size_t base64_encoded_size(size_t bytes) {
    return (bytes + 2) / 3 * 4;
}
static inline size_t base64_encode(const void *data, size_t bytes, char *out) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *in = data;
    size_t written = 0;
    for (size_t i = 0; i < bytes; i += 3) {
        uint32_t value = (uint32_t)in[i] << 16;
        bool second = i + 1 < bytes, third = i + 2 < bytes;
        if (second)
            value |= (uint32_t)in[i + 1] << 8;
        if (third)
            value |= in[i + 2];
        out[written++] = alphabet[(value >> 18) & 63];
        out[written++] = alphabet[(value >> 12) & 63];
        out[written++] = second ? alphabet[(value >> 6) & 63] : '=';
        out[written++] = third ? alphabet[value & 63] : '=';
    }
    return written;
}
#endif
