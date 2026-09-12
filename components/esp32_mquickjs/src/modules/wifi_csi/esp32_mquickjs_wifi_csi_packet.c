#include "esp32_mquickjs_wifi_csi_packet.h"

bool esp32_mquickjs_wifi_csi_packet_options_valid(
    const esp32_mquickjs_wifi_csi_packet_options_t *options)
{
    if (options == NULL || (unsigned)options->mode > ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL)
        return false;
    if (options->mode == ESP32_MQUICKJS_WIFI_CSI_PACKET_NONE)
        return !options->required && !options->require_complete;
    return options->snap_length >= ESP32_MQUICKJS_WIFI_CSI_MAX_HEADER_BYTES &&
        options->snap_length <= ESP32_MQUICKJS_WIFI_CSI_MAX_PACKET_BYTES &&
        (!options->require_complete || options->mode == ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL);
}

uint32_t esp32_mquickjs_wifi_csi_packet_capacity(
    const esp32_mquickjs_wifi_csi_packet_options_t *options)
{
    if (options->mode == ESP32_MQUICKJS_WIFI_CSI_PACKET_NONE) return 0;
    return options->mode == ESP32_MQUICKJS_WIFI_CSI_PACKET_HEADER
        ? ESP32_MQUICKJS_WIFI_CSI_MAX_HEADER_BYTES : options->snap_length;
}

esp32_mquickjs_wifi_csi_packet_result_t esp32_mquickjs_wifi_csi_packet_prepare(
    const esp32_mquickjs_wifi_csi_packet_options_t *options,
    const esp32_mquickjs_wifi_csi_packet_input_t *input,
    esp32_mquickjs_wifi_csi_packet_t *output)
{
    if (output == NULL) return ESP32_MQUICKJS_WIFI_CSI_PACKET_UNAVAILABLE;
    *output = (esp32_mquickjs_wifi_csi_packet_t){0};
    if (!esp32_mquickjs_wifi_csi_packet_options_valid(options) ||
        input == NULL ||
        input->bytes == NULL || input->copied_length == 0 ||
        input->driver_packet_length == 0 ||
        (uintptr_t)input->bytes > UINTPTR_MAX - input->copied_length)
        return ESP32_MQUICKJS_WIFI_CSI_PACKET_UNAVAILABLE;
    /* Clip completed native copies to the reported packet boundary: a copy
     * may also contain padding or CSI. sig_len alone never expands this span. */
    uint32_t readable = input->copied_length < input->driver_packet_length
        ? (uint32_t)input->copied_length : input->driver_packet_length;
    esp32_mquickjs_wifi_rx_header_t header;
    if (esp32_mquickjs_wifi_rx_parse_header(input->bytes, readable, &header) !=
            ESP32_MQUICKJS_WIFI_RX_PARSED ||
        header.header_length > ESP32_MQUICKJS_WIFI_CSI_MAX_HEADER_BYTES)
        return ESP32_MQUICKJS_WIFI_CSI_PACKET_MALFORMED;
    uint32_t length = options->mode == ESP32_MQUICKJS_WIFI_CSI_PACKET_NONE ? 0 :
        options->mode == ESP32_MQUICKJS_WIFI_CSI_PACKET_HEADER
        ? header.header_length : readable < options->snap_length ? readable : options->snap_length;
    bool truncated = options->mode == ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL &&
        length < input->driver_packet_length;
    if (options->require_complete && truncated)
        return ESP32_MQUICKJS_WIFI_CSI_PACKET_INCOMPLETE;
    *output = (esp32_mquickjs_wifi_csi_packet_t){
        .header = header, .bytes = (uint8_t *)input->bytes, .length = length,
        .readable_length = readable, .driver_packet_length = input->driver_packet_length,
        .driver_payload_length = input->driver_payload_length,
        .mode = options->mode, .truncated = truncated,
    };
    return ESP32_MQUICKJS_WIFI_CSI_PACKET_OK;
}
