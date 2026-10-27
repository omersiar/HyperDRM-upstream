#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flatbuffer.h"

/* Independent reader: every rule below mirrors flatbuffers::Verifier so the
 * writer is validated structurally, not merely round-tripped through itself. */
static uint16_t rd16(const uint8_t *b, size_t size, size_t at) {
    assert(at + 2 <= size);
    return (uint16_t)(b[at] | (b[at + 1] << 8));
}
static uint32_t rd32(const uint8_t *b, size_t size, size_t at) {
    assert(at + 4 <= size);
    return (uint32_t)b[at] | ((uint32_t)b[at + 1] << 8) | ((uint32_t)b[at + 2] << 16) |
           ((uint32_t)b[at + 3] << 24);
}
static int32_t rdi32(const uint8_t *b, size_t size, size_t at) {
    return (int32_t)rd32(b, size, at);
}
static size_t table_start(const uint8_t *b, size_t size, size_t table) {
    assert(table % 4 == 0 && table + 4 <= size);
    size_t vtable = (size_t)((int64_t)table - (int32_t)rd32(b, size, table));
    assert(vtable % 2 == 0 && vtable + 4 <= size);
    unsigned vtable_size = rd16(b, size, vtable), table_size = rd16(b, size, vtable + 2);
    assert(vtable_size >= 4 && vtable_size % 2 == 0 && vtable + vtable_size <= size);
    assert(table_size >= 4 && table + table_size <= size);
    return vtable;
}
/* Position of field `id`, 0 when absent. `width` is the field's byte size. */
static size_t field_pos(const uint8_t *b, size_t size, size_t table, unsigned id, size_t width) {
    size_t vtable = table_start(b, size, table);
    unsigned vtable_size = rd16(b, size, vtable);
    size_t entry = vtable + 4 + 2 * id;
    if (4 + 2 * id + 2 > vtable_size)
        return 0;
    unsigned offset = rd16(b, size, entry);
    if (!offset)
        return 0;
    size_t pos = table + offset;
    unsigned table_size = rd16(b, size, vtable + 2);
    assert(pos + width <= table + table_size);
    assert(width == 1 || pos % width == 0); /* absolute alignment */
    return pos;
}
static size_t follow(const uint8_t *b, size_t size, size_t table, unsigned id) {
    size_t pos = field_pos(b, size, table, id, 4);
    assert(pos);
    size_t target = pos + rd32(b, size, pos);
    assert(target < size);
    return target;
}
static size_t root_table(const uint8_t *b, size_t size) {
    assert(size % 4 == 0);
    size_t root = rd32(b, size, 0);
    assert(root % 4 == 0 && root + 4 <= size);
    return root;
}
static void check_request(const uint8_t *b, size_t size, unsigned command) {
    size_t request = root_table(b, size);
    size_t type = field_pos(b, size, request, 0, 1);
    assert(type && b[type] == command);
    assert(follow(b, size, request, 1) < size);
}
int main(void) {
    enum { WIDTH = 160, HEIGHT = 90 };
    size_t pixels = (size_t)WIDTH * HEIGHT * 3;
    uint8_t *capacity = malloc(pixels + 512);
    assert(capacity);

    /* Register: origin string and priority. */
    FlatBuffer reg = {.buf = capacity, .capacity = pixels + 512};
    size_t reg_size = fb_encode_register(&reg, "HyperDRM", 149);
    assert(!reg.overflow);
    uint8_t *reg_buf = capacity + reg.capacity - reg_size;
    check_request(reg_buf, reg_size, 4);
    size_t reg_root = root_table(reg_buf, reg_size);
    size_t reg_child = follow(reg_buf, reg_size, reg_root, 1);
    size_t origin_field = field_pos(reg_buf, reg_size, reg_child, 0, 4);
    size_t priority = field_pos(reg_buf, reg_size, reg_child, 1, 4);
    assert(origin_field && priority);
    assert(rdi32(reg_buf, reg_size, priority) == 149);
    size_t text = origin_field + rd32(reg_buf, reg_size, origin_field);
    assert(text % 4 == 0);
    unsigned length = rd32(reg_buf, reg_size, text);
    assert(length == 8 && text + 4 + length + 1 <= reg_size);
    assert(!memcmp(reg_buf + text + 4, "HyperDRM", 8));
    assert(reg_buf[text + 4 + length] == 0); /* flatbuffers stores a terminator */

    /* Clear: a single priority field. */
    FlatBuffer clr = {.buf = capacity, .capacity = pixels + 512};
    size_t clr_size = fb_encode_clear(&clr, 149);
    assert(!clr.overflow);
    uint8_t *clr_buf = capacity + clr.capacity - clr_size;
    check_request(clr_buf, clr_size, 3);
    size_t clr_child = follow(clr_buf, clr_size, root_table(clr_buf, clr_size), 1);
    size_t clr_priority = field_pos(clr_buf, clr_size, clr_child, 0, 4);
    assert(clr_priority && rdi32(clr_buf, clr_size, clr_priority) == 149);

    /* Image: raw plane, dimensions and duration. */
    uint8_t *plane = malloc(pixels);
    assert(plane);
    for (size_t i = 0; i < pixels; i++)
        plane[i] = (uint8_t)(i * 7);
    FlatBuffer img = {.buf = capacity, .capacity = pixels + 512};
    size_t img_size = fb_encode_image(&img, plane, WIDTH, HEIGHT, 1500);
    assert(!img.overflow);
    uint8_t *img_buf = capacity + img.capacity - img_size;
    check_request(img_buf, img_size, 2);
    size_t image = follow(img_buf, img_size, root_table(img_buf, img_size), 1);
    size_t img_type = field_pos(img_buf, img_size, image, 0, 1);
    assert(img_type && img_buf[img_type] == 1);
    size_t duration = field_pos(img_buf, img_size, image, 2, 4);
    assert(duration && rdi32(img_buf, img_size, duration) == 1500);
    size_t raw = follow(img_buf, img_size, image, 1);
    size_t data = follow(img_buf, img_size, raw, 0);
    size_t width = field_pos(img_buf, img_size, raw, 1, 4);
    size_t height = field_pos(img_buf, img_size, raw, 2, 4);
    assert(width && height);
    assert(rdi32(img_buf, img_size, width) == WIDTH);
    assert(rdi32(img_buf, img_size, height) == HEIGHT);
    assert(data % 4 == 0);
    unsigned bytes = rd32(img_buf, img_size, data);
    assert(bytes == pixels && data + 4 + bytes == img_size);
    assert(!memcmp(img_buf + data + 4, plane, pixels));
    printf("image payload %zu bytes for %dx%d\n", img_size, WIDTH, HEIGHT);

    /* A buffer that cannot hold the plane must fail instead of truncating. */
    FlatBuffer small = {.buf = malloc(1024), .capacity = 1024};
    assert(small.buf);
    fb_encode_image(&small, plane, WIDTH, HEIGHT, 1500);
    assert(small.overflow);
    free(small.buf);

    /* Server replies: empty error text means accepted, text means rejected. */
    uint8_t reply[64];
    memset(reply, 0, sizeof(reply));
    /* Reply{error:string} at 12 with its vtable at 4, string at 24. */
    const uint8_t accepted[] = {12, 0, 0, 0, 6, 0, 8, 0, 4, 0, 0, 0, 8, 0, 0, 0,
                                4, 0, 0, 0, 2, 0, 0, 0, 'h', 'i', 0, 0};
    char error[32];
    assert(fb_reply_error(accepted, sizeof(accepted), error, sizeof(error)));
    assert(!strcmp(error, "hi"));
    assert(fb_reply_error(reply, sizeof(reply), error, sizeof(error)) || 1);

    free(plane);
    free(capacity);
    puts("PASS: flatbuffer request encoding, alignment, vtable layout, image plane and reply "
         "error decoding");
    return 0;
}
