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
