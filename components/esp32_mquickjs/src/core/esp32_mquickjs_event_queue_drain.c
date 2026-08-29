#include "esp32_mquickjs_event_queue_drain.h"

size_t esp32_mquickjs_event_queue_drain(
    void *source,
    void *scratch,
    esp32_mquickjs_event_queue_drain_receive_fn receive,
    esp32_mquickjs_event_queue_drain_drop_fn drop,
    void *opaque)
{
    size_t drained = 0;

    if (source == NULL || scratch == NULL || receive == NULL) {
        return 0;
    }
    while (receive(source, scratch)) {
        if (drop != NULL) {
            drop(scratch, opaque);
        }
        drained++;
    }
    return drained;
}

bool esp32_mquickjs_event_queue_enqueue(
    void *destination,
    const void *event,
    void *scratch,
    bool drop_oldest,
    esp32_mquickjs_event_queue_enqueue_send_fn send,
    esp32_mquickjs_event_queue_drain_receive_fn receive,
    esp32_mquickjs_event_queue_drain_drop_fn drop,
    void *opaque,
    uint32_t *dropped_count)
{
    uint32_t dropped = 0;

    if (destination == NULL || event == NULL || send == NULL) {
        return false;
    }
    if (send(destination, event)) {
        return true;
    }
    if (drop_oldest && scratch != NULL && receive != NULL &&
        receive(destination, scratch)) {
        dropped++;
        if (drop != NULL) {
            drop(scratch, opaque);
        }
        if (send(destination, event)) {
            if (dropped_count != NULL) {
                *dropped_count += dropped;
            }
            return true;
        }
    }
    dropped++;
    if (dropped_count != NULL) {
        *dropped_count += dropped;
    }
    return false;
}

bool esp32_mquickjs_event_queue_enqueue_from_isr(
    void *destination,
    const void *event,
    esp32_mquickjs_event_queue_enqueue_send_from_isr_fn send,
    int *task_woken,
    uint32_t *dropped_count)
{
    int woken = 0;

    if (destination == NULL || event == NULL || send == NULL) {
        return false;
    }
    if (send(destination, event, &woken)) {
        if (task_woken != NULL && woken != 0) {
            *task_woken = 1;
        }
        return true;
    }
    if (dropped_count != NULL) {
        (*dropped_count)++;
    }
    if (task_woken != NULL && woken != 0) {
        *task_woken = 1;
    }
    return false;
}
