#include "esp32_mquickjs_wifi_raw_tx_validate.h"
#include <string.h>

static bool raw_tx_overlaps(const void *a, size_t a_length, const void *b, size_t b_length)
{
    uintptr_t first = (uintptr_t)a, second = (uintptr_t)b;
    if (a_length > UINTPTR_MAX - first || b_length > UINTPTR_MAX - second) return true;
    return first < second + b_length && second < first + a_length;
}

esp32_mquickjs_wifi_raw_tx_validation_t esp32_mquickjs_wifi_raw_tx_validate(
    const uint8_t *bytes, size_t length,
    const esp32_mquickjs_wifi_raw_tx_validation_policy_t *policy,
    esp32_mquickjs_wifi_raw_tx_validated_frame_t *output)
{
    if (bytes == NULL || policy == NULL || output == NULL)
        return ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT;
    if (length < ESP32_MQUICKJS_WIFI_RAW_TX_MIN_FRAME_BYTES || length > ESP32_MQUICKJS_WIFI_RAW_TX_MAX_FRAME_BYTES)
        return ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_LENGTH;
    if (raw_tx_overlaps(output, sizeof(*output), bytes, length) ||
        raw_tx_overlaps(output, sizeof(*output), policy, sizeof(*policy)))
        return ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT;
    if ((policy->interface != ESP32_MQUICKJS_WIFI_RAW_TX_STATION &&
         policy->interface != ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT) ||
        (policy->associated_path && !policy->connection_active))
        return ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT;
    uint16_t fc = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    if ((fc & 3U) != 0) return ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_HEADER;
    if ((fc & 0x4000U) != 0) return ESP32_MQUICKJS_WIFI_RAW_TX_PROTECTED_FRAME;
    uint8_t type = (fc >> 2) & 3U, subtype = (fc >> 4) & 15U;
    esp32_mquickjs_wifi_raw_tx_validated_frame_t frame = {0};
    if (type == 0) {
        switch (subtype) {
        case 8: frame.frame_type = ESP32_MQUICKJS_WIFI_RAW_TX_BEACON; break;
        case 4: frame.frame_type = ESP32_MQUICKJS_WIFI_RAW_TX_PROBE_REQUEST; break;
        case 5: frame.frame_type = ESP32_MQUICKJS_WIFI_RAW_TX_PROBE_RESPONSE; break;
        case 13: frame.frame_type = ESP32_MQUICKJS_WIFI_RAW_TX_ACTION; break;
        default: return ESP32_MQUICKJS_WIFI_RAW_TX_UNSUPPORTED_FRAME;
        }
    } else if (type == 2) {
        if ((subtype & 8U) != 0) return ESP32_MQUICKJS_WIFI_RAW_TX_QOS_FRAME;
        /* Plain Data only. Null/CF variants are not claimed by this allowlist. */
        if (subtype != 0) return ESP32_MQUICKJS_WIFI_RAW_TX_UNSUPPORTED_FRAME;
        frame.frame_type = ESP32_MQUICKJS_WIFI_RAW_TX_NON_QOS_DATA;
    } else return ESP32_MQUICKJS_WIFI_RAW_TX_UNSUPPORTED_FRAME;
    if (esp32_mquickjs_wifi_rx_parse_header(bytes, length, &frame.header) != ESP32_MQUICKJS_WIFI_RX_PARSED)
        return ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_HEADER;
    if (policy->connection_active && !policy->driver_sequence)
        return ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SEQUENCE_REQUIRED;
    if (policy->associated_path) {
        uint16_t expected = policy->interface == ESP32_MQUICKJS_WIFI_RAW_TX_STATION ? 0x0100U : 0x0200U;
        if (type == 2 && (fc & 0x0300U) != expected) return ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_DS;
        /* SDK rejects Retry, Power Management and More Data on connected paths. */
        if ((fc & (0x0800U | 0x1000U | 0x2000U)) != 0)
            return ESP32_MQUICKJS_WIFI_RAW_TX_CONNECTION_FLAGS;
    }
    frame.byte_length = (uint16_t)length;
    memcpy(output, &frame, sizeof(frame));
    return ESP32_MQUICKJS_WIFI_RAW_TX_VALID;
}
