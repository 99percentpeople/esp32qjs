#pragma once

#include "esp32_mquickjs_types.h"
#include "esp32_mquickjs_bitmap_image.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA

typedef struct {
    esp32_mquickjs_bitmap_view_t view;
    uint32_t camera_generation;
    uint32_t frame_generation;
    bool active;
} esp32_mquickjs_camera_bitmap_lease_t;

bool esp32_mquickjs_init_camera_runtime(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_camera_runtime(JSContext *ctx);

JSValue js_camera_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_camera_finalizer(JSContext *ctx, void *opaque);
JSValue js_camera_capture(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_camera_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_camera_controls(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_camera_set_control(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_camera_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_camera_frame_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_camera_frame_finalizer(JSContext *ctx, void *opaque);
JSValue js_camera_frame_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_camera_frame_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_camera_frame_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

bool esp32_mquickjs_camera_frame_acquire_bitmap_view(
    JSContext *ctx,
    JSValue value,
    const char *api_name,
    esp32_mquickjs_camera_bitmap_lease_t *out_lease);
void esp32_mquickjs_camera_frame_release_bitmap_view(
    esp32_mquickjs_camera_bitmap_lease_t *lease);

JSValue js_camera_capabilities(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_camera_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
