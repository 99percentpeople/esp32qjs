#include "esp32_mquickjs_wifi_csi_wire.h"
#include <string.h>

bool esp32_mquickjs_wifi_csi_wire_snapshot(const esp32_mquickjs_wifi_csi_slot_t *slot,
    esp32_mquickjs_wifi_csi_wire_snapshot_t *output)
{
    if (slot == NULL || output == NULL || slot->payload == NULL || slot->length == 0 ||
        slot->length > UINT32_MAX || slot->session_generation == 0 || slot->metadata.radio_generation == 0)
        return false;
    uintptr_t start = (uintptr_t)slot, end = (uintptr_t)output;
    if (start > UINTPTR_MAX - sizeof(*slot) || end > UINTPTR_MAX - sizeof(*output) ||
        (start < end + sizeof(*output) && end < start + sizeof(*slot))) return false;
    esp32_mquickjs_wifi_csi_wire_snapshot_t result = {0};
    esp32_mquickjs_wifi_rx_wire_metadata_t *m = &result.metadata;
    const esp32_mquickjs_wifi_csi_metadata_t *source = &slot->metadata;
    esp32_mquickjs_wifi_rx_wire_metadata_init(m);
    result.frame.sequence = slot->sequence;
    result.frame.csi_length = (uint32_t)slot->length;
    m->timestamp_us = source->timestamp_us;
    m->rx_sequence = source->rx_sequence;
    m->session_generation = slot->session_generation;
    m->radio_generation = source->radio_generation;
    m->address_mask = source->address_mask;
    memcpy(m->addresses, source->addresses, sizeof(m->addresses));
    m->rx_flags = source->phy_flags;
    if (source->ampdu_count_available) m->ampdu_count = source->ampdu_count;
    m->rssi = source->rssi;
    m->primary = source->channel;
    m->secondary = (unsigned)source->secondary <= 2 ? (uint8_t)source->secondary : UINT8_MAX;
    m->phy = (unsigned)source->phy <= 6 ? (uint8_t)source->phy : UINT8_MAX;
#define SCALAR(available, value, field, flag) do { \
    if (source->available) { m->field = source->value; m->rx_flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_##flag; } \
} while (0)
    SCALAR(noise_floor_available, noise_floor, noise_floor, NOISE_AVAILABLE);
    SCALAR(antenna_available, antenna, antenna, ANTENNA_AVAILABLE);
    SCALAR(mcs_available, mcs, mcs, MCS_AVAILABLE);
    SCALAR(bandwidth_available, bandwidth_mhz, bandwidth_mhz, BANDWIDTH_AVAILABLE);
#undef SCALAR
    m->guard_interval_ns = source->guard_interval_ns;
    m->he_ltf_size = source->he_ltf_size;
    if (source->dcm_state > 2U) return false;
    if (source->dcm_state != 0U) {
        m->rx_flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_DCM_AVAILABLE;
        if (source->dcm_state == 2U) m->rx_flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_DCM;
    }
    if (source->stbc_available) {
        m->rx_flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_STBC_AVAILABLE;
        if (source->stbc) m->rx_flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_STBC;
    }
    if (source->channel_estimate_valid_available) {
        m->rx_flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_ESTIMATE_AVAILABLE;
        if (source->channel_estimate_valid) m->rx_flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_ESTIMATE_VALID;
    }
    m->first_word_invalid = source->first_word_invalid;
    m->csi_data_valid = true; /* Published nonempty, untruncated CSI bytes. Not an RF-quality claim. */
    m->csi_layout = &source->layout;
    const esp32_mquickjs_wifi_csi_packet_t *packet = slot->packet;
    if (packet != NULL && packet->length != 0) {
        const esp32_mquickjs_wifi_rx_header_t *h = &packet->header;
        if (packet->bytes == NULL || h->status != ESP32_MQUICKJS_WIFI_RX_PARSED ||
            h->header_length > packet->length || packet->length > packet->readable_length ||
            packet->readable_length > packet->driver_packet_length) return false;
        result.frame.packet_length = packet->length;
        result.frame.packet_readable_length = packet->readable_length;
        result.frame.driver_payload_length = packet->driver_payload_length;
        result.frame.captured_header_length = h->header_length;
        result.frame.flags = ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PRESENT |
            ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PARSED | ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_POINTER_VALID;
        if (packet->mode == ESP32_MQUICKJS_WIFI_CSI_PACKET_HEADER)
            result.frame.flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_HEADER_ONLY;
        if (packet->truncated) result.frame.flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_TRUNCATED;
        m->address_mask = h->address_mask;
        memcpy(m->addresses, h->addresses, sizeof(m->addresses));
        m->frame_control_available = h->frame_control_valid;
        m->duration_available = h->duration_valid;
        m->sequence_available = h->sequence_valid;
        m->qos_available = h->qos_valid;
        m->frame_control = h->frame_control;
        m->duration_id = h->duration_id;
        m->sequence_control = h->sequence_control;
        m->qos_control = h->qos_control;
        m->header_length = h->header_length;
        m->packet_type = (uint8_t)h->type + 1;
        m->capture_mode = (uint8_t)packet->mode;
        m->driver_payload_length_available = true;
        m->driver_packet_length_available = true;
        m->driver_packet_length = packet->driver_packet_length;
        /* FCS and protected-payload representation remain unknown. */
    }
    if (!esp32_mquickjs_wifi_rx_wire_metadata_valid(ESP32_MQUICKJS_WIFI_RX_WIRE_CSI, &result.frame, m))
        return false;
    *output = result;
    return true;
}
