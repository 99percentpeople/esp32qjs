#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*esp32_mquickjs_event_queue_drain_receive_fn)(
    void *source,
    void *event);
typedef void (*esp32_mquickjs_event_queue_drain_drop_fn)(
    void *event,
    void *opaque);
typedef bool (*esp32_mquickjs_event_queue_enqueue_send_fn)(
    void *destination,
    const void *event);
typedef bool (*esp32_mquickjs_event_queue_enqueue_send_from_isr_fn)(
    void *destination,
    const void *event,
    int *task_woken);

size_t esp32_mquickjs_event_queue_drain(
    void *source,
    void *scratch,
    esp32_mquickjs_event_queue_drain_receive_fn receive,
    esp32_mquickjs_event_queue_drain_drop_fn drop,
    void *opaque);

/**
 * Perform one task-context enqueue attempt without allocating.
 *
 * On DROP_OLDEST, an evicted owned event is released exactly once before the
 * retry. dropped_count includes both the evicted event and, if the retry also
 * fails, the rejected incoming event.
 */
bool esp32_mquickjs_event_queue_enqueue(
    void *destination,
    const void *event,
    void *scratch,
    bool drop_oldest,
    esp32_mquickjs_event_queue_enqueue_send_fn send,
    esp32_mquickjs_event_queue_drain_receive_fn receive,
    esp32_mquickjs_event_queue_drain_drop_fn drop,
    void *opaque,
    uint32_t *dropped_count);

/**
 * Perform one ISR-safe enqueue attempt. ISR producers always use DROP_NEW:
 * no queue element is removed and ownership of a rejected event stays with
 * the producer. The helper records exactly one drop for a full queue.
 */
bool esp32_mquickjs_event_queue_enqueue_from_isr(
    void *destination,
    const void *event,
    esp32_mquickjs_event_queue_enqueue_send_from_isr_fn send,
    int *task_woken,
    uint32_t *dropped_count);
