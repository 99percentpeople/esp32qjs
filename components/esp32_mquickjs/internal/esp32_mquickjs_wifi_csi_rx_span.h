#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A receipt for bytes already written by the reviewed native RX copy sites.
 * Addresses are only identities here: these functions never read a packet. */
typedef struct {
    uintptr_t base;
    uintptr_t packet;
    uintptr_t end;
    bool valid;
} esp32_mquickjs_wifi_csi_rx_span_t;

void esp32_mquickjs_wifi_csi_rx_span_begin(esp32_mquickjs_wifi_csi_rx_span_t *span,
    const void *base, size_t metadata_bytes, size_t copied_prefix);
void esp32_mquickjs_wifi_csi_rx_span_append(esp32_mquickjs_wifi_csi_rx_span_t *span,
    const void *destination, size_t copied_bytes);
bool esp32_mquickjs_wifi_csi_rx_span_finish(const esp32_mquickjs_wifi_csi_rx_span_t *span,
    const void *base, const void *packet, size_t *readable_bytes);
