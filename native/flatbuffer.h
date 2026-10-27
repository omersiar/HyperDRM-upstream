#ifndef HYPERDRM_FLATBUFFER_H
#define HYPERDRM_FLATBUFFER_H
/* Minimal FlatBuffers writer for HyperHDR's hyperhdrnet.Request schema
 * (include/flatbuffers/parser/hyperhdr_request.fbs):
 *
 *   enum Command : byte { Color, Image, Clear, Register }     // Image = 2
 *   enum ImageType : byte { RawImage, NV12Image }             // RawImage = 1
 *   table Register { origin:string (required); priority:int; }
 *   table RawImage { data:[ubyte]; width:int = -1; height:int = -1; }
 *   table Image    { data:ImageType (required); duration:int = -1; }
 *   table Clear    { priority:int; }
 *   table Request  { command:Command (required); }
 *
 * Buffers are assembled back to front, so every stored offset is a distance
 * between two objects and survives the final front padding. No FlatBuffers
 * runtime, allocator or code generator is needed on the TV. The verifier only
 * requires that each field is aligned relative to the buffer start, which holds
 * because the builder pads before every element and the total size is a
 * multiple of four. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FB_MAX_FIELDS 4
typedef struct {
    uint8_t *buf;
    size_t capacity, used;
    bool overflow;
} FlatBuffer;

typedef struct {
    size_t end, slot[FB_MAX_FIELDS];
    unsigned fields;
} FlatBufferTable;

static inline void fb_put(FlatBuffer *b, const void *src, size_t n) {
    if (b->overflow || b->used + n > b->capacity) {
        b->overflow = true;
        return;
    }
    memcpy(b->buf + b->capacity - b->used - n, src, n);
    b->used += n;
}
static inline void fb_pad(FlatBuffer *b, size_t n) {
    if (b->overflow || b->used + n > b->capacity) {
        b->overflow = true;
        return;
    }
    memset(b->buf + b->capacity - b->used - n, 0, n);
    b->used += n;
}
/* Pad so that an element of `size` bytes lands on an `alignment` boundary. */
static inline void fb_align(FlatBuffer *b, size_t size, size_t alignment) {
    while ((b->used + size) % alignment)
        fb_pad(b, 1);
}
static inline void fb_u16(FlatBuffer *b, uint16_t v) {
    uint8_t raw[2] = {(uint8_t)(v & 0xff), (uint8_t)(v >> 8)};
    fb_put(b, raw, 2);
}
static inline void fb_u32(FlatBuffer *b, uint32_t v) {
    uint8_t raw[4] = {(uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff),
                      (uint8_t)((v >> 16) & 0xff), (uint8_t)((v >> 24) & 0xff)};
    fb_put(b, raw, 4);
}
/* Overwrite a scalar already written `used_after` bytes from the end. */
static inline void fb_patch_i32(FlatBuffer *b, size_t used_after, int32_t v) {
    if (b->overflow || used_after > b->used)
        return;
    uint8_t *at = b->buf + b->capacity - used_after;
    at[0] = (uint8_t)(v & 0xff);
    at[1] = (uint8_t)((v >> 8) & 0xff);
    at[2] = (uint8_t)((v >> 16) & 0xff);
    at[3] = (uint8_t)((v >> 24) & 0xff);
}
static inline void fb_table_start(FlatBuffer *b, FlatBufferTable *t) {
    t->end = b->used;
    t->fields = 0;
    memset(t->slot, 0, sizeof(t->slot));
}
static inline void fb_table_field(FlatBufferTable *t, unsigned id, size_t used_after) {
    if (id >= FB_MAX_FIELDS)
        return;
    t->slot[id] = used_after;
    if (t->fields < id + 1)
        t->fields = id + 1;
}
static inline void fb_field_offset(FlatBuffer *b, FlatBufferTable *t, unsigned id,
                                   size_t target_used) {
    fb_align(b, 4, 4);
    fb_u32(b, (uint32_t)(b->used + 4 - target_used));
    fb_table_field(t, id, b->used);
}
static inline void fb_field_u8(FlatBuffer *b, FlatBufferTable *t, unsigned id, uint8_t v) {
    fb_put(b, &v, 1);
    fb_table_field(t, id, b->used);
}
static inline void fb_field_i32(FlatBuffer *b, FlatBufferTable *t, unsigned id, int32_t v) {
    fb_align(b, 4, 4);
    fb_u32(b, (uint32_t)v);
    fb_table_field(t, id, b->used);
}
static inline size_t fb_table_end(FlatBuffer *b, FlatBufferTable *t) {
    fb_align(b, 4, 4);
    fb_pad(b, 4);
    size_t start = b->used;
    unsigned fields = t->fields;
    while (fields && !t->slot[fields - 1])
        fields--;
    for (int i = (int)fields - 1; i >= 0; i--) {
        fb_align(b, 2, 2);
        fb_u16(b, (uint16_t)(start - t->slot[i]));
    }
    fb_align(b, 2, 2);
    fb_u16(b, (uint16_t)(start - t->end));
    fb_align(b, 2, 2);
    fb_u16(b, (uint16_t)(4 + 2 * fields));
    fb_patch_i32(b, start, (int32_t)(b->used - start));
    return start;
}
static inline size_t fb_finish(FlatBuffer *b, size_t root) {
    fb_align(b, 4, 4);
    fb_u32(b, (uint32_t)(b->used + 4 - root));
    return b->used;
}
static inline size_t fb_create_blob(FlatBuffer *b, const void *data, size_t bytes) {
    fb_align(b, bytes, 4);
    fb_put(b, data, bytes);
    fb_align(b, 4, 4);
    fb_u32(b, (uint32_t)bytes);
    return b->used;
}
static inline size_t fb_create_string(FlatBuffer *b, const char *text) {
    size_t length = strlen(text);
    fb_align(b, length + 1, 4);
    fb_pad(b, 1);
    fb_put(b, text, length);
    fb_align(b, 4, 4);
    fb_u32(b, (uint32_t)length);
    return b->used;
}
static inline size_t fb_encode_command(FlatBuffer *b, uint8_t command, unsigned id,
                                       FlatBufferTable *child) {
    FlatBufferTable request;
    fb_table_start(b, &request);
    fb_field_u8(b, &request, 0, command);
    fb_field_offset(b, &request, 1, fb_table_end(b, child));
    (void)id;
    return fb_finish(b, fb_table_end(b, &request));
}
/* Register declares the client name and the priority the server reports back. */
static inline size_t fb_encode_register(FlatBuffer *b, const char *origin, int priority) {
    size_t text = fb_create_string(b, origin);
    FlatBufferTable register_table;
    fb_table_start(b, &register_table);
    fb_field_offset(b, &register_table, 0, text);
    fb_field_i32(b, &register_table, 1, priority);
    return fb_encode_command(b, 4, 0, &register_table);
}
static inline size_t fb_encode_clear(FlatBuffer *b, int priority) {
    FlatBufferTable clear_table;
    fb_table_start(b, &clear_table);
    fb_field_i32(b, &clear_table, 0, priority);
    return fb_encode_command(b, 3, 0, &clear_table);
}
/* One raw RGB plane. The server samples it with each instance's own geometry. */
static inline size_t fb_encode_image(FlatBuffer *b, const void *rgb, int width, int height,
                                     int duration) {
    size_t data = fb_create_blob(b, rgb, (size_t)width * (size_t)height * 3);
    FlatBufferTable raw;
    fb_table_start(b, &raw);
    fb_field_offset(b, &raw, 0, data);
    fb_field_i32(b, &raw, 1, width);
    fb_field_i32(b, &raw, 2, height);
    FlatBufferTable image;
    fb_table_start(b, &image);
    fb_field_u8(b, &image, 0, 1);
    fb_field_offset(b, &image, 1, fb_table_end(b, &raw));
    fb_field_i32(b, &image, 2, duration);
    return fb_encode_command(b, 2, 0, &image);
}
/* Read a 32-bit little-endian scalar from a completed buffer. */
static inline bool fb_read_u32(const uint8_t *buf, size_t size, size_t at, uint32_t *out) {
    if (at > size || size - at < 4)
        return false;
    *out = (uint32_t)buf[at] | ((uint32_t)buf[at + 1] << 8) | ((uint32_t)buf[at + 2] << 16) |
           ((uint32_t)buf[at + 3] << 24);
    return true;
}
/* Field position of `id` inside the root table, or 0 when absent. */
static inline size_t fb_root_field(const uint8_t *buf, size_t size, unsigned id) {
    uint32_t root;
    if (!fb_read_u32(buf, size, 0, &root) || root >= size)
        return 0;
    uint32_t soffset;
    if (!fb_read_u32(buf, size, root, &soffset))
        return 0;
    size_t vtable = (size_t)((int64_t)root - (int32_t)soffset);
    uint32_t vtable_size;
    if (!fb_read_u32(buf, size, vtable & ~(size_t)1, &vtable_size))
        return 0;
    if ((vtable_size & 0xffff) < 4 + 2 * (id + 1))
        return 0;
    size_t entry = vtable + 4 + 2 * id;
    if (entry + 2 > size)
        return 0;
    unsigned offset = (unsigned)buf[entry] | ((unsigned)buf[entry + 1] << 8);
    return offset ? root + offset : 0;
}
/* Copy the server's error text (empty string means the request was accepted). */
static inline bool fb_reply_error(const uint8_t *buf, size_t size, char *out, size_t capacity) {
    if (!out || !capacity)
        return false;
    out[0] = 0;
    size_t field = fb_root_field(buf, size, 0);
    if (!field)
        return true;
    uint32_t offset;
    if (!fb_read_u32(buf, size, field, &offset))
        return false;
    size_t text = field + offset;
    uint32_t length;
    if (!fb_read_u32(buf, size, text, &length) || text + 4 + length > size)
        return false;
    size_t copy = length < capacity - 1 ? length : capacity - 1;
    memcpy(out, buf + text + 4, copy);
    out[copy] = 0;
    return true;
}
#endif
