#pragma once
#include "esp32_mquickjs_wifi_csi_resources.h"
#include "esp32_mquickjs_wifi_rx_wire_metadata.h"

typedef struct {
    esp32_mquickjs_wifi_rx_wire_frame_t frame;
    esp32_mquickjs_wifi_rx_wire_metadata_t metadata;
} esp32_mquickjs_wifi_csi_wire_snapshot_t;
/* Caller must hold the exact slot's event/public/retained owner before entry and
 * until encoding finishes. The metadata layout pointer borrows that slot, so a
 * snapshot alone is not a retained owner. No heap/SDK/JS or payload reads here.
 * Output must not overlap the slot; it commits only after complete preflight. */
bool esp32_mquickjs_wifi_csi_wire_snapshot(const esp32_mquickjs_wifi_csi_slot_t *slot,
    esp32_mquickjs_wifi_csi_wire_snapshot_t *output);
