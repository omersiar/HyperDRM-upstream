#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "base64.h"
int main(void) {
    char out[64];
    /* RFC 4648 test vectors, including both padding lengths. */
    assert(base64_encode("", 0, out) == 0);
    memset(out, 0, sizeof(out));
    assert(base64_encode("f", 1, out) == 4);
    assert(!strcmp(out, "Zg=="));
    memset(out, 0, sizeof(out));
    assert(base64_encode("fo", 2, out) == 4);
    assert(!strcmp(out, "Zm8="));
    memset(out, 0, sizeof(out));
    assert(base64_encode("foo", 3, out) == 4);
    assert(!strcmp(out, "Zm9v"));
    memset(out, 0, sizeof(out));
    assert(base64_encode("foob", 4, out) == 8);
    assert(!strcmp(out, "Zm9vYg=="));
    memset(out, 0, sizeof(out));
    assert(base64_encode("fooba", 5, out) == 8);
    assert(!strcmp(out, "Zm9vYmE="));
    memset(out, 0, sizeof(out));
    assert(base64_encode("foobar", 6, out) == 8);
    assert(!strcmp(out, "Zm9vYmFy"));
    /* The histogram payload is a whole number of three-byte groups, so the
     * service never emits padding and the decode has no special cases. */
    assert(base64_encoded_size(0) == 0);
    assert(base64_encoded_size(1) == 4);
    assert(base64_encoded_size(24576) == 32768);
    assert(base64_encode((const unsigned char[6]){0, 0, 0, 0, 0, 0}, 6, out) == 8);
    assert(!strcmp(out, "AAAAAAAA"));
    puts("PASS: base64 padding, RFC 4648 vectors, histogram payload size");
    return 0;
}
