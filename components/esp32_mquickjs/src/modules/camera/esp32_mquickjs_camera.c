#include "esp32_mquickjs_camera.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA

#include "esp32_mquickjs_camera_driver_resources.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_peripheral_lease.h"
#include "utils/esp32_mquickjs_byte_source.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_camera.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor.h"
#include "soc/soc_caps.h"

#define CAMERA_REQUIRED_PIN_MISSING (-2)
#define CAMERA_DEFAULT_TIMEOUT_MS 5000U
#define CAMERA_DEFAULT_CHUNK_BYTES 4096U
#define CAMERA_MAX_CHUNK_BYTES (32U * 1024U)
#define CAMERA_MAX_COPY_BYTES (32U * 1024U)
#define CAMERA_SCCB_I2C_PORT 1
#define CAMERA_FRAME_OWNER_KEY "__esp32qjsCameraFrameOwner"

/*
 * esp32-camera exposes only a fixed four-second esp_camera_fb_get() wait.
 * The component version pinned by this project exports cam_take(), which lets
 * the adapter keep the public Camera.capture(timeoutMs) contract and poll
 * cancellation without modifying the dependency.
 */
extern camera_fb_t *cam_take(TickType_t timeout);

static bool camera_psram_is_initialized(void)
{
#if CONFIG_SPIRAM
    return esp_psram_is_initialized();
#else
    return false;
#endif
}

static size_t camera_psram_size(void)
{
#if CONFIG_SPIRAM
    return esp_psram_get_size();
#else
    return 0;
#endif
}

static int camera_driver_deinit(void *opaque)
{
    (void)opaque;
    return esp_camera_deinit();
}

static const esp32_mquickjs_camera_driver_resource_ops_t
    s_camera_driver_resource_ops = {
        .deinit = camera_driver_deinit,
        .opaque = NULL,
    };

typedef struct {
    uint32_t generation;
} esp32_mquickjs_camera_ref_t;

typedef struct {
    uint32_t camera_generation;
    uint32_t frame_generation;
} esp32_mquickjs_camera_frame_ref_t;

typedef struct esp32_mquickjs_camera_source esp32_mquickjs_camera_source_t;

typedef struct {
    bool allocated;
    bool initialized;
    bool busy;
    bool release_pending;
    uint32_t generation;
    uint32_t frame_generation;
    uint32_t sequence;
    uint32_t default_timeout_ms;
    pixformat_t pixel_format;
    framesize_t frame_size;
    int jpeg_quality;
    size_t frame_buffers;
    camera_grab_mode_t grab_mode;
    camera_fb_location_t buffer_location;
    bool psram_dma;
    sensor_t *sensor;
    camera_fb_t *leased_fb;
    bool frame_revoked;
    bool source_active;
    esp32_mquickjs_camera_source_t *source;
    uint16_t bitmap_read_leases;
    esp32_mquickjs_future_driver_state_t *capture_state;
    esp32_mquickjs_future_driver_state_t *close_state;
    esp32_mquickjs_peripheral_lease_t camera_lease;
    esp32_mquickjs_peripheral_lease_t i2c_lease;
    esp32_mquickjs_peripheral_lease_t ledc_timer_lease;
    esp32_mquickjs_peripheral_lease_t ledc_channel_lease;
} esp32_mquickjs_camera_slot_t;

struct esp32_mquickjs_camera_source {
    uint32_t camera_generation;
    uint32_t frame_generation;
    size_t chunk_bytes;
    size_t offset;
    bool opened;
    bool iterator_active;
    bool consumed;
    bool destroy_requested;
};

struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;
    JSGCRef owner_ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint32_t camera_generation;
    uint32_t timeout_ms;
    uint64_t started_us;
    framesize_t frame_size;
    pixformat_t pixel_format;
    camera_fb_t *frame;
    bool owner_retained;
    bool started;
    bool cleanup_started;
    bool cleanup_submitted;
    bool cleanup_finalized;
    bool close_initialized;
    _Atomic bool completed;
    _Atomic bool cancelled;
    esp_err_t close_result;
};

static bool camera_future_completed(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire);
}

static void camera_future_complete(
    esp32_mquickjs_future_driver_state_t *state)
{
    atomic_store_explicit(&state->completed, true, memory_order_release);
}

static bool camera_future_cancelled(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(
                                &state->cancelled, memory_order_acquire);
}

static void camera_future_mark_cancelled(
    esp32_mquickjs_future_driver_state_t *state)
{
    atomic_store_explicit(&state->cancelled, true, memory_order_release);
}

static esp32_mquickjs_camera_slot_t s_camera;
static uint32_t s_camera_next_generation = 1;

static uint32_t camera_take_generation(void)
{
    uint32_t generation = s_camera_next_generation++;

    if (generation == 0) {
        generation = s_camera_next_generation++;
    }
    return generation;
}

static bool camera_to_i32(JSContext *ctx, JSValue value, int *out)
{
    double number;
    int converted;

    if (!JS_IsNumber(ctx, value) ||
        JS_ToNumber(ctx, &number, value) != 0 || !isfinite(number) ||
        number < INT32_MIN || number > INT32_MAX) {
        return false;
    }
    converted = (int)number;
    if ((double)converted != number) {
        return false;
    }
    *out = converted;
    return true;
}

static bool camera_to_u32(JSContext *ctx, JSValue value, uint32_t *out)
{
    int raw;

    if (!camera_to_i32(ctx, value, &raw) || raw < 0) {
        return false;
    }
    *out = (uint32_t)raw;
    return true;
}

static bool camera_to_bool(JSContext *ctx, JSValue value, bool *out)
{
    (void)ctx;
    if (!JS_IsBool(value)) {
        return false;
    }
    *out = value == JS_TRUE;
    return true;
}

static const char *camera_pixel_format_name(pixformat_t format)
{
    switch (format) {
    case PIXFORMAT_JPEG:
        return "jpeg";
    case PIXFORMAT_GRAYSCALE:
        return "grayscale";
    case PIXFORMAT_RGB565:
        return "rgb565";
    default:
        return "unknown";
    }
}

static bool camera_parse_pixel_format(JSContext *ctx, JSValue value,
                                      pixformat_t *out)
{
    JSCStringBuf buffer;
    const char *text;

    if (!JS_IsString(ctx, value) ||
        (text = JS_ToCString(ctx, value, &buffer)) == NULL) {
        return false;
    }
    if (strcmp(text, "jpeg") == 0) {
        *out = PIXFORMAT_JPEG;
    } else if (strcmp(text, "grayscale") == 0) {
        *out = PIXFORMAT_GRAYSCALE;
    } else if (strcmp(text, "rgb565") == 0) {
        *out = PIXFORMAT_RGB565;
    } else {
        return false;
    }
    return true;
}

typedef struct {
    const char *name;
    framesize_t value;
} camera_frame_size_entry_t;

static const camera_frame_size_entry_t s_frame_sizes[] = {
    {"96x96", FRAMESIZE_96X96},
    {"qqvga", FRAMESIZE_QQVGA},
    {"128x128", FRAMESIZE_128X128},
    {"qcif", FRAMESIZE_QCIF},
    {"hqvga", FRAMESIZE_HQVGA},
    {"qvga", FRAMESIZE_QVGA},
    {"cif", FRAMESIZE_CIF},
    {"vga", FRAMESIZE_VGA},
    {"svga", FRAMESIZE_SVGA},
    {"xga", FRAMESIZE_XGA},
    {"sxga", FRAMESIZE_SXGA},
    {"uxga", FRAMESIZE_UXGA},
};

static const char *camera_frame_size_name(framesize_t value)
{
    size_t i;

    for (i = 0; i < sizeof(s_frame_sizes) / sizeof(s_frame_sizes[0]); ++i) {
        if (s_frame_sizes[i].value == value) {
            return s_frame_sizes[i].name;
        }
    }
    return "unknown";
}

static bool camera_parse_frame_size(JSContext *ctx, JSValue value,
                                    framesize_t *out)
{
    JSCStringBuf buffer;
    const char *text;
    size_t i;

    if (!JS_IsString(ctx, value) ||
        (text = JS_ToCString(ctx, value, &buffer)) == NULL) {
        return false;
    }
    for (i = 0; i < sizeof(s_frame_sizes) / sizeof(s_frame_sizes[0]); ++i) {
        if (strcmp(text, s_frame_sizes[i].name) == 0) {
            *out = s_frame_sizes[i].value;
            return true;
        }
    }
    return false;
}

static const char *camera_sensor_model(const sensor_t *sensor)
{
    if (sensor == NULL) {
        return "unknown";
    }
#if CONFIG_OV2640_SUPPORT
    if (sensor->id.PID == OV2640_PID) {
        return "ov2640";
    }
#endif
#if CONFIG_OV3660_SUPPORT
    if (sensor->id.PID == OV3660_PID) {
        return "ov3660";
    }
#endif
#if CONFIG_OV5640_SUPPORT
    if (sensor->id.PID == OV5640_PID) {
        return "ov5640";
    }
#endif
    return "unknown";
}

static bool camera_sensor_supported(const sensor_t *sensor)
{
    return strcmp(camera_sensor_model(sensor), "unknown") != 0;
}

static bool camera_slot_matches(uint32_t generation)
{
    return s_camera.allocated && s_camera.generation == generation;
}

static bool camera_frame_storage_matches(uint32_t camera_generation,
                                         uint32_t frame_generation)
{
    return camera_slot_matches(camera_generation) &&
           s_camera.leased_fb != NULL &&
           s_camera.frame_generation == frame_generation;
}

static bool camera_frame_matches(uint32_t camera_generation,
                                 uint32_t frame_generation)
{
    return camera_frame_storage_matches(camera_generation,
                                        frame_generation) &&
           !s_camera.frame_revoked;
}

static esp_err_t camera_cleanup(void);
static void camera_close_schedule_cleanup(
    esp32_mquickjs_future_driver_state_t *state);

static void camera_cleanup_if_ready(void)
{
    if (s_camera.release_pending && !s_camera.busy &&
        s_camera.leased_fb == NULL && !s_camera.source_active &&
        s_camera.bitmap_read_leases == 0) {
        if (s_camera.close_state != NULL) {
            camera_close_schedule_cleanup(s_camera.close_state);
        } else {
            (void)camera_cleanup();
        }
    }
}

static void camera_release_frame(void)
{
    if (s_camera.source_active || s_camera.bitmap_read_leases != 0) {
        return;
    }
    if (s_camera.leased_fb != NULL) {
        esp_camera_fb_return(s_camera.leased_fb);
        s_camera.leased_fb = NULL;
    }
    s_camera.frame_revoked = false;
    camera_cleanup_if_ready();
}

static void camera_revoke_frame(void)
{
    esp32_mquickjs_camera_source_t *source = s_camera.source;

    if (s_camera.leased_fb == NULL) {
        return;
    }
    s_camera.frame_revoked = true;
    if (source != NULL) {
        source->consumed = true;
        if (!source->iterator_active) {
            s_camera.source_active = false;
            s_camera.source = NULL;
        }
    }
    camera_release_frame();
}

static void camera_release_leases(void)
{
    esp32_mquickjs_peripheral_lease_release(&s_camera.ledc_channel_lease);
    esp32_mquickjs_peripheral_lease_release(&s_camera.ledc_timer_lease);
    esp32_mquickjs_peripheral_lease_release(&s_camera.i2c_lease);
    esp32_mquickjs_peripheral_lease_release(&s_camera.camera_lease);
}

static esp_err_t camera_cleanup(void)
{
    esp32_mquickjs_camera_driver_resources_t resources;
    esp_err_t err;

    if (!s_camera.allocated) {
        return ESP_OK;
    }
    if (s_camera.busy || s_camera.source_active ||
        s_camera.bitmap_read_leases != 0 || s_camera.close_state != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_camera.release_pending = false;
    camera_release_frame();
    resources = (esp32_mquickjs_camera_driver_resources_t){
        .initialized = s_camera.initialized,
    };
    err = (esp_err_t)esp32_mquickjs_camera_driver_resources_deinit(
        &resources, &s_camera_driver_resource_ops);
    s_camera.initialized = resources.initialized;
    if (err != ESP_OK) {
        s_camera.release_pending = true;
        return err;
    }
    camera_release_leases();
    memset(&s_camera, 0, sizeof(s_camera));
    return ESP_OK;
}

static int camera_from_value(JSContext *ctx, JSValue value,
                             const char *api_name,
                             esp32_mquickjs_camera_ref_t **out_ref)
{
    esp32_mquickjs_camera_ref_t *ref;

    if (JS_GetClassID(ctx, value) != JS_CLASS_CAMERA ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "%s expects a Camera", api_name);
        return -1;
    }
    if (!camera_slot_matches(ref->generation) || s_camera.release_pending) {
        JS_ThrowReferenceError(ctx, "%s failed because the Camera is closed",
                               api_name);
        return -1;
    }
    if (out_ref != NULL) {
        *out_ref = ref;
    }
    return 0;
}

static int camera_frame_from_value(
    JSContext *ctx, JSValue value, const char *api_name,
    esp32_mquickjs_camera_frame_ref_t **out_ref)
{
    esp32_mquickjs_camera_frame_ref_t *ref;

    if (JS_GetClassID(ctx, value) != JS_CLASS_CAMERA_FRAME ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "%s expects a CameraFrame", api_name);
        return -1;
    }
    if (!camera_frame_matches(ref->camera_generation,
                              ref->frame_generation)) {
        JS_ThrowReferenceError(ctx,
                               "%s failed because the CameraFrame is closed",
                               api_name);
        return -1;
    }
    if (out_ref != NULL) {
        *out_ref = ref;
    }
    return 0;
}

bool esp32_mquickjs_camera_frame_acquire_bitmap_view(
    JSContext *ctx,
    JSValue value,
    const char *api_name,
    esp32_mquickjs_camera_bitmap_lease_t *out_lease)
{
    esp32_mquickjs_camera_frame_ref_t *ref;
    camera_fb_t *frame;
    esp32_mquickjs_bitmap_pixel_format_t format;
    uint32_t bytes_per_pixel;
    size_t required;

    if (out_lease == NULL) {
        return false;
    }
    memset(out_lease, 0, sizeof(*out_lease));
    if (camera_frame_from_value(ctx, value, api_name, &ref) != 0) {
        return false;
    }
    if (s_camera.source_active) {
        JS_ThrowInternalError(
            ctx, "%s failed because the CameraFrame is busy", api_name);
        return false;
    }
    frame = s_camera.leased_fb;
    switch (frame->format) {
        case PIXFORMAT_GRAYSCALE:
            format = ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8;
            bytes_per_pixel = 1U;
            break;
        case PIXFORMAT_RGB565:
            format = ESP32_MQUICKJS_BITMAP_FORMAT_RGB565;
            bytes_per_pixel = 2U;
            break;
        case PIXFORMAT_RGB888:
            format = ESP32_MQUICKJS_BITMAP_FORMAT_RGB888;
            bytes_per_pixel = 3U;
            break;
        default:
            JS_ThrowTypeError(
                ctx,
                "%s requires a raw grayscale, rgb565, or rgb888 CameraFrame; decode JPEG/BMP separately",
                api_name);
            return false;
    }
    if (frame->width == 0 || frame->height == 0 ||
        frame->width > UINT32_MAX || frame->height > UINT32_MAX ||
        frame->width > UINT32_MAX / bytes_per_pixel ||
        frame->height > SIZE_MAX / (frame->width * bytes_per_pixel)) {
        JS_ThrowInternalError(ctx, "%s received invalid CameraFrame dimensions",
                              api_name);
        return false;
    }
    required = frame->width * frame->height * bytes_per_pixel;
    if (required > frame->len) {
        JS_ThrowInternalError(ctx, "%s received a truncated CameraFrame",
                              api_name);
        return false;
    }
    if (s_camera.bitmap_read_leases == UINT16_MAX) {
        JS_ThrowInternalError(ctx, "%s could not acquire a CameraFrame read lease",
                              api_name);
        return false;
    }
    ++s_camera.bitmap_read_leases;
    out_lease->view.data = frame->buf;
    out_lease->view.length = frame->len;
    out_lease->view.width = (uint32_t)frame->width;
    out_lease->view.height = (uint32_t)frame->height;
    out_lease->view.stride = (uint32_t)frame->width * bytes_per_pixel;
    out_lease->view.format = format;
    out_lease->view.layout = ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR;
    out_lease->view.byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE;
    out_lease->view.bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB;
    out_lease->camera_generation = ref->camera_generation;
    out_lease->frame_generation = ref->frame_generation;
    out_lease->active = true;
    return true;
}

void esp32_mquickjs_camera_frame_release_bitmap_view(
    esp32_mquickjs_camera_bitmap_lease_t *lease)
{
    if (lease == NULL || !lease->active) {
        return;
    }
    if (camera_frame_storage_matches(lease->camera_generation,
                                     lease->frame_generation) &&
        s_camera.bitmap_read_leases != 0) {
        --s_camera.bitmap_read_leases;
    }
    lease->active = false;
    if (s_camera.frame_revoked) {
        camera_release_frame();
    } else {
        camera_cleanup_if_ready();
    }
}

static JSValue camera_make_object(JSContext *ctx)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    esp32_mquickjs_camera_ref_t *ref;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_CAMERA);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    ref = heap_caps_malloc(sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    ref->generation = s_camera.generation;
    JS_SetOpaque(ctx, *object, ref);
    return JS_PopGCRef(ctx, &object_ref);
}

static uint64_t camera_frame_timestamp_us(const camera_fb_t *frame)
{
    return frame == NULL ? 0 :
        (uint64_t)frame->timestamp.tv_sec * 1000000ULL +
        (uint64_t)frame->timestamp.tv_usec;
}

static JSValue camera_make_frame_object(JSContext *ctx, JSValue camera_owner)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    esp32_mquickjs_camera_frame_ref_t *ref;
    camera_fb_t *frame = s_camera.leased_fb;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_CAMERA_FRAME);
    if (JS_IsException(*object) || frame == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    ref = heap_caps_malloc(sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    ref->camera_generation = s_camera.generation;
    ref->frame_generation = s_camera.frame_generation;
    JS_SetOpaque(ctx, *object, ref);
    if (!esp32_mquickjs_set_property_ref(ctx, object, "width",
                                         JS_NewInt64(ctx, frame->width)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "height",
                                         JS_NewInt64(ctx, frame->height)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "format",
            JS_NewString(ctx, camera_pixel_format_name(frame->format))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "byteLength",
                                         JS_NewInt64(ctx, frame->len)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "timestampUs",
            JS_NewInt64(ctx, (int64_t)camera_frame_timestamp_us(frame))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "sequence",
            JS_NewUint32(ctx, s_camera.sequence++)) ||
        !esp32_mquickjs_set_property_ref(ctx, object,
                                         CAMERA_FRAME_OWNER_KEY,
                                         camera_owner)) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

static bool camera_source_next(JSContext *ctx, void *opaque,
                               esp32_mquickjs_byte_span_t *out)
{
    esp32_mquickjs_camera_source_t *source = opaque;
    camera_fb_t *frame;
    size_t remaining;
    size_t length;

    (void)ctx;
    if (source == NULL || source->destroy_requested || source->consumed ||
        !camera_frame_matches(source->camera_generation,
                              source->frame_generation)) {
        return false;
    }
    frame = s_camera.leased_fb;
    if (source->offset >= frame->len) {
        source->consumed = true;
        return false;
    }
    remaining = frame->len - source->offset;
    length = remaining < source->chunk_bytes ? remaining : source->chunk_bytes;
    out->data = frame->buf + source->offset;
    out->length = length;
    out->owner = JS_UNDEFINED;
    out->dma_capable = esp_ptr_dma_capable(out->data) ||
                       esp_ptr_dma_ext_capable(out->data);
    source->offset += length;
    return true;
}

static void camera_source_iterator_close(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_camera_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) {
        return;
    }
    source->iterator_active = false;
    source->consumed = true;
    if (camera_frame_storage_matches(source->camera_generation,
                                     source->frame_generation)) {
        if (s_camera.source == source) {
            s_camera.source = NULL;
            s_camera.source_active = false;
        }
        camera_release_frame();
    }
    if (source->destroy_requested) {
        heap_caps_free(source);
    }
}

static bool camera_source_open(JSContext *ctx, JSValue source_value,
                               void *opaque,
                               esp32_mquickjs_byte_span_source_t *out,
                               JSValue *out_error)
{
    esp32_mquickjs_camera_source_t *source = opaque;

    (void)source_value;
    if (source == NULL || source->opened || source->consumed ||
        source->destroy_requested ||
        !camera_frame_matches(source->camera_generation,
                              source->frame_generation)) {
        *out_error = JS_ThrowReferenceError(
            ctx, "CameraFrame source is closed or has already been consumed");
        return false;
    }
    source->opened = true;
    source->iterator_active = true;
    out->opaque = source;
    out->next = camera_source_next;
    out->close = camera_source_iterator_close;
    return true;
}

static size_t camera_source_known_length(void *opaque)
{
    esp32_mquickjs_camera_source_t *source = opaque;

    return source != NULL &&
                   camera_frame_matches(source->camera_generation,
                                        source->frame_generation)
               ? s_camera.leased_fb->len
               : 0;
}

static void camera_source_destroy(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_camera_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) {
        return;
    }
    if (source->iterator_active) {
        source->destroy_requested = true;
        source->consumed = true;
        return;
    }
    if (camera_frame_storage_matches(source->camera_generation,
                                     source->frame_generation)) {
        if (s_camera.source == source) {
            s_camera.source = NULL;
            s_camera.source_active = false;
        }
        camera_release_frame();
    }
    heap_caps_free(source);
}

static const esp32_mquickjs_byte_span_source_object_ops_t s_camera_source_ops = {
    .class_id = JS_CLASS_BYTE_SPAN_SOURCE,
    .open = camera_source_open,
    .known_length = camera_source_known_length,
    .destroy = camera_source_destroy,
};

static void camera_capture_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    uint64_t elapsed_us;

    if (state == NULL) {
        return;
    }
    for (;;) {
        if (camera_future_cancelled(state)) {
            break;
        }
        elapsed_us = (uint64_t)esp_timer_get_time() - state->started_us;
        if (state->timeout_ms > 0 &&
            elapsed_us >= (uint64_t)state->timeout_ms * 1000ULL) {
            break;
        }
        if (esp_camera_available_frames()) {
            state->frame = cam_take(1);
            if (state->frame != NULL) {
                state->frame->width = resolution[state->frame_size].width;
                state->frame->height = resolution[state->frame_size].height;
                state->frame->format = state->pixel_format;
                break;
            }
        }
        if (state->timeout_ms == 0) {
            break;
        }
        vTaskDelay(1);
    }
    camera_future_complete(state);
}

static bool camera_capture_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_camera_ref_t *camera_ref;
    uint32_t timeout_ms;
    JSValue *owner;

    if (out_state == NULL || argc > 1 ||
        camera_from_value(ctx, this_ref->val, "Camera.capture()",
                          &camera_ref) != 0) {
        return false;
    }
    timeout_ms = s_camera.default_timeout_ms;
    if (argc == 1 && !JS_IsUndefined(argv[0].val) &&
        !camera_to_u32(ctx, argv[0].val, &timeout_ms)) {
        JS_ThrowTypeError(ctx,
                          "Camera.capture(timeoutMs?) expects a non-negative integer");
        return false;
    }
    if (s_camera.busy) {
        JS_ThrowInternalError(ctx, "Camera already has a pending capture");
        return false;
    }
    if (s_camera.leased_fb != NULL) {
        JS_ThrowInternalError(ctx,
                              "Camera.capture() requires the previous frame to be closed");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    atomic_init(&state->completed, false);
    atomic_init(&state->cancelled, false);
    state->ctx = ctx;
    state->camera_generation = camera_ref->generation;
    state->timeout_ms = timeout_ms;
    owner = JS_AddGCRef(ctx, &state->owner_ref);
    *owner = this_ref->val;
    state->owner_retained = true;
    *out_state = state;
    return true;
}

static bool camera_capture_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || !camera_slot_matches(state->camera_generation)) {
        JS_ThrowReferenceError(ctx, "Camera closed before capture started");
        return false;
    }
    if (s_camera.busy) {
        JS_ThrowInternalError(ctx, "Camera already has a pending capture");
        return false;
    }
    s_camera.busy = true;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    state->started_us = (uint64_t)esp_timer_get_time();
    state->frame_size = s_camera.frame_size;
    state->pixel_format = s_camera.pixel_format;
    s_camera.capture_state = state;
    if (!esp32_mquickjs_future_submit_worker(runtime, token,
                                             camera_capture_worker, state)) {
        JS_ThrowInternalError(ctx, "Camera capture worker queue is full");
        return false;
    }
    return true;
}

static esp32_mquickjs_future_poll_t camera_capture_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return camera_future_completed(state)
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue camera_capture_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    uint64_t elapsed_ms;
    JSValue result;

    if (state == NULL || camera_future_cancelled(state)) {
        return JS_ThrowInternalError(ctx, "Camera capture cancelled");
    }
    if (!camera_slot_matches(state->camera_generation) ||
        s_camera.release_pending) {
        return JS_ThrowReferenceError(ctx, "Camera closed during capture");
    }
    elapsed_ms = ((uint64_t)esp_timer_get_time() - state->started_us) / 1000ULL;
    if (state->frame == NULL ||
        (state->timeout_ms > 0 && elapsed_ms > state->timeout_ms)) {
        if (state->frame != NULL) {
            esp_camera_fb_return(state->frame);
            state->frame = NULL;
        }
        return JS_NULL;
    }
    s_camera.leased_fb = state->frame;
    state->frame = NULL;
    s_camera.frame_generation = camera_take_generation();
    result = camera_make_frame_object(ctx, state->owner_ref.val);
    if (JS_IsException(result)) {
        camera_release_frame();
    }
    return result;
}

static esp32_mquickjs_cancel_result_t camera_capture_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    bool interrupted;

    if (state == NULL || camera_future_cancelled(state)) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    interrupted = !camera_future_completed(state);
    camera_future_mark_cancelled(state);
    if (state->started && camera_slot_matches(state->camera_generation)) {
        s_camera.release_pending = true;
    }
    return interrupted
        ? ESP32_MQUICKJS_CANCEL_REQUESTED
        : ESP32_MQUICKJS_CANCELLED;
}

static void camera_capture_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->frame != NULL) {
        esp_camera_fb_return(state->frame);
    }
    if (state->started && camera_slot_matches(state->camera_generation)) {
        if (s_camera.capture_state == state) {
            s_camera.capture_state = NULL;
        }
        s_camera.busy = false;
        camera_cleanup_if_ready();
    }
    if (state->owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
    }
    heap_caps_free(state);
}

static const esp32_mquickjs_future_driver_t s_camera_capture_driver = {
    .capture = camera_capture_prepare,
    .start = camera_capture_start,
    .poll = camera_capture_poll,
    .finish = camera_capture_finish,
    .cancel = camera_capture_cancel,
    .destroy = camera_capture_destroy,
};

static void camera_close_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    esp32_mquickjs_camera_driver_resources_t resources;

    if (state == NULL) {
        return;
    }
    resources = (esp32_mquickjs_camera_driver_resources_t){
        .initialized = state->close_initialized,
    };
    state->close_result =
        (esp_err_t)esp32_mquickjs_camera_driver_resources_deinit(
            &resources, &s_camera_driver_resource_ops);
    state->close_initialized = resources.initialized;
    camera_future_complete(state);
}

static void camera_close_schedule_cleanup(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->cleanup_started ||
        !camera_slot_matches(state->camera_generation)) {
        return;
    }
    state->cleanup_started = true;
    state->cleanup_submitted = esp32_mquickjs_future_submit_worker(
        state->runtime, state->token, camera_close_worker, state);
    if (!state->cleanup_submitted) {
        camera_future_complete(state);
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
}

static bool camera_close_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_camera_ref_t *camera_ref;
    JSValue *owner;

    (void)argv;
    if (out_state == NULL || argc != 0 ||
        JS_GetClassID(ctx, this_ref->val) != JS_CLASS_CAMERA) {
        JS_ThrowTypeError(ctx, "Camera.close() expects no arguments");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    atomic_init(&state->completed, false);
    atomic_init(&state->cancelled, false);
    state->ctx = ctx;
    state->close_result = ESP_OK;
    camera_ref = JS_GetOpaque(ctx, this_ref->val);
    if (camera_ref != NULL && camera_slot_matches(camera_ref->generation)) {
        state->camera_generation = camera_ref->generation;
        owner = JS_AddGCRef(ctx, &state->owner_ref);
        *owner = this_ref->val;
        state->owner_retained = true;
    }
    *out_state = state;
    return true;
}

static bool camera_close_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (state->camera_generation == 0 ||
        !camera_slot_matches(state->camera_generation)) {
        camera_future_complete(state);
        (void)esp32_mquickjs_future_wake(runtime, token);
        return true;
    }
    if (s_camera.close_state != NULL) {
        JS_ThrowInternalError(ctx, "Camera close is already pending");
        return false;
    }
    state->close_initialized = s_camera.initialized;
    s_camera.release_pending = true;
    s_camera.close_state = state;
    if (s_camera.capture_state != NULL) {
        (void)camera_capture_cancel(s_camera.capture_state);
    }
    camera_revoke_frame();
    camera_cleanup_if_ready();
    return true;
}

static esp32_mquickjs_future_poll_t camera_close_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (!camera_future_completed(state)) {
        return ESP32_MQUICKJS_FUTURE_PENDING;
    }
    if (!state->cleanup_finalized) {
        if (!state->cleanup_submitted) {
            esp32_mquickjs_camera_driver_resources_t resources = {
                .initialized = state->close_initialized,
            };

            state->close_result =
                (esp_err_t)esp32_mquickjs_camera_driver_resources_deinit(
                    &resources, &s_camera_driver_resource_ops);
            state->close_initialized = resources.initialized;
        }
        if (camera_slot_matches(state->camera_generation) &&
            s_camera.close_state == state) {
            s_camera.initialized = state->close_initialized;
            if (state->close_result != ESP_OK) {
                s_camera.release_pending = true;
                s_camera.close_state = NULL;
                state->cleanup_finalized = true;
                return ESP32_MQUICKJS_FUTURE_READY;
            }
            if (state->owner_retained) {
                esp32_mquickjs_camera_ref_t *camera_ref =
                    JS_GetOpaque(state->ctx, state->owner_ref.val);

                if (camera_ref != NULL &&
                    camera_ref->generation == state->camera_generation) {
                    JS_SetOpaque(state->ctx, state->owner_ref.val, NULL);
                    heap_caps_free(camera_ref);
                }
            }
            camera_release_leases();
            memset(&s_camera, 0, sizeof(s_camera));
        }
        state->cleanup_finalized = true;
    }
    return ESP32_MQUICKJS_FUTURE_READY;
}

static JSValue camera_close_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && state->close_result != ESP_OK) {
        return JS_ThrowInternalError(ctx,
                                     "Camera.close() failed: %s",
                                     esp_err_to_name(state->close_result));
    }
    return JS_TRUE;
}

static esp32_mquickjs_cancel_result_t camera_close_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return ESP32_MQUICKJS_CANCEL_REJECTED;
}

static void camera_close_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
    }
    heap_caps_free(state);
}

static const esp32_mquickjs_future_driver_t s_camera_close_driver = {
    .capture = camera_close_prepare,
    .start = camera_close_start,
    .poll = camera_close_poll,
    .finish = camera_close_finish,
    .cancel = camera_close_cancel,
    .destroy = camera_close_destroy,
};

static bool camera_register_future_driver(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef object_ref;
    JSGCRef capture_ref;
    JSGCRef close_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *capture = JS_PushGCRef(ctx, &capture_ref);
    JSValue *close = JS_PushGCRef(ctx, &close_ref);
    bool result;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_CAMERA);
    *capture = JS_IsException(*object)
                   ? JS_EXCEPTION
                   : JS_GetPropertyStr(ctx, *object, "capture");
    *close = JS_IsException(*object)
                 ? JS_EXCEPTION
                 : JS_GetPropertyStr(ctx, *object, "close");
    result = !JS_IsException(*capture) &&
             !JS_IsException(*close) &&
             esp32_mquickjs_future_register_driver(
                 ctx, runtime, *capture, &s_camera_capture_driver) &&
             esp32_mquickjs_future_register_driver(
                 ctx, runtime, *close, &s_camera_close_driver);
    if (!result && !JS_IsException(*object)) {
        JS_ThrowInternalError(ctx, "failed to register Camera Future driver");
    }
    JS_PopGCRef(ctx, &close_ref);
    JS_PopGCRef(ctx, &capture_ref);
    JS_PopGCRef(ctx, &object_ref);
    return result;
}

bool esp32_mquickjs_init_camera_runtime(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    esp_err_t err;

    if (s_camera.allocated) {
        s_camera.release_pending = true;
        err = camera_cleanup();
        if (err != ESP_OK) {
            JS_ThrowInternalError(ctx,
                                  "camera runtime cleanup failed: %s",
                                  esp_err_to_name(err));
            return false;
        }
    }
    return camera_register_future_driver(ctx, runtime);
}

void esp32_mquickjs_deinit_camera_runtime(JSContext *ctx)
{
    (void)ctx;
    if (!s_camera.allocated) {
        return;
    }
    s_camera.release_pending = true;
    if (!s_camera.busy) {
        camera_cleanup();
    }
}

JSValue js_camera_constructor(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "Camera cannot be constructed directly");
}

void js_camera_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_camera_ref_t *ref = opaque;

    (void)ctx;
    if (ref != NULL && camera_slot_matches(ref->generation)) {
        s_camera.release_pending = true;
        camera_cleanup_if_ready();
    }
    heap_caps_free(ref);
}

JSValue js_camera_capture(JSContext *ctx, JSValue *this_val,
                          int argc, JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *method = JS_GetPropertyStr(ctx, *this_val, "capture");
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

static JSValue camera_make_status(JSContext *ctx)
{
    JSGCRef result_ref;
    JSGCRef sensor_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *sensor = JS_PushGCRef(ctx, &sensor_ref);

    *result = JS_NewObject(ctx);
    *sensor = JS_NewObject(ctx);
    if (JS_IsException(*result) || JS_IsException(*sensor) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "opened", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "capturePending",
                                         JS_NewBool(s_camera.busy)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "frameLeased",
                                         JS_NewBool(s_camera.leased_fb != NULL)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "pixelFormat",
            JS_NewString(ctx, camera_pixel_format_name(s_camera.pixel_format))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "frameSize",
            JS_NewString(ctx, camera_frame_size_name(s_camera.frame_size))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "jpegQuality",
                                         JS_NewInt32(ctx, s_camera.jpeg_quality)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "frameBuffers",
            JS_NewInt64(ctx, (int64_t)s_camera.frame_buffers)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "grabMode",
            JS_NewString(ctx, s_camera.grab_mode == CAMERA_GRAB_LATEST
                                  ? "latest" : "whenEmpty")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "bufferLocation",
            JS_NewString(ctx, s_camera.buffer_location == CAMERA_FB_IN_PSRAM
                                  ? "psram" : "dram")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "psramDma",
                                         JS_NewBool(s_camera.psram_dma)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, sensor, "model",
            JS_NewString(ctx, camera_sensor_model(s_camera.sensor))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, sensor, "pid",
            JS_NewUint32(ctx, s_camera.sensor != NULL
                                  ? s_camera.sensor->id.PID : 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "sensor", *sensor)) {
        JS_PopGCRef(ctx, &sensor_ref);
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    *sensor = JS_UNDEFINED;
    JS_PopGCRef(ctx, &sensor_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_camera_status(JSContext *ctx, JSValue *this_val,
                         int argc, JSValue *argv)
{
    (void)argc;
    (void)argv;
    if (camera_from_value(ctx, *this_val, "Camera.status()", NULL) != 0) {
        return JS_EXCEPTION;
    }
    return camera_make_status(ctx);
}

JSValue js_camera_controls(JSContext *ctx, JSValue *this_val,
                           int argc, JSValue *argv)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    camera_status_t *status;

    (void)argc;
    (void)argv;
    if (camera_from_value(ctx, *this_val, "Camera.controls()", NULL) != 0) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    status = &s_camera.sensor->status;
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "frameSize",
            JS_NewString(ctx, camera_frame_size_name(status->framesize))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "jpegQuality",
                                         JS_NewInt32(ctx, status->quality)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "brightness",
                                         JS_NewInt32(ctx, status->brightness)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "contrast",
                                         JS_NewInt32(ctx, status->contrast)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "saturation",
                                         JS_NewInt32(ctx, status->saturation)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "horizontalMirror",
                                         JS_NewBool(status->hmirror != 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "verticalFlip",
                                         JS_NewBool(status->vflip != 0))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_camera_set_control(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    JSCStringBuf name_buf;
    const char *name;
    int value = 0;
    int result = -1;
    bool bool_value;
    framesize_t frame_size;

    if (camera_from_value(ctx, *this_val, "Camera.setControl()", NULL) != 0) {
        return JS_EXCEPTION;
    }
    if (s_camera.busy) {
        return JS_ThrowInternalError(
            ctx, "Camera.setControl() refused while capture is pending");
    }
    if (argc != 2 || !JS_IsString(ctx, argv[0]) ||
        (name = JS_ToCString(ctx, argv[0], &name_buf)) == NULL) {
        return JS_ThrowTypeError(ctx,
                                 "Camera.setControl(name, value) expects two arguments");
    }
    if (strcmp(name, "frameSize") == 0) {
        if (!camera_parse_frame_size(ctx, argv[1], &frame_size)) {
            return JS_ThrowRangeError(ctx, "unsupported camera frame size");
        }
        result = s_camera.sensor->set_framesize(s_camera.sensor, frame_size);
        if (result == 0) {
            s_camera.frame_size = frame_size;
        }
    } else if (strcmp(name, "jpegQuality") == 0) {
        if (!camera_to_i32(ctx, argv[1], &value) || value < 0 || value > 63) {
            return JS_ThrowRangeError(ctx, "jpegQuality must be 0..63");
        }
        result = s_camera.sensor->set_quality(s_camera.sensor, value);
        if (result == 0) {
            s_camera.jpeg_quality = value;
        }
    } else if (strcmp(name, "brightness") == 0 ||
               strcmp(name, "contrast") == 0 ||
               strcmp(name, "saturation") == 0) {
        if (!camera_to_i32(ctx, argv[1], &value) || value < -2 || value > 2) {
            return JS_ThrowRangeError(ctx, "%s must be -2..2", name);
        }
        result = strcmp(name, "brightness") == 0
                     ? s_camera.sensor->set_brightness(s_camera.sensor, value)
                     : strcmp(name, "contrast") == 0
                           ? s_camera.sensor->set_contrast(s_camera.sensor, value)
                           : s_camera.sensor->set_saturation(s_camera.sensor, value);
    } else if (strcmp(name, "horizontalMirror") == 0 ||
               strcmp(name, "verticalFlip") == 0) {
        if (!camera_to_bool(ctx, argv[1], &bool_value)) {
            return JS_ThrowTypeError(ctx, "%s expects a boolean", name);
        }
        result = strcmp(name, "horizontalMirror") == 0
                     ? s_camera.sensor->set_hmirror(s_camera.sensor,
                                                    bool_value ? 1 : 0)
                     : s_camera.sensor->set_vflip(s_camera.sensor,
                                                  bool_value ? 1 : 0);
    } else {
        return JS_ThrowRangeError(ctx, "unsupported camera control: %s", name);
    }
    if (result != 0) {
        return JS_ThrowInternalError(ctx, "camera sensor rejected control %s", name);
    }
    return js_camera_controls(ctx, this_val, 0, NULL);
}

JSValue js_camera_close(JSContext *ctx, JSValue *this_val,
                        int argc, JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *method = JS_GetPropertyStr(ctx, *this_val, "close");
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

JSValue js_camera_frame_constructor(JSContext *ctx, JSValue *this_val,
                                    int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx,
                             "CameraFrame cannot be constructed directly");
}

void js_camera_frame_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_camera_frame_ref_t *ref = opaque;

    (void)ctx;
    if (ref != NULL &&
        camera_frame_storage_matches(ref->camera_generation,
                                     ref->frame_generation)) {
        camera_release_frame();
    }
    heap_caps_free(ref);
}

JSValue js_camera_frame_source(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv)
{
    esp32_mquickjs_camera_frame_ref_t *ref;
    esp32_mquickjs_camera_source_t *source;
    uint32_t chunk_bytes = CAMERA_DEFAULT_CHUNK_BYTES;

    if (camera_frame_from_value(ctx, *this_val, "CameraFrame.source()", &ref) != 0) {
        return JS_EXCEPTION;
    }
    if (s_camera.source_active) {
        return JS_ThrowInternalError(ctx,
                                     "CameraFrame already has an active source");
    }
    if (s_camera.bitmap_read_leases != 0) {
        return JS_ThrowInternalError(
            ctx, "CameraFrame.source() failed because the CameraFrame is busy");
    }
    if (argc > 1) {
        return JS_ThrowTypeError(ctx,
                                 "CameraFrame.source(options?) expects at most one argument");
    }
    if (argc == 1 && !JS_IsUndefined(argv[0])) {
        JSValue property;

        if (JS_GetClassID(ctx, argv[0]) < 0 || JS_IsArray(ctx, argv[0])) {
            return JS_ThrowTypeError(ctx,
                                     "CameraFrame.source({ chunkBytes }) expects an object");
        }
        property = JS_GetPropertyStr(ctx, argv[0], "chunkBytes");
        if (JS_IsException(property) ||
            (!JS_IsUndefined(property) &&
             !camera_to_u32(ctx, property, &chunk_bytes))) {
            return JS_ThrowTypeError(ctx,
                                     "CameraFrame.source({ chunkBytes }) expects an integer");
        }
    }
    if (chunk_bytes == 0 || chunk_bytes > CAMERA_MAX_CHUNK_BYTES) {
        return JS_ThrowRangeError(ctx,
                                  "CameraFrame source chunkBytes must be 1..32768");
    }
    source = heap_caps_calloc(1, sizeof(*source), MALLOC_CAP_8BIT);
    if (source == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    source->camera_generation = ref->camera_generation;
    source->frame_generation = ref->frame_generation;
    source->chunk_bytes = chunk_bytes;
    s_camera.source_active = true;
    s_camera.source = source;
    return esp32_mquickjs_new_byte_span_source(ctx, *this_val,
                                               &s_camera_source_ops, source);
}

JSValue js_camera_frame_read(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    esp32_mquickjs_camera_frame_ref_t *ref;
    uint32_t offset = 0;
    uint32_t limit = CAMERA_MAX_COPY_BYTES;
    size_t length;
    uint8_t *copy = NULL;

    if (camera_frame_from_value(ctx, *this_val, "CameraFrame.read()", &ref) != 0) {
        return JS_EXCEPTION;
    }
    (void)ref;
    if (argc > 2 ||
        (argc >= 1 && !JS_IsUndefined(argv[0]) &&
         !camera_to_u32(ctx, argv[0], &offset)) ||
        (argc >= 2 && !JS_IsUndefined(argv[1]) &&
         !camera_to_u32(ctx, argv[1], &limit))) {
        return JS_ThrowTypeError(ctx,
                                 "CameraFrame.read(offset?, limit?) expects non-negative integers");
    }
    if (limit > CAMERA_MAX_COPY_BYTES) {
        return JS_ThrowRangeError(ctx,
                                  "CameraFrame.read() copies at most 32 KiB");
    }
    if ((size_t)offset > s_camera.leased_fb->len) {
        return JS_ThrowRangeError(ctx, "CameraFrame.read() offset is out of range");
    }
    length = s_camera.leased_fb->len - offset;
    if (length > limit) {
        length = limit;
    }
    if (length > 0) {
        copy = esp32_mquickjs_memory_payload_alloc(
            length, ESP32_MQUICKJS_MEMORY_EXTERNAL);
        if (copy == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }
        memcpy(copy, s_camera.leased_fb->buf + offset, length);
    }
    return esp32_mquickjs_new_owned_byte_view(ctx, copy, length);
}

JSValue js_camera_frame_close(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    esp32_mquickjs_camera_frame_ref_t *ref;
    JSValue owner_result;

    (void)argc;
    (void)argv;
    if (JS_GetClassID(ctx, *this_val) != JS_CLASS_CAMERA_FRAME) {
        return JS_ThrowTypeError(ctx,
                                 "CameraFrame.close() expects a CameraFrame");
    }
    ref = JS_GetOpaque(ctx, *this_val);
    if (ref == NULL) {
        return JS_TRUE;
    }
    if (!camera_frame_matches(ref->camera_generation, ref->frame_generation)) {
        owner_result = JS_SetPropertyStr(ctx, *this_val,
                                         CAMERA_FRAME_OWNER_KEY,
                                         JS_UNDEFINED);
        JS_SetOpaque(ctx, *this_val, NULL);
        heap_caps_free(ref);
        return JS_IsException(owner_result) ? JS_EXCEPTION : JS_TRUE;
    }
    if (s_camera.source_active) {
        return JS_ThrowInternalError(ctx,
                                     "CameraFrame.close() refused while a source is active");
    }
    if (s_camera.bitmap_read_leases != 0) {
        return JS_ThrowInternalError(
            ctx, "CameraFrame.close() failed because the CameraFrame is busy");
    }
    camera_release_frame();
    owner_result = JS_SetPropertyStr(ctx, *this_val, CAMERA_FRAME_OWNER_KEY,
                                     JS_UNDEFINED);
    JS_SetOpaque(ctx, *this_val, NULL);
    heap_caps_free(ref);
    return JS_IsException(owner_result) ? JS_EXCEPTION : JS_TRUE;
}

static JSValue camera_string_array(JSContext *ctx,
                                   const char *const *values,
                                   size_t count)
{
    JSGCRef array_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    size_t i;

    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) {
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }
    for (i = 0; i < count; ++i) {
        if (JS_IsException(JS_SetPropertyUint32(
                ctx, *array, (uint32_t)i, JS_NewString(ctx, values[i])))) {
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
    }
    return JS_PopGCRef(ctx, &array_ref);
}

JSValue js_camera_capabilities(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv)
{
    static const char *const sensors[] = {
#if CONFIG_OV2640_SUPPORT
        "ov2640",
#endif
#if CONFIG_OV3660_SUPPORT
        "ov3660",
#endif
#if CONFIG_OV5640_SUPPORT
        "ov5640",
#endif
        NULL,
    };
    static const char *const formats[] = {"jpeg", "grayscale", "rgb565"};
    const char *frame_names[sizeof(s_frame_sizes) / sizeof(s_frame_sizes[0])];
    JSGCRef result_ref;
    JSGCRef sensors_ref;
    JSGCRef formats_ref;
    JSGCRef sizes_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *sensor_values = JS_PushGCRef(ctx, &sensors_ref);
    JSValue *format_values = JS_PushGCRef(ctx, &formats_ref);
    JSValue *size_values = JS_PushGCRef(ctx, &sizes_ref);
    size_t i;

    (void)this_val;
    (void)argc;
    (void)argv;
    for (i = 0; i < sizeof(s_frame_sizes) / sizeof(s_frame_sizes[0]); ++i) {
        frame_names[i] = s_frame_sizes[i].name;
    }
    *result = JS_NewObject(ctx);
    *sensor_values = camera_string_array(
        ctx, sensors, (sizeof(sensors) / sizeof(sensors[0])) - 1U);
    *format_values = camera_string_array(
        ctx, formats, sizeof(formats) / sizeof(formats[0]));
    *size_values = camera_string_array(
        ctx, frame_names, sizeof(frame_names) / sizeof(frame_names[0]));
    if (JS_IsException(*result) || JS_IsException(*sensor_values) ||
        JS_IsException(*format_values) || JS_IsException(*size_values) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "target",
                                         JS_NewString(ctx, CONFIG_IDF_TARGET)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "psram",
                                         JS_NewBool(camera_psram_is_initialized())) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "psramBytes",
                                         JS_NewInt64(ctx, camera_psram_size())) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "sensorDrivers",
                                         *sensor_values) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "pixelFormats",
                                         *format_values) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "frameSizes",
                                         *size_values)) {
        JS_PopGCRef(ctx, &sizes_ref);
        JS_PopGCRef(ctx, &formats_ref);
        JS_PopGCRef(ctx, &sensors_ref);
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    *sensor_values = JS_UNDEFINED;
    *format_values = JS_UNDEFINED;
    *size_values = JS_UNDEFINED;
    JS_PopGCRef(ctx, &sizes_ref);
    JS_PopGCRef(ctx, &formats_ref);
    JS_PopGCRef(ctx, &sensors_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

static int camera_profile_pin(const char *key)
{
    return esp32_mquickjs_profile_int_or(key, CAMERA_REQUIRED_PIN_MISSING);
}

static bool camera_read_optional_i32(JSContext *ctx, JSValue object,
                                     const char *name, int *value)
{
    JSValue property = JS_GetPropertyStr(ctx, object, name);

    return !JS_IsException(property) &&
           (JS_IsUndefined(property) || camera_to_i32(ctx, property, value));
}

static bool camera_read_optional_bool(JSContext *ctx, JSValue object,
                                      const char *name, bool *value)
{
    JSValue property = JS_GetPropertyStr(ctx, object, name);

    return !JS_IsException(property) &&
           (JS_IsUndefined(property) || camera_to_bool(ctx, property, value));
}

JSValue js_camera_open(JSContext *ctx, JSValue *this_val,
                       int argc, JSValue *argv)
{
    camera_config_t config = {
        .pin_pwdn = camera_profile_pin("ESP32QJS_CAMERA_PWDN"),
        .pin_reset = camera_profile_pin("ESP32QJS_CAMERA_RESET"),
        .pin_xclk = camera_profile_pin("ESP32QJS_CAMERA_XCLK"),
        .pin_sccb_sda = camera_profile_pin("ESP32QJS_CAMERA_SCCB_SDA"),
        .pin_sccb_scl = camera_profile_pin("ESP32QJS_CAMERA_SCCB_SCL"),
        .pin_d7 = camera_profile_pin("ESP32QJS_CAMERA_D7"),
        .pin_d6 = camera_profile_pin("ESP32QJS_CAMERA_D6"),
        .pin_d5 = camera_profile_pin("ESP32QJS_CAMERA_D5"),
        .pin_d4 = camera_profile_pin("ESP32QJS_CAMERA_D4"),
        .pin_d3 = camera_profile_pin("ESP32QJS_CAMERA_D3"),
        .pin_d2 = camera_profile_pin("ESP32QJS_CAMERA_D2"),
        .pin_d1 = camera_profile_pin("ESP32QJS_CAMERA_D1"),
        .pin_d0 = camera_profile_pin("ESP32QJS_CAMERA_D0"),
        .pin_vsync = camera_profile_pin("ESP32QJS_CAMERA_VSYNC"),
        .pin_href = camera_profile_pin("ESP32QJS_CAMERA_HREF"),
        .pin_pclk = camera_profile_pin("ESP32QJS_CAMERA_PCLK"),
        .xclk_freq_hz = (int)esp32_mquickjs_profile_uint_or(
            "ESP32QJS_CAMERA_XCLK_FREQ_HZ", 20000000U),
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = 1,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
        .sccb_i2c_port = CAMERA_SCCB_I2C_PORT,
    };
    uint32_t timeout_ms = CAMERA_DEFAULT_TIMEOUT_MS;
    int frame_buffers = 1;
#ifdef CONFIG_CAMERA_PSRAM_DMA
    bool psram_dma = true;
#else
    bool psram_dma = false;
#endif
    esp_err_t err;
    JSValue result;
    int pins[] = {config.pin_xclk, config.pin_sccb_sda, config.pin_sccb_scl,
                  config.pin_d0, config.pin_d1, config.pin_d2, config.pin_d3,
                  config.pin_d4, config.pin_d5, config.pin_d6, config.pin_d7,
                  config.pin_vsync, config.pin_href, config.pin_pclk};
    size_t i;

    (void)this_val;
    if (argc > 1 || (argc == 1 && !JS_IsUndefined(argv[0]) &&
                     (JS_GetClassID(ctx, argv[0]) < 0 ||
                      JS_IsArray(ctx, argv[0])))) {
        return JS_ThrowTypeError(ctx, "camera.open(options?) expects an object");
    }
    if (s_camera.allocated && s_camera.release_pending) {
        err = camera_cleanup();
        if (err != ESP_OK) {
            return JS_ThrowInternalError(ctx,
                                         "camera.open() cleanup failed: %s",
                                         esp_err_to_name(err));
        }
    }
    if (s_camera.allocated) {
        return JS_ThrowInternalError(ctx, "camera.open() supports one Camera at a time");
    }
    if (argc == 1 && !JS_IsUndefined(argv[0])) {
        JSGCRef property_ref;
        JSGCRef pins_ref;
        JSValue *property = JS_PushGCRef(ctx, &property_ref);
        JSValue *pins_obj = JS_PushGCRef(ctx, &pins_ref);
        JSCStringBuf text_buf;
        const char *text;

        *property = JS_GetPropertyStr(ctx, argv[0], "sensor");
        if (JS_IsException(*property)) {
            goto parse_fail;
        }
        if (!JS_IsUndefined(*property)) {
            JS_ThrowTypeError(
                ctx,
                "camera.open() does not accept a sensor model; the driver probes it");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "pixelFormat");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             !camera_parse_pixel_format(ctx, *property, &config.pixel_format))) {
            JS_ThrowRangeError(ctx, "camera.open() received unsupported pixelFormat");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "frameSize");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             !camera_parse_frame_size(ctx, *property, &config.frame_size))) {
            JS_ThrowRangeError(ctx, "camera.open() received unsupported frameSize");
            goto parse_fail;
        }
        if (!camera_read_optional_i32(ctx, argv[0], "jpegQuality",
                                      &config.jpeg_quality) ||
            config.jpeg_quality < 0 || config.jpeg_quality > 63 ||
            !camera_read_optional_i32(ctx, argv[0], "frameBuffers",
                                      &frame_buffers) ||
            !camera_read_optional_bool(ctx, argv[0], "psramDma",
                                       &psram_dma) ||
            frame_buffers < 1 || frame_buffers > 2) {
            JS_ThrowRangeError(ctx, "camera.open() received invalid JPEG/DMA options");
            goto parse_fail;
        }
        if (!camera_read_optional_i32(ctx, argv[0], "xclkFreqHz",
                                      &config.xclk_freq_hz) ||
            config.xclk_freq_hz < 1000000 || config.xclk_freq_hz > 40000000) {
            JS_ThrowRangeError(ctx, "camera.open({ xclkFreqHz }) is out of range");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "timeoutMs");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             !camera_to_u32(ctx, *property, &timeout_ms))) {
            JS_ThrowTypeError(ctx, "camera.open({ timeoutMs }) expects an integer");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "grabMode");
        if (JS_IsException(*property)) {
            goto parse_fail;
        }
        if (!JS_IsUndefined(*property)) {
            text = JS_ToCString(ctx, *property, &text_buf);
            if (text == NULL ||
                (strcmp(text, "whenEmpty") != 0 && strcmp(text, "latest") != 0)) {
                JS_ThrowRangeError(ctx, "camera.open({ grabMode }) expects whenEmpty/latest");
                goto parse_fail;
            }
            config.grab_mode = strcmp(text, "latest") == 0
                                   ? CAMERA_GRAB_LATEST
                                   : CAMERA_GRAB_WHEN_EMPTY;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "bufferLocation");
        if (JS_IsException(*property)) {
            goto parse_fail;
        }
        if (!JS_IsUndefined(*property)) {
            text = JS_ToCString(ctx, *property, &text_buf);
            if (text == NULL ||
                (strcmp(text, "psram") != 0 && strcmp(text, "dram") != 0)) {
                JS_ThrowRangeError(ctx, "camera.open({ bufferLocation }) expects psram/dram");
                goto parse_fail;
            }
            config.fb_location = strcmp(text, "psram") == 0
                                     ? CAMERA_FB_IN_PSRAM
                                     : CAMERA_FB_IN_DRAM;
        }
        *pins_obj = JS_GetPropertyStr(ctx, argv[0], "pins");
        if (JS_IsException(*pins_obj)) {
            goto parse_fail;
        }
        if (!JS_IsUndefined(*pins_obj)) {
            if (JS_GetClassID(ctx, *pins_obj) < 0 ||
                JS_IsArray(ctx, *pins_obj) ||
                !camera_read_optional_i32(ctx, *pins_obj, "pwdn", &config.pin_pwdn) ||
                !camera_read_optional_i32(ctx, *pins_obj, "reset", &config.pin_reset) ||
                !camera_read_optional_i32(ctx, *pins_obj, "xclk", &config.pin_xclk) ||
                !camera_read_optional_i32(ctx, *pins_obj, "sccbSda", &config.pin_sccb_sda) ||
                !camera_read_optional_i32(ctx, *pins_obj, "sccbScl", &config.pin_sccb_scl) ||
                !camera_read_optional_i32(ctx, *pins_obj, "d0", &config.pin_d0) ||
                !camera_read_optional_i32(ctx, *pins_obj, "d1", &config.pin_d1) ||
                !camera_read_optional_i32(ctx, *pins_obj, "d2", &config.pin_d2) ||
                !camera_read_optional_i32(ctx, *pins_obj, "d3", &config.pin_d3) ||
                !camera_read_optional_i32(ctx, *pins_obj, "d4", &config.pin_d4) ||
                !camera_read_optional_i32(ctx, *pins_obj, "d5", &config.pin_d5) ||
                !camera_read_optional_i32(ctx, *pins_obj, "d6", &config.pin_d6) ||
                !camera_read_optional_i32(ctx, *pins_obj, "d7", &config.pin_d7) ||
                !camera_read_optional_i32(ctx, *pins_obj, "vsync", &config.pin_vsync) ||
                !camera_read_optional_i32(ctx, *pins_obj, "href", &config.pin_href) ||
                !camera_read_optional_i32(ctx, *pins_obj, "pclk", &config.pin_pclk)) {
                JS_ThrowTypeError(ctx, "camera.open({ pins }) received invalid pin values");
                goto parse_fail;
            }
        }
        JS_PopGCRef(ctx, &pins_ref);
        JS_PopGCRef(ctx, &property_ref);
        goto parsed;

parse_fail:
        JS_PopGCRef(ctx, &pins_ref);
        JS_PopGCRef(ctx, &property_ref);
        return JS_EXCEPTION;
    }

parsed:
    config.fb_count = (size_t)frame_buffers;
    if (!((config.fb_count == 1 && config.grab_mode == CAMERA_GRAB_WHEN_EMPTY) ||
          (config.fb_count == 2 && config.grab_mode == CAMERA_GRAB_LATEST))) {
        return JS_ThrowRangeError(
            ctx,
            "camera.open() requires 1/whenEmpty or explicitly 2/latest framebuffer mode");
    }
    if (config.fb_location == CAMERA_FB_IN_PSRAM &&
        !camera_psram_is_initialized()) {
        return JS_ThrowInternalError(ctx,
                                     "camera.open() requested PSRAM but PSRAM is unavailable");
    }
    pins[0] = config.pin_xclk;
    pins[1] = config.pin_sccb_sda;
    pins[2] = config.pin_sccb_scl;
    pins[3] = config.pin_d0;
    pins[4] = config.pin_d1;
    pins[5] = config.pin_d2;
    pins[6] = config.pin_d3;
    pins[7] = config.pin_d4;
    pins[8] = config.pin_d5;
    pins[9] = config.pin_d6;
    pins[10] = config.pin_d7;
    pins[11] = config.pin_vsync;
    pins[12] = config.pin_href;
    pins[13] = config.pin_pclk;
    for (i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        size_t other;

        if (pins[i] < 0 || pins[i] >= GPIO_NUM_MAX ||
            !GPIO_IS_VALID_GPIO(pins[i])) {
            return JS_ThrowTypeError(
                ctx,
                "camera.open() requires a complete explicit or hardware-profile pin map");
        }
        for (other = 0; other < i; ++other) {
            if (pins[i] == pins[other]) {
                return JS_ThrowRangeError(
                    ctx, "camera.open() requires distinct signal GPIOs");
            }
        }
    }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(config.pin_xclk) ||
        (config.pin_pwdn >= 0 && !GPIO_IS_VALID_OUTPUT_GPIO(config.pin_pwdn)) ||
        (config.pin_reset >= 0 && !GPIO_IS_VALID_OUTPUT_GPIO(config.pin_reset)) ||
        config.pin_pwdn < -1 || config.pin_reset < -1) {
        return JS_ThrowTypeError(ctx, "camera.open() received invalid output pins");
    }
    if (config.pin_pwdn >= 0 && config.pin_pwdn == config.pin_reset) {
        return JS_ThrowRangeError(
            ctx, "camera.open() requires distinct control GPIOs");
    }
    for (i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        if ((config.pin_pwdn >= 0 && config.pin_pwdn == pins[i]) ||
            (config.pin_reset >= 0 && config.pin_reset == pins[i])) {
            return JS_ThrowRangeError(
                ctx, "camera.open() requires distinct control GPIOs");
        }
    }
    if (!esp32_mquickjs_peripheral_lease_acquire(
            ESP32_MQUICKJS_PERIPHERAL_CAMERA, 0,
            ESP32_MQUICKJS_PERIPHERAL_OWNER_CAMERA,
            &s_camera.camera_lease) ||
        !esp32_mquickjs_peripheral_lease_acquire(
            ESP32_MQUICKJS_PERIPHERAL_I2C_PORT, CAMERA_SCCB_I2C_PORT,
            ESP32_MQUICKJS_PERIPHERAL_OWNER_CAMERA, &s_camera.i2c_lease) ||
        !esp32_mquickjs_peripheral_lease_acquire_any(
            ESP32_MQUICKJS_PERIPHERAL_LEDC_TIMER, SOC_LEDC_TIMER_NUM,
            ESP32_MQUICKJS_PERIPHERAL_OWNER_CAMERA,
            &s_camera.ledc_timer_lease) ||
        !esp32_mquickjs_peripheral_lease_acquire_any(
            ESP32_MQUICKJS_PERIPHERAL_LEDC_CHANNEL, SOC_LEDC_CHANNEL_NUM,
            ESP32_MQUICKJS_PERIPHERAL_OWNER_CAMERA,
            &s_camera.ledc_channel_lease)) {
        camera_release_leases();
        return JS_ThrowInternalError(ctx,
                                     "camera.open() could not reserve camera/SCCB/LEDC resources");
    }
    config.ledc_timer = (ledc_timer_t)s_camera.ledc_timer_lease.index;
    config.ledc_channel = (ledc_channel_t)s_camera.ledc_channel_lease.index;
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
        camera_release_leases();
        return JS_ThrowInternalError(ctx, "camera.open() failed: %s",
                                     esp_err_to_name(err));
    }
    if (esp_camera_get_psram_mode() != psram_dma) {
        err = esp_camera_set_psram_mode(psram_dma);
        if (err != ESP_OK) {
            (void)esp_camera_deinit();
            camera_release_leases();
            return JS_ThrowInternalError(
                ctx, "camera.open() could not configure PSRAM DMA: %s",
                esp_err_to_name(err));
        }
    }
    s_camera.allocated = true;
    s_camera.initialized = true;
    s_camera.sensor = esp_camera_sensor_get();
    if (!camera_sensor_supported(s_camera.sensor)) {
        s_camera.release_pending = true;
        err = camera_cleanup();
        if (err != ESP_OK) {
            return JS_ThrowInternalError(
                ctx,
                "camera.open() detected an unsupported sensor and cleanup failed: %s",
                esp_err_to_name(err));
        }
        return JS_ThrowInternalError(ctx,
                                     "camera.open() detected an unsupported sensor");
    }
    s_camera.generation = camera_take_generation();
    s_camera.default_timeout_ms = timeout_ms;
    s_camera.pixel_format = config.pixel_format;
    s_camera.frame_size = config.frame_size;
    s_camera.jpeg_quality = config.jpeg_quality;
    s_camera.frame_buffers = config.fb_count;
    s_camera.grab_mode = config.grab_mode;
    s_camera.buffer_location = config.fb_location;
    s_camera.psram_dma = esp_camera_get_psram_mode();
    result = camera_make_object(ctx);
    if (JS_IsException(result)) {
        s_camera.release_pending = true;
        (void)camera_cleanup();
    }
    return result;
}

#endif
