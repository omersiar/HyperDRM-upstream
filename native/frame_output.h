#ifndef HYPERDRM_FRAME_OUTPUT_H
#define HYPERDRM_FRAME_OUTPUT_H
/* Transport dispatch. Hyperion and single-instance HyperHDR use the JSON API,
 * where a connection is bound to one LED hardware instance. HyperHDR with the
 * FlatBuffer transport decodes one image in the receiver and fans it out to
 * every running instance, so no per-instance connection is needed. */
typedef struct {
    char *line;
    unsigned char *plane;
    unsigned width, height;
} OutputFrame;

static OutputFrame frame_build(const unsigned char colors[16][3], const LedBlend *mapping,
                               unsigned count, unsigned duration) {
    OutputFrame frame = {NULL, NULL, 0, 0};
    if (target_flatbuffer(&target)) {
        spatial_dimensions(&frame.width, &frame.height);
        frame.plane = malloc((size_t)frame.width * frame.height * 3);
        if (frame.plane)
            spatial_plane(colors, frame.plane);
        return frame;
    }
    frame.line = region_command(colors, mapping, count, duration);
    return frame;
}
static void frame_release(OutputFrame *frame) {
    free(frame->line);
    free(frame->plane);
    frame->line = NULL;
    frame->plane = NULL;
    frame->width = frame->height = 0;
}
static bool frame_send(const OutputFrame *frame, unsigned duration, uint64_t now) {
    if (target_flatbuffer(&target))
        return frame->plane && fb_out_frame(&receiver_runtime.flatbuffer, frame->plane,
                                            (int)frame->width, (int)frame->height, (int)duration,
                                            now);
    return frame->line && output_frame(&receiver_runtime.output, frame->line);
}
static bool receiver_begin(uint64_t now) {
    if (target_flatbuffer(&target))
        return fb_out_begin(&receiver_runtime.flatbuffer, &target, PRIORITY,
                            (size_t)SPATIAL_PIXELS * 3 + 512, now);
    return output_begin(&receiver_runtime.output, &target, now);
}
static void receiver_tick(PlaybackJson *json, uint64_t now) {
    if (target_flatbuffer(&target))
        fb_out_tick(&receiver_runtime.flatbuffer, now);
    else
        output_tick(&receiver_runtime.output, json, now);
}
static void receiver_stop(uint64_t now, bool clear) {
    if (target_flatbuffer(&target))
        fb_out_stop(&receiver_runtime.flatbuffer, now, clear);
    else
        output_stop(&receiver_runtime.output, now, clear);
}
static void receiver_close(void) {
    if (target_flatbuffer(&target))
        fb_out_close(&receiver_runtime.flatbuffer);
    else
        output_close(&receiver_runtime.output);
    receiver_runtime.flatbuffer.stage = FB_IDLE;
}
static bool receiver_ready(void) {
    return target_flatbuffer(&target) ? receiver_runtime.flatbuffer.stage == FB_READY
                                      : receiver_runtime.output.stage == OUTPUT_READY;
}
static bool receiver_failed(void) {
    return target_flatbuffer(&target) ? receiver_runtime.flatbuffer.stage == FB_FAILED
                                      : receiver_runtime.output.stage == OUTPUT_FAILED;
}
static const char *receiver_error(void) {
    return target_flatbuffer(&target) ? receiver_runtime.flatbuffer.error
                                      : receiver_runtime.output.error;
}
static unsigned long receiver_frames(void) {
    return target_flatbuffer(&target) ? receiver_runtime.flatbuffer.frames
                                      : receiver_runtime.output.acknowledgements;
}
static bool receiver_closing(void) {
    return target_flatbuffer(&target) ? receiver_runtime.flatbuffer.closing
                                      : receiver_runtime.output.closing;
}
#endif
