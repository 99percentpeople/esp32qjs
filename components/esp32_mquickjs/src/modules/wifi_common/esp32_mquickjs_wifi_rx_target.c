#include "esp32_mquickjs_wifi_rx_target.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include <stdint.h>
#include <string.h>

_Static_assert(offsetof(wifi_promiscuous_pkt_t, rx_ctrl) == 0, "RX metadata must be the callback prefix");
_Static_assert(offsetof(wifi_promiscuous_pkt_t, payload) == sizeof(wifi_pkt_rx_ctrl_t), "Unexpected RX payload offset");

#if CONFIG_SOC_WIFI_HE_SUPPORT
/* Fixed SDK esp_wifi_htsig_t packs HT-SIG into he_siga1: MCS[6:0],
 * CBW[7], smoothing[24], not-sounding[25], aggregation[27], STBC[29:28],
 * FEC[30], SGI[31]. Decode the copied word, never cast a callback pointer.
 * This format contains no AMPDU count or antenna observation. */
static void wifi_rx_decode_he_ht(esp32_mquickjs_wifi_rx_driver_metadata_t *metadata)
{
    if (metadata->raw_format != RX_BB_FORMAT_HT) return;
    uint32_t signal = metadata->signal_word1;
    uint8_t mcs = (uint8_t)(signal & 0x7fU);
    if (mcs > 76U) return; /* Reserved HT MCS must not become a known PHY. */
    metadata->ht_fields_available = true;
    metadata->mcs = mcs;
    metadata->bandwidth_mhz = (signal & (1UL << 7U)) != 0U ? 40U : 20U;
    metadata->smoothing = (signal & (1UL << 24U)) != 0U;
    metadata->sounding = (signal & (1UL << 25U)) == 0U;
    metadata->aggregation = (signal & (1UL << 27U)) != 0U;
    metadata->stbc = ((signal >> 28U) & 3U) != 0U;
    metadata->ldpc = (signal & (1UL << 30U)) != 0U;
    metadata->short_gi = (signal & (1UL << 31U)) != 0U;
}
#endif

esp32_mquickjs_wifi_rx_span_status_t esp32_mquickjs_wifi_rx_target_view(
    const void *buffer, wifi_promiscuous_pkt_type_t type,
    esp32_mquickjs_wifi_rx_target_view_t *output)
{
    if (output == NULL) return ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    output->metadata.type = ESP32_MQUICKJS_WIFI_PACKET_UNKNOWN;
    (void)esp32_mquickjs_wifi_rx_parse_header(NULL, 0, &output->header);
    /* Unknown callback types do not grant even a readable metadata prefix. */
    switch (type) {
    case WIFI_PKT_MGMT: output->metadata.type = ESP32_MQUICKJS_WIFI_PACKET_MANAGEMENT; break;
    case WIFI_PKT_CTRL: output->metadata.type = ESP32_MQUICKJS_WIFI_PACKET_CONTROL; break;
    case WIFI_PKT_DATA: output->metadata.type = ESP32_MQUICKJS_WIFI_PACKET_DATA; break;
    case WIFI_PKT_MISC: output->metadata.type = ESP32_MQUICKJS_WIFI_PACKET_MISC; break;
    default: return output->status = ESP32_MQUICKJS_WIFI_RX_SPAN_UNKNOWN_TYPE;
    }
    uintptr_t base = (uintptr_t)buffer;
    if (buffer == NULL || base > UINTPTR_MAX - sizeof(wifi_pkt_rx_ctrl_t))
        return output->status = ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_ARGUMENT;
    /* The SDK guarantees the prefix for known types. Copy to aligned local
     * storage; no bitfield casts or reads past metadata-only buffers. */
    wifi_pkt_rx_ctrl_t rx;
    memcpy(&rx, buffer, sizeof(rx));
    esp32_mquickjs_wifi_rx_driver_metadata_t *metadata = &output->metadata;
    metadata->available = true;
    metadata->rssi = rx.rssi;
    metadata->noise_floor = rx.noise_floor;
    metadata->primary = rx.channel;
    metadata->timestamp_us = rx.timestamp;
    metadata->driver_length = rx.sig_len;
    metadata->rx_state = rx.rx_state;
    metadata->raw_rate = rx.rate;
    uint32_t readable_length;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    metadata->he_layout = true;
    metadata->dump_length_available = true;
    metadata->dump_length = rx.dump_len;
    metadata->secondary_raw = rx.second;
    metadata->raw_format = rx.cur_bb_format;
    metadata->signal_word1 = rx.he_siga1;
    metadata->signal_word2 = rx.he_siga2;
    metadata->rx_end_state = rx.rxend_state;
    wifi_rx_decode_he_ht(metadata);
    /* The public callback payload is bounded by sig_len. The pinned C5 native
     * callback also reports dump_len=sig_len+4; preserve that raw observation
     * without reading its extra tail or inferring FCS inclusion. Other shapes
     * still require a nonzero dump_len no larger than sig_len below. */
    readable_length = rx.dump_len;
    if (metadata->driver_length != 0U && readable_length == (uint32_t)metadata->driver_length + 4U)
        readable_length = metadata->driver_length;
#else
    metadata->secondary_raw = rx.secondary_channel;
    metadata->raw_format = rx.sig_mode;
    metadata->antenna_available = true;
    metadata->antenna = rx.ant;
    if (rx.sig_mode == 1) {
        metadata->ht_fields_available = true;
        metadata->mcs = rx.mcs;
        metadata->bandwidth_mhz = rx.cwb ? 40 : 20;
        metadata->ampdu_count = rx.ampdu_cnt;
        metadata->stbc = rx.stbc != 0;
        metadata->short_gi = rx.sgi;
        metadata->ldpc = rx.fec_coding;
        metadata->aggregation = rx.aggregation;
        metadata->smoothing = rx.smoothing;
        metadata->sounding = !rx.not_sounding;
    }
    /* Native legacy callback payload length is documented by sig_len. This
     * is used only after callback-kind/prefix validation, never for MISC. */
    readable_length = rx.sig_len;
#endif
    if (type == WIFI_PKT_MISC)
        return output->status = ESP32_MQUICKJS_WIFI_RX_SPAN_METADATA_ONLY;
    if (readable_length == 0 || readable_length > metadata->driver_length ||
        base + sizeof(rx) > UINTPTR_MAX - readable_length)
        return output->status = ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_LENGTH;
    output->bytes = (const uint8_t *)buffer + sizeof(rx);
    output->readable_length = (uint16_t)readable_length;
    /* CRC-error frames are a documented sniffer input. Keep rx_state and do
     * not label their FCS valid; structural parsing and validOnly are distinct. */
    (void)esp32_mquickjs_wifi_rx_parse_header(output->bytes, readable_length, &output->header);
    output->header_type_matches = output->header.frame_control_valid && output->header.version == 0 &&
        output->header.type == metadata->type;
    return output->status = ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET;
}
#endif
