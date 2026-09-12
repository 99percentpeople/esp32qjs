#include "esp32_mquickjs_wifi_rx_wire.h"
#include <string.h>

_Static_assert(ESP32_MQUICKJS_WIFI_RX_WIRE_MAX_FRAMES <=
    (UINT32_MAX - ESP32_MQUICKJS_WIFI_RX_WIRE_HEADER_BYTES) /
    (ESP32_MQUICKJS_WIFI_RX_WIRE_CSI_DIRECTORY_BYTES + ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES),
    "RX wire control dimensions must fit uint32");

static bool rx_wire_add(uint32_t left, uint32_t right, uint32_t *output)
{
    if (left > UINT32_MAX - right) return false;
    *output = left + right;
    return true;
}
static bool rx_wire_align(uint32_t offset, uint32_t *output)
{
    uint32_t padding = (4U - (offset & 3U)) & 3U;
    return rx_wire_add(offset, padding, output);
}
static bool rx_wire_frame_valid(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *frame)
{
    if ((frame->flags & ~ESP32_MQUICKJS_WIFI_RX_WIRE_RECORD_FLAGS) != 0 ||
        frame->captured_header_length > frame->packet_length ||
        frame->packet_length > frame->packet_readable_length ||
        (kind == ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR && frame->csi_length != 0)) return false;
    bool present = (frame->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PRESENT) != 0;
    bool header_only = (frame->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_HEADER_ONLY) != 0;
    bool truncated = (frame->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_TRUNCATED) != 0;
    if (present != (frame->packet_length != 0)) return false;
    bool pointer_valid = (frame->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_POINTER_VALID) != 0;
    bool parsed = (frame->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PARSED) != 0;
    if (parsed && !pointer_valid) return false;
    /* Reported lengths and a proven source span can exist without copying a
     * packet section. They are not lengths used for output writes or offsets. */
    if (!present) return !header_only && !truncated && frame->captured_header_length == 0;
    if (!pointer_valid) return false;
    if (header_only) {
        /* A deliberate complete header-only capture is not snap truncation. */
        if (truncated || frame->captured_header_length != frame->packet_length ||
            (frame->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PARSED) == 0) return false;
    } else if (frame->packet_length < frame->packet_readable_length && !truncated) return false;
    return true;
}

static bool rx_wire_advance(const esp32_mquickjs_wifi_rx_wire_frame_t *frame,
    uint32_t *cursor, esp32_mquickjs_wifi_rx_wire_offsets_t *offsets)
{
    offsets->csi_offset = frame->csi_length != 0 ? *cursor : 0;
    if (!rx_wire_add(*cursor, frame->csi_length, cursor) || !rx_wire_align(*cursor, cursor)) return false;
    offsets->packet_offset = frame->packet_length != 0 ? *cursor : 0;
    if (!rx_wire_add(*cursor, frame->packet_length, cursor) || !rx_wire_align(*cursor, cursor)) return false;
    offsets->end_offset = *cursor;
    return true;
}

bool esp32_mquickjs_wifi_rx_wire_layout(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *frames, uint32_t frame_count,
    esp32_mquickjs_wifi_rx_wire_layout_t *output)
{
    if (frames == NULL || output == NULL || frame_count == 0 ||
        frame_count > ESP32_MQUICKJS_WIFI_RX_WIRE_MAX_FRAMES ||
        (kind != ESP32_MQUICKJS_WIFI_RX_WIRE_CSI && kind != ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR)) return false;
    esp32_mquickjs_wifi_rx_wire_layout_t result = {.frame_count = frame_count};
    result.directory_bytes = kind == ESP32_MQUICKJS_WIFI_RX_WIRE_CSI ?
        ESP32_MQUICKJS_WIFI_RX_WIRE_CSI_DIRECTORY_BYTES : ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR_DIRECTORY_BYTES;
    /* frame_count has the common pool bound, so both products fit uint32. */
    result.metadata_base = ESP32_MQUICKJS_WIFI_RX_WIRE_HEADER_BYTES + frame_count * result.directory_bytes;
    result.control_bytes = result.metadata_base + frame_count * ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES;
    uint32_t cursor = result.control_bytes;
    for (uint32_t i = 0; i < frame_count; ++i) {
        esp32_mquickjs_wifi_rx_wire_offsets_t offsets;
        if (!rx_wire_frame_valid(kind, &frames[i]) || !rx_wire_advance(&frames[i], &cursor, &offsets)) return false;
    }
    result.total_bytes = cursor;
    *output = result;
    return true;
}

bool esp32_mquickjs_wifi_rx_wire_offsets(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *frames, uint32_t frame_count, uint32_t index,
    esp32_mquickjs_wifi_rx_wire_offsets_t *output)
{
    esp32_mquickjs_wifi_rx_wire_layout_t layout;
    if (output == NULL || index >= frame_count ||
        !esp32_mquickjs_wifi_rx_wire_layout(kind, frames, frame_count, &layout)) return false;
    uint32_t cursor = layout.control_bytes;
    esp32_mquickjs_wifi_rx_wire_offsets_t result;
    for (uint32_t i = 0; i <= index; ++i)
        if (!rx_wire_advance(&frames[i], &cursor, &result)) return false;
    result.metadata_offset = layout.metadata_base + index * ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES;
    *output = result;
    return true;
}

static void rx_wire_u16(uint8_t *output, uint16_t value)
{ output[0] = (uint8_t)value; output[1] = (uint8_t)(value >> 8); }
static void rx_wire_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value; output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16); output[3] = (uint8_t)(value >> 24);
}

bool esp32_mquickjs_wifi_rx_wire_write_control(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *frames, uint32_t frame_count,
    uint8_t *output, size_t output_capacity)
{
    esp32_mquickjs_wifi_rx_wire_layout_t layout;
    if (output == NULL || !esp32_mquickjs_wifi_rx_wire_layout(kind, frames, frame_count, &layout) ||
        output_capacity < layout.control_bytes) return false;
    uintptr_t input_start = (uintptr_t)frames, output_start = (uintptr_t)output;
    size_t input_bytes = (size_t)frame_count * sizeof(*frames);
    if (input_start > UINTPTR_MAX - input_bytes || output_start > UINTPTR_MAX - layout.control_bytes ||
        (input_start < output_start + layout.control_bytes && output_start < input_start + input_bytes)) return false;
    memset(output, 0, layout.control_bytes);
    memcpy(output, kind == ESP32_MQUICKJS_WIFI_RX_WIRE_CSI ? "E32QCSI1" : "E32QMON1", 8);
    rx_wire_u16(output + 8, 1);
    rx_wire_u16(output + 10, ESP32_MQUICKJS_WIFI_RX_WIRE_HEADER_BYTES);
    rx_wire_u32(output + 12, frame_count);
    rx_wire_u16(output + 16, (uint16_t)layout.directory_bytes);
    rx_wire_u16(output + 18, ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES);
    rx_wire_u32(output + 20, ESP32_MQUICKJS_WIFI_RX_WIRE_CANONICAL);
    rx_wire_u32(output + 24, layout.total_bytes);
    uint32_t cursor = layout.control_bytes;
    for (uint32_t i = 0; i < frame_count; ++i) {
        uint8_t *directory = output + ESP32_MQUICKJS_WIFI_RX_WIRE_HEADER_BYTES + i * layout.directory_bytes;
        esp32_mquickjs_wifi_rx_wire_offsets_t offsets;
        /* Complete immutable-input preflight has already proved these sums. */
        (void)rx_wire_advance(&frames[i], &cursor, &offsets);
        rx_wire_u32(directory, layout.metadata_base + i * ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES);
        if (kind == ESP32_MQUICKJS_WIFI_RX_WIRE_CSI) {
            rx_wire_u32(directory + 4, offsets.csi_offset);
            rx_wire_u32(directory + 8, frames[i].csi_length);
            rx_wire_u32(directory + 12, offsets.packet_offset);
            rx_wire_u32(directory + 16, frames[i].packet_length);
            rx_wire_u16(directory + 20, frames[i].captured_header_length);
            rx_wire_u16(directory + 22, frames[i].flags);
            rx_wire_u32(directory + 24, frames[i].driver_payload_length);
            rx_wire_u32(directory + 28, frames[i].packet_readable_length);
            rx_wire_u32(directory + 32, frames[i].sequence);
        } else {
            rx_wire_u32(directory + 4, offsets.packet_offset);
            rx_wire_u32(directory + 8, frames[i].packet_length);
            rx_wire_u32(directory + 12, frames[i].driver_payload_length);
            rx_wire_u32(directory + 16, frames[i].packet_readable_length);
            rx_wire_u16(directory + 20, frames[i].captured_header_length);
            rx_wire_u16(directory + 22, frames[i].flags);
        }
    }
    return true;
}
