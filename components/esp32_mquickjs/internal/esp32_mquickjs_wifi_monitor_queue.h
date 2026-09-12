#pragma once
#include "esp32_mquickjs_wifi_monitor_resources.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include "esp32_mquickjs_event_queue.h"

typedef struct {
    esp32_mquickjs_wifi_monitor_resources_t *resources;
    esp32_mquickjs_event_queue_t *queue;
    _Atomic bool context_owned; /* No reuse while an old queue still owns it. */
    /* make_frame receives an already PUBLIC root. Success transfers it to the
     * returned JS Frame, which must also retain its own context reference until
     * finalization; on exception this bridge closes it. The converter must
     * detach any partially created object's native ownership before failing. */
    JSValue (*make_frame)(JSContext *ctx, const esp32_mquickjs_wifi_monitor_event_t *event, void *opaque);
    bool (*retain_context)(void *opaque);
    void (*release_context)(void *opaque);
    /* Called on runtime/reaper threads too: stop/request cleanup only, no JS,
     * waits or destruction of storage still retained by queue/native refs. */
    void (*request_close)(void *opaque);
    void *opaque;
} esp32_mquickjs_wifi_monitor_queue_t;

/* Zero initialize bridge before first construction. Callbacks must remain immutable/live until the queue's final
 * release_context, plus all external producer/session references. The caller
 * owns an independent context reference during construction and teardown.
 * Init is serialized before enabling resources or admitting a Radio sink. */
JSValue esp32_mquickjs_wifi_monitor_queue_new(JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime, esp32_mquickjs_wifi_monitor_queue_t *bridge, uint32_t capacity);
/* Native publication boundary; use as resources_init's publish hook. */
bool esp32_mquickjs_wifi_monitor_queue_publish(
    const esp32_mquickjs_wifi_monitor_event_t *event, void *opaque);
/* Only after producer admission is stopped AND Radio callbacks drained.
 * Close/discard and release the producer's native queue reference. JS/Future/
 * reaper references still retain the context until actual queue destruction.
 * Caller separately disposes the rooted JS queue value on the JS thread. */
void esp32_mquickjs_wifi_monitor_queue_detach(esp32_mquickjs_wifi_monitor_queue_t *bridge);
#endif
