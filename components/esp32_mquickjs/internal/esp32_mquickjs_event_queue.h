#pragma once

#include "esp32_mquickjs_types.h"
#include "esp32_mquickjs_future.h"

typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;

typedef enum {
    ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
    ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,
} esp32_mquickjs_event_queue_overflow_t;

typedef JSValue (*esp32_mquickjs_event_queue_to_js_fn)(JSContext *ctx,
                                                        const void *event,
                                                        void *opaque);
typedef void (*esp32_mquickjs_event_queue_drop_fn)(void *event, void *opaque);
typedef void (*esp32_mquickjs_event_queue_close_fn)(void *opaque);

typedef struct {
    uint32_t open;
    uint32_t dropped;
} esp32_mquickjs_event_queue_status_t;

typedef struct {
    bool open;
    uint32_t queued;
    uint32_t capacity;
    uint32_t dropped;
    bool receiver_pending;
} esp32_mquickjs_event_queue_stats_t;

bool esp32_mquickjs_init_event_queue_runtime(JSContext *ctx,
                                              esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_event_queue_runtime(esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_get_event_queue_status(
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_event_queue_status_t *status);
JSValue esp32_mquickjs_event_queue_new(JSContext *ctx,
                                       esp32_mquickjs_runtime_t *runtime,
                                       size_t event_size,
                                       uint32_t capacity,
                                       esp32_mquickjs_event_queue_overflow_t overflow,
                                       esp32_mquickjs_event_queue_to_js_fn to_js,
                                       esp32_mquickjs_event_queue_drop_fn drop,
                                       esp32_mquickjs_event_queue_close_fn close,
                                       void *opaque);
bool esp32_mquickjs_event_queue_send(esp32_mquickjs_event_queue_t *queue,
                                     const void *event);
bool esp32_mquickjs_event_queue_send_from_isr(esp32_mquickjs_event_queue_t *queue,
                                              const void *event,
                                              int *task_woken);
bool esp32_mquickjs_event_queue_close(esp32_mquickjs_event_queue_t *queue);
bool esp32_mquickjs_event_queue_dispose(JSContext *ctx, JSValue value);
size_t esp32_mquickjs_event_queue_discard_all(
    esp32_mquickjs_event_queue_t *queue);
bool esp32_mquickjs_event_queue_is_closed(const esp32_mquickjs_event_queue_t *queue);
bool esp32_mquickjs_event_queue_get_stats(
    esp32_mquickjs_event_queue_t *queue,
    esp32_mquickjs_event_queue_stats_t *stats);
uint32_t esp32_mquickjs_event_queue_dropped(esp32_mquickjs_event_queue_t *queue);
bool esp32_mquickjs_event_queue_register_receive_alias(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    JSValue function);
esp32_mquickjs_event_queue_t *esp32_mquickjs_event_queue_from_value(
    JSContext *ctx,
    JSValue value);

JSValue js_event_queue_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_event_queue_finalizer(JSContext *ctx, void *opaque);
JSValue js_event_queue_receive(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_event_queue_stats(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_event_queue_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
