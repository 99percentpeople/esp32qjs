#include "esp32_mquickjs_wifi_csi_rx_span.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

void IRAM_ATTR esp32_mquickjs_wifi_csi_rx_span_begin(esp32_mquickjs_wifi_csi_rx_span_t *span,
    const void *base, size_t metadata_bytes, size_t copied_prefix)
{
    if (span == NULL) return;
    *span = (esp32_mquickjs_wifi_csi_rx_span_t){0};
    uintptr_t start = (uintptr_t)base;
    if (base == NULL || metadata_bytes == 0 || copied_prefix > metadata_bytes ||
        start > UINTPTR_MAX - metadata_bytes) return;
    span->base = start;
    span->packet = start + metadata_bytes;
    span->end = span->packet;
    span->valid = true;
}

void IRAM_ATTR esp32_mquickjs_wifi_csi_rx_span_append(esp32_mquickjs_wifi_csi_rx_span_t *span,
    const void *destination, size_t copied_bytes)
{
    if (span == NULL || !span->valid) return;
    uintptr_t start = (uintptr_t)destination;
    if (destination == NULL || start < span->base || start > UINTPTR_MAX - copied_bytes) {
        span->valid = false;
        return;
    }
    uintptr_t end = start + copied_bytes;
    /* Fixed metadata tail stores before packet are immaterial. No gap or
     * overlap in the packet itself is inferred to have been initialized. */
    if (end <= span->packet) return;
    if ((span->end == span->packet && start <= span->packet) || start == span->end) {
        span->end = end;
    } else {
        span->valid = false;
    }
}

bool IRAM_ATTR esp32_mquickjs_wifi_csi_rx_span_finish(const esp32_mquickjs_wifi_csi_rx_span_t *span,
    const void *base, const void *packet, size_t *readable_bytes)
{
    if (readable_bytes == NULL) return false;
    *readable_bytes = 0;
    if (span == NULL || !span->valid || span->base != (uintptr_t)base ||
        span->packet != (uintptr_t)packet || span->end <= span->packet) return false;
    *readable_bytes = (size_t)(span->end - span->packet);
    return true;
}
