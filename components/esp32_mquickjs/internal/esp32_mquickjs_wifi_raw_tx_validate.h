#pragma once
#include "esp32_mquickjs_wifi_raw_tx_limits.h"
#include "esp32_mquickjs_wifi_rx.h"


typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_STATION,
    ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT,
} esp32_mquickjs_wifi_raw_tx_interface_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_BEACON,
    ESP32_MQUICKJS_WIFI_RAW_TX_PROBE_REQUEST,
    ESP32_MQUICKJS_WIFI_RAW_TX_PROBE_RESPONSE,
    ESP32_MQUICKJS_WIFI_RAW_TX_ACTION,
    ESP32_MQUICKJS_WIFI_RAW_TX_NON_QOS_DATA,
    ESP32_MQUICKJS_WIFI_RAW_TX_ASSOCIATION_REQUEST,
    ESP32_MQUICKJS_WIFI_RAW_TX_ASSOCIATION_RESPONSE,
    ESP32_MQUICKJS_WIFI_RAW_TX_REASSOCIATION_REQUEST,
    ESP32_MQUICKJS_WIFI_RAW_TX_REASSOCIATION_RESPONSE,
    ESP32_MQUICKJS_WIFI_RAW_TX_TIMING_ADVERTISEMENT,
    ESP32_MQUICKJS_WIFI_RAW_TX_ATIM,
    ESP32_MQUICKJS_WIFI_RAW_TX_DISASSOCIATION,
    ESP32_MQUICKJS_WIFI_RAW_TX_AUTHENTICATION,
    ESP32_MQUICKJS_WIFI_RAW_TX_DEAUTHENTICATION,
    ESP32_MQUICKJS_WIFI_RAW_TX_ACTION_NO_ACK,
} esp32_mquickjs_wifi_raw_tx_frame_type_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_VALID,
    ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT,
    ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_LENGTH,
    ESP32_MQUICKJS_WIFI_RAW_TX_UNSUPPORTED_FRAME,
    ESP32_MQUICKJS_WIFI_RAW_TX_PROTECTED_FRAME,
    ESP32_MQUICKJS_WIFI_RAW_TX_QOS_FRAME,
    ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_HEADER,
    ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SEQUENCE_REQUIRED,
    ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_DS,
    ESP32_MQUICKJS_WIFI_RAW_TX_CONNECTION_FLAGS,
} esp32_mquickjs_wifi_raw_tx_validation_t;

typedef struct {
    esp32_mquickjs_wifi_raw_tx_interface_t interface;
    bool driver_sequence;
    /* Native Radio/association facts, never caller-provided JS assertions.
     * connection_active: an established Wi-Fi connection requires driver sequence.
     * associated_path: this frame's Addr1/Addr2 identify an actual connected
     * STA->AP or AP->STA path on the selected interface, per the pinned SDK guide.
     * It must be false without connection_active. Recheck at driver admission. */
    bool connection_active;
    bool associated_path;
} esp32_mquickjs_wifi_raw_tx_validation_policy_t;
typedef struct {
    esp32_mquickjs_wifi_raw_tx_frame_type_t frame_type;
    uint16_t byte_length;
    esp32_mquickjs_wifi_rx_header_t header;
} esp32_mquickjs_wifi_raw_tx_validated_frame_t;

/* Allocation/SDK/JS-free preflight for this build's SDK admission policy. Input must be
 * a proven, stable pure MAC-frame span without caller-added FCS. It does not
 * recognize arbitrary container bytes or infer FCS from a payload's last bytes.
 * Rejects output overlap; on failure output and input are unchanged. This checks
 * MAC layout and driver constraints, not arbitrary management/action bodies. */
esp32_mquickjs_wifi_raw_tx_validation_t esp32_mquickjs_wifi_raw_tx_validate(
    const uint8_t *bytes, size_t length,
    const esp32_mquickjs_wifi_raw_tx_validation_policy_t *policy,
    esp32_mquickjs_wifi_raw_tx_validated_frame_t *output);
