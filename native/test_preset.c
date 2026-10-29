#include <assert.h>
#include <stdio.h>
#include "preset_payload.h"
int main(void) {
    assert(preset_from_payload("{\"preset\":0}") == 0);
    assert(preset_from_payload("{\"preset\":7}") == 7);
    assert(preset_from_payload("{\"preset\":15}") == 15);
    assert(preset_from_payload("{\"preset\": 3 }") == 3);
    /* Out of range, absent and malformed payloads are rejected, not clamped. */
    assert(preset_from_payload("{\"preset\":16}") == -1);
    assert(preset_from_payload("{\"preset\":-1}") == -1);
    assert(preset_from_payload("{\"preset\":}") == -1);
    assert(preset_from_payload("{}") == -1);
    assert(preset_from_payload("") == -1);
    assert(preset_from_payload(NULL) == -1);
    puts("PASS: preset payload bounds");
    return 0;
}
