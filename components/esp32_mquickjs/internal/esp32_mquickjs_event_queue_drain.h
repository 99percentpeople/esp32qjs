#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef bool (*esp32_mquickjs_event_queue_drain_receive_fn)(
    void *source,
    void *event);
typedef void (*esp32_mquickjs_event_queue_drain_drop_fn)(
    void *event,
    void *opaque);

size_t esp32_mquickjs_event_queue_drain(
    void *source,
    void *scratch,
    esp32_mquickjs_event_queue_drain_receive_fn receive,
    esp32_mquickjs_event_queue_drain_drop_fn drop,
    void *opaque);
