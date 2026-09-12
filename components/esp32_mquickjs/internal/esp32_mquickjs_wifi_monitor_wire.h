#pragma once
#include "esp32_mquickjs_wifi_monitor_resources.h"
#include "esp32_mquickjs_wifi_rx_wire_metadata.h"
#include "esp32_mquickjs_wifi_rx_vht_signal.h"
#include "esp32_mquickjs_wifi_rx_he_signal.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
/* Normalized sole-v1 PHY (0..6 or 255); shared by public info and wire. */
uint8_t esp32_mquickjs_wifi_monitor_phy_format(const esp32_mquickjs_wifi_rx_driver_metadata_t *driver);
/* Compact optional PHY facts used by both JS and wire, never retained per frame. */
typedef struct {
    uint32_t flags; /* Existing normalized RX availability/value flags only. */
    uint16_t guard_interval_ns;
    uint8_t bandwidth_mhz, mcs, ampdu_count, he_ltf_size;
} esp32_mquickjs_wifi_monitor_phy_snapshot_t;
esp32_mquickjs_wifi_monitor_phy_snapshot_t esp32_mquickjs_wifi_monitor_phy_snapshot(
    const esp32_mquickjs_wifi_rx_driver_metadata_t *driver);
typedef struct {
    esp32_mquickjs_wifi_rx_wire_frame_t frame;
    esp32_mquickjs_wifi_rx_wire_metadata_t metadata;
} esp32_mquickjs_wifi_monitor_wire_snapshot_t;
/* Copies only stable, pointer-free native facts. No payload retain/read, SDK call
 * or JS allocation. Output commits after common wire validation succeeds. The
 * caller separately retains the payload/Session for any subsequent stream. */
bool esp32_mquickjs_wifi_monitor_wire_snapshot(const esp32_mquickjs_wifi_monitor_info_t *info,
    uint32_t sequence, uint32_t session_generation, uint32_t radio_generation,
    esp32_mquickjs_wifi_monitor_wire_snapshot_t *output);
#endif
