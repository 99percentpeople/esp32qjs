#include "esp32_mquickjs_wifi_rx.h"

#include <string.h>

/* MAC header formats, not body minimum lengths. Reserved/extended layouts
 * deliberately remain unavailable instead of guessing a 24-byte header.
 * Layout/address reference: Linux v6.12 net/wireless/util.c ieee80211_hdrlen
 * and ieee80211_data_to_8023_exthdr. The parser is independently implemented. */
static const char *const s_management_names[16] = {
    "association-request", "association-response", "reassociation-request", "reassociation-response",
    "probe-request", "probe-response", "timing-advertisement", NULL,
    "beacon", "atim", "disassociation", "authentication", "deauthentication", "action", "action-no-ack", NULL,
};
static const char *const s_control_names[16] = {
    NULL, NULL, NULL, NULL, NULL, "vht-ndp-announcement", NULL, "control-wrapper",
    "block-ack-request", "block-ack", "ps-poll", "rts", "cts", "ack", "cf-end", "cf-end-cf-ack",
};
static const char *const s_data_names[16] = {
    "data", "data-cf-ack", "data-cf-poll", "data-cf-ack-cf-poll",
    "null", "cf-ack", "cf-poll", "cf-ack-cf-poll",
    "qos-data", "qos-data-cf-ack", "qos-data-cf-poll", "qos-data-cf-ack-cf-poll",
    "qos-null", NULL, "qos-cf-poll", "qos-cf-ack-cf-poll",
};

const char *esp32_mquickjs_wifi_rx_subtype_name(
    esp32_mquickjs_wifi_packet_type_t type, uint8_t subtype)
{
    if (subtype >= 16) return NULL;
    switch (type) {
    case ESP32_MQUICKJS_WIFI_PACKET_MANAGEMENT: return s_management_names[subtype];
    case ESP32_MQUICKJS_WIFI_PACKET_CONTROL: return s_control_names[subtype];
    case ESP32_MQUICKJS_WIFI_PACKET_DATA: return s_data_names[subtype];
    default: return NULL;
    }
}

static uint16_t wifi_rx_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t wifi_rx_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void wifi_rx_address(esp32_mquickjs_wifi_rx_header_t *header,
    esp32_mquickjs_wifi_rx_address_t role, const uint8_t *bytes)
{
    memcpy(header->addresses[role], bytes, 6);
    header->address_mask |= (uint8_t)(1U << role);
}

esp32_mquickjs_wifi_rx_parse_status_t esp32_mquickjs_wifi_rx_parse_header(
    const uint8_t *bytes, size_t readable_length, esp32_mquickjs_wifi_rx_header_t *output)
{
    if (output == NULL) return ESP32_MQUICKJS_WIFI_RX_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    output->type = ESP32_MQUICKJS_WIFI_PACKET_UNKNOWN;
    output->status = ESP32_MQUICKJS_WIFI_RX_SHORT_HEADER;
    if (bytes == NULL && readable_length != 0)
        return output->status = ESP32_MQUICKJS_WIFI_RX_INVALID_ARGUMENT;
    if (readable_length < 2) return output->status;
    uint16_t fc = wifi_rx_u16(bytes);
    output->frame_control = fc;
    output->frame_control_valid = true;
    output->version = fc & 3U;
    if (output->version != 0)
        return output->status = ESP32_MQUICKJS_WIFI_RX_UNSUPPORTED_VERSION;
    uint8_t type = (fc >> 2) & 3U;
    output->subtype = (fc >> 4) & 15U;
    if (type == 3) return output->status = ESP32_MQUICKJS_WIFI_RX_UNSUPPORTED_LAYOUT;
    output->type = (esp32_mquickjs_wifi_packet_type_t)type;
    if (readable_length >= 4) {
        output->duration_id = wifi_rx_u16(bytes + 2);
        output->duration_valid = true;
    }
    if (esp32_mquickjs_wifi_rx_subtype_name(output->type, output->subtype) == NULL)
        return output->status = ESP32_MQUICKJS_WIFI_RX_UNSUPPORTED_LAYOUT;

    bool to_ds = (fc & 0x0100U) != 0, from_ds = (fc & 0x0200U) != 0;
    bool order = (fc & 0x8000U) != 0;
    uint8_t qos_offset = 0, ht_offset = 0;
    if (output->type != ESP32_MQUICKJS_WIFI_PACKET_DATA && (to_ds || from_ds))
        return output->status = ESP32_MQUICKJS_WIFI_RX_INVALID_FLAGS;
    if (output->type == ESP32_MQUICKJS_WIFI_PACKET_CONTROL) {
        output->header_length = (output->subtype == 12 || output->subtype == 13) ? 10 : 16;
        if (output->subtype == 7) ht_offset = 12; /* RA + carried FC + HT control; no TA. */
    } else {
        output->header_length = 24;
        if (output->type == ESP32_MQUICKJS_WIFI_PACKET_DATA) {
            if (to_ds && from_ds) output->header_length += 6;
            if ((output->subtype & 8U) != 0) {
                qos_offset = output->header_length;
                output->header_length += 2;
                if (order) { ht_offset = output->header_length; output->header_length += 4; }
            }
        } else if (order) { ht_offset = 24; output->header_length += 4; }
    }
    if (readable_length < output->header_length) return output->status;

    /* Every following read is within the complete, already checked MAC header.
     * No typed/unaligned loads. We never inspect body/aggregate/mesh/FCS bytes. */
    wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_RECEIVER, bytes + 4);
    if (output->type == ESP32_MQUICKJS_WIFI_PACKET_CONTROL) {
        wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_DESTINATION, bytes + 4);
        if (output->subtype != 7 && output->header_length == 16) {
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_TRANSMITTER, bytes + 10);
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_SOURCE, bytes + 10);
            if (output->subtype == 10) wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_BSSID, bytes + 4);
            else if (output->subtype == 14 || output->subtype == 15)
                wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_BSSID, bytes + 10);
        }
    } else {
        wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_TRANSMITTER, bytes + 10);
        output->sequence_control = wifi_rx_u16(bytes + 22);
        output->sequence_valid = true;
        if (!to_ds && !from_ds) {
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_DESTINATION, bytes + 4);
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_SOURCE, bytes + 10);
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_BSSID, bytes + 16);
        } else if (to_ds && from_ds) {
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_DESTINATION, bytes + 16);
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_SOURCE, bytes + 24);
        } else if (to_ds) {
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_DESTINATION, bytes + 16);
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_SOURCE, bytes + 10);
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_BSSID, bytes + 4);
        } else {
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_DESTINATION, bytes + 4);
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_SOURCE, bytes + 16);
            wifi_rx_address(output, ESP32_MQUICKJS_WIFI_RX_BSSID, bytes + 10);
        }
    }
    if (qos_offset != 0) {
        output->qos_control = wifi_rx_u16(bytes + qos_offset);
        output->qos_valid = true;
    }
    if (ht_offset != 0) {
        output->ht_control = wifi_rx_u32(bytes + ht_offset);
        output->ht_valid = true;
    }
    return output->status = ESP32_MQUICKJS_WIFI_RX_PARSED;
}
