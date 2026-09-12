#include "esp32_mquickjs_wifi_rx_wire_metadata.h"
#include <string.h>

_Static_assert(ESP32_MQUICKJS_WIFI_CSI_MAX_SEGMENTS == 3 &&
    ESP32_MQUICKJS_WIFI_CSI_MAX_SUBCARRIER_RANGES == 2 &&
    ESP32_MQUICKJS_WIFI_CSI_MAX_NULL_SUBCARRIERS == 3,
    "CSI layout dimensions must match the sole-v1 wire slots");
_Static_assert(ESP32_MQUICKJS_WIFI_RX_WIRE_SEGMENT_BASE +
    ESP32_MQUICKJS_WIFI_CSI_MAX_SEGMENTS * ESP32_MQUICKJS_WIFI_RX_WIRE_SEGMENT_BYTES + 8 ==
    ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES, "RX metadata reserved tail must fit");

void esp32_mquickjs_wifi_rx_wire_metadata_init(esp32_mquickjs_wifi_rx_wire_metadata_t *m)
{
    if (m == NULL) return;
    memset(m, 0, sizeof(*m));
    m->rx_sequence = UINT32_MAX;
    m->secondary = m->antenna = m->phy = m->mcs = UINT8_MAX;
    m->legacy_rate = m->signal_mode = m->ampdu_count = m->rx_state = UINT8_MAX;
    m->timestamp_accuracy = 2;
}

static bool rx_meta_value_available(uint32_t flags, uint32_t value, uint32_t available)
{
    return (flags & value) == 0 || (flags & available) != 0;
}

static bool rx_meta_signal_valid(const esp32_mquickjs_wifi_rx_wire_metadata_t *m)
{
    uint32_t f = m->rx_flags;
    if (m->address_mask > 31 || (m->secondary > 2 && m->secondary != UINT8_MAX) ||
        (m->phy > 6 && m->phy != UINT8_MAX) || m->timestamp_accuracy > 2 ||
        (f & ~((1U << 20) - 1U)) != 0) return false;
#define PAIR(value, available) \
    if (!rx_meta_value_available(f, ESP32_MQUICKJS_WIFI_RX_WIRE_##value, \
        ESP32_MQUICKJS_WIFI_RX_WIRE_##available)) return false
    PAIR(STBC, STBC_AVAILABLE);
    PAIR(SMOOTHING, SMOOTHING_AVAILABLE);
    PAIR(SOUNDING, SOUNDING_AVAILABLE);
    PAIR(AGGREGATION, AGGREGATION_AVAILABLE);
    PAIR(LDPC, FEC_AVAILABLE);
    PAIR(SGI, SGI_AVAILABLE);
    PAIR(DCM, DCM_AVAILABLE);
    PAIR(ESTIMATE_VALID, ESTIMATE_AVAILABLE);
#undef PAIR
    if (((f & ESP32_MQUICKJS_WIFI_RX_WIRE_ANTENNA_AVAILABLE) && m->antenna == UINT8_MAX) ||
        ((f & ESP32_MQUICKJS_WIFI_RX_WIRE_MCS_AVAILABLE) && m->mcs == UINT8_MAX)) return false;
    if ((f & ESP32_MQUICKJS_WIFI_RX_WIRE_BANDWIDTH_AVAILABLE) &&
        m->bandwidth_mhz != 20 && m->bandwidth_mhz != 40 &&
        m->bandwidth_mhz != 80 && m->bandwidth_mhz != 160) return false;
    if (m->guard_interval_ns != 0 && m->guard_interval_ns != 400 && m->guard_interval_ns != 800 &&
        m->guard_interval_ns != 1600 && m->guard_interval_ns != 3200) return false;
    if (m->he_ltf_size != 0 && m->he_ltf_size != 1 && m->he_ltf_size != 2 && m->he_ltf_size != 4) return false;
    bool he = m->phy >= 3 && m->phy <= 6;
    if ((m->he_ltf_size != 0 || (f & ESP32_MQUICKJS_WIFI_RX_WIRE_DCM_AVAILABLE)) && !he) return false;
    if (m->guard_interval_ns != 0) {
        if (he ? m->guard_interval_ns == 400 :
            (m->phy != 1 && m->phy != 2) || m->guard_interval_ns > 800) return false;
        if ((f & ESP32_MQUICKJS_WIFI_RX_WIRE_SGI_AVAILABLE) &&
            m->guard_interval_ns != ((f & ESP32_MQUICKJS_WIFI_RX_WIRE_SGI) ? 400 : 800)) return false;
    }
    return true;
}

static bool rx_meta_layout_valid(const esp32_mquickjs_wifi_csi_layout_t *l, uint32_t length)
{
    if (l == NULL) return length == 0;
    if (length == 0 || l->byte_length != length || (unsigned)l->schema > 2 ||
        (unsigned)l->sample_encoding > 3 || l->segment_count == 0 || l->segment_count > 3 ||
        l->trailing_padding_bytes > 3) return false;
    const uint8_t bits[] = {0, 8, 12, 12}, bytes_per_pair[] = {0, 2, 4, 3};
    unsigned encoding = (unsigned)l->sample_encoding;
    if (l->sample_bits != bits[encoding] ||
        (l->known && (encoding == 0 || l->schema == 0))) return false;
    uint64_t offset = 0, pairs = 0;
    for (unsigned i = 0; i < l->segment_count; ++i) {
        const esp32_mquickjs_wifi_csi_segment_t *s = &l->segments[i];
        if ((unsigned)s->type > 7 || s->subcarrier_range_count > 2 || s->null_subcarrier_count > 3 ||
            s->offset_bytes != offset || s->length_bytes == 0) return false;
        offset += s->length_bytes;
        pairs += s->iq_pair_count;
        if (offset > length || pairs > UINT32_MAX) return false;
        if (encoding != 0 && (uint64_t)s->iq_pair_count * bytes_per_pair[encoding] != s->length_bytes)
            return false;
        uint32_t range_pairs = 0;
        for (unsigned j = 0; j < s->subcarrier_range_count; ++j) {
            const esp32_mquickjs_wifi_csi_subcarrier_range_t *r = &s->subcarrier_ranges[j];
            if (r->start > r->end) return false;
            range_pairs += (uint32_t)((int32_t)r->end - r->start + 1);
        }
        if (s->subcarrier_range_count == 2 &&
            s->subcarrier_ranges[0].start <= s->subcarrier_ranges[1].end &&
            s->subcarrier_ranges[1].start <= s->subcarrier_ranges[0].end) return false;
        if (l->known && (s->type == ESP32_MQUICKJS_WIFI_CSI_SEGMENT_UNKNOWN ||
            range_pairs != s->iq_pair_count)) return false;
        for (unsigned j = 0; j < s->null_subcarrier_count; ++j)
            for (unsigned k = 0; k < j; ++k)
                if (s->null_subcarriers[j] == s->null_subcarriers[k]) return false;
    }
    return offset + l->trailing_padding_bytes == length && pairs == l->iq_pair_count;
}

static bool rx_meta_packet_valid(const esp32_mquickjs_wifi_rx_wire_frame_t *f,
    const esp32_mquickjs_wifi_rx_wire_metadata_t *m)
{
    if (m->packet_type > 4 || m->fcs_state > 3 || m->capture_mode > 2) return false;
    if ((!m->driver_payload_length_available && f->driver_payload_length != 0) ||
        (!m->driver_packet_length_available && m->driver_packet_length != 0)) return false;
    uint32_t prefix = m->header_length < f->packet_length ? m->header_length : f->packet_length;
    if (f->captured_header_length != prefix) return false;
    bool pointer = (f->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_POINTER_VALID) != 0;
    bool parsed = (f->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PARSED) != 0;
    bool header_only = (f->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_HEADER_ONLY) != 0;
    if ((m->frame_control_available && (!pointer || f->packet_readable_length < 2)) ||
        (m->duration_available && (!m->frame_control_available || f->packet_readable_length < 4)) ||
        (m->sequence_available && f->packet_readable_length < 24) ||
        (m->qos_available && f->packet_readable_length < 26) ||
        ((m->sequence_available || m->qos_available || m->header_length != 0) && !m->frame_control_available))
        return false;
    if (parsed && (!m->frame_control_available || !m->duration_available || m->header_length == 0 ||
        m->header_length > f->packet_readable_length || (m->frame_control & 3U) != 0 ||
        m->packet_type == 0 || m->packet_type > 3 ||
        m->packet_type != ((m->frame_control >> 2) & 3U) + 1U)) return false;
    if (f->packet_length != 0 && (m->capture_mode == 0 || header_only != (m->capture_mode == 1))) return false;
    if (header_only && m->header_length != f->packet_length) return false;
    if (m->driver_packet_length_available && f->packet_readable_length > m->driver_packet_length) return false;
    if (f->packet_length != 0 && !header_only) {
        uint32_t complete_length = m->driver_packet_length_available
            ? m->driver_packet_length : f->packet_readable_length;
        bool truncated = (f->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_TRUNCATED) != 0;
        if (truncated != (f->packet_length < complete_length)) return false;
    }
    return true;
}

static void rx_meta_u16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void rx_meta_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

bool esp32_mquickjs_wifi_rx_wire_metadata_valid(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *f,
    const esp32_mquickjs_wifi_rx_wire_metadata_t *m)
{
    esp32_mquickjs_wifi_rx_wire_layout_t envelope;
    if (m == NULL || !esp32_mquickjs_wifi_rx_wire_layout(kind, f, 1, &envelope) || !rx_meta_signal_valid(m) ||
        !rx_meta_layout_valid(m->csi_layout, f->csi_length) || !rx_meta_packet_valid(f, m)) return false;
    if ((m->csi_layout == NULL && (m->csi_data_valid || m->first_word_invalid)) ||
        (kind == ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR && (m->rx_flags &
            (ESP32_MQUICKJS_WIFI_RX_WIRE_ESTIMATE_AVAILABLE | ESP32_MQUICKJS_WIFI_RX_WIRE_ESTIMATE_VALID))))
        return false;
    return true;
}

bool esp32_mquickjs_wifi_rx_wire_write_metadata(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *f,
    const esp32_mquickjs_wifi_rx_wire_metadata_t *m, uint8_t *output, size_t output_capacity)
{
    if (output == NULL || output_capacity < ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES ||
        (uintptr_t)output > UINTPTR_MAX - ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES ||
        !esp32_mquickjs_wifi_rx_wire_metadata_valid(kind, f, m)) return false;
    uint8_t record[ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES] = {0};
    rx_meta_u32(record, f->sequence);
    rx_meta_u32(record + 4, (uint32_t)m->timestamp_us);
    rx_meta_u32(record + 8, (uint32_t)(m->timestamp_us >> 32));
    rx_meta_u32(record + 12, m->rx_sequence);
    rx_meta_u32(record + 16, m->session_generation);
    rx_meta_u32(record + 20, m->radio_generation);
    for (unsigned i = 0; i < 5; ++i)
        if (m->address_mask & (1U << i)) memcpy(record + 24 + i * 6, m->addresses[i], 6);
    rx_meta_u16(record + 54, m->address_mask);
    record[56] = (uint8_t)m->rssi;
    uint32_t r = m->rx_flags;
    record[57] = (r & ESP32_MQUICKJS_WIFI_RX_WIRE_NOISE_AVAILABLE) ? (uint8_t)m->noise_floor : 0;
    record[58] = m->primary;
    record[59] = m->secondary;
    record[60] = (r & ESP32_MQUICKJS_WIFI_RX_WIRE_ANTENNA_AVAILABLE) ? m->antenna : UINT8_MAX;
    record[61] = m->phy;
    record[62] = (r & ESP32_MQUICKJS_WIFI_RX_WIRE_BANDWIDTH_AVAILABLE) ? m->bandwidth_mhz : 0;
    record[63] = (r & ESP32_MQUICKJS_WIFI_RX_WIRE_MCS_AVAILABLE) ? m->mcs : UINT8_MAX;
    rx_meta_u32(record + 64, r);
    const esp32_mquickjs_wifi_csi_layout_t *l = m->csi_layout;
    if (l != NULL) {
        uint32_t flags = (m->first_word_invalid ? 1U : 0U) | (m->csi_data_valid ? 2U : 0U) | (l->known ? 4U : 0U);
        rx_meta_u32(record + 68, flags);
        record[72] = (uint8_t)l->sample_encoding;
        record[73] = l->sample_bits;
        record[74] = (uint8_t)l->schema;
        rx_meta_u32(record + 76, f->csi_length);
        rx_meta_u32(record + 80, l->iq_pair_count);
        rx_meta_u16(record + 84, l->trailing_padding_bytes);
        record[86] = l->segment_count;
        for (unsigned i = 0; i < l->segment_count; ++i) {
            const esp32_mquickjs_wifi_csi_segment_t *s = &l->segments[i];
            uint8_t *p = record + ESP32_MQUICKJS_WIFI_RX_WIRE_SEGMENT_BASE +
                i * ESP32_MQUICKJS_WIFI_RX_WIRE_SEGMENT_BYTES;
            p[0] = (uint8_t)s->type;
            p[1] = s->subcarrier_range_count;
            p[2] = s->null_subcarrier_count;
            rx_meta_u32(p + 4, s->offset_bytes);
            rx_meta_u32(p + 8, s->length_bytes);
            rx_meta_u32(p + 12, s->iq_pair_count);
            for (unsigned j = 0; j < s->subcarrier_range_count; ++j) {
                rx_meta_u16(p + 16 + j * 4, (uint16_t)s->subcarrier_ranges[j].start);
                rx_meta_u16(p + 18 + j * 4, (uint16_t)s->subcarrier_ranges[j].end);
            }
            for (unsigned j = 0; j < s->null_subcarrier_count; ++j)
                rx_meta_u16(p + 24 + j * 2, (uint16_t)s->null_subcarriers[j]);
        }
    }
    record[87] = m->timestamp_accuracy;
    uint32_t packet_flags = 0;
    if (f->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PRESENT) packet_flags |= 1U << 0;
    if (f->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_HEADER_ONLY) packet_flags |= 1U << 1;
    if (f->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_TRUNCATED) packet_flags |= 1U << 2;
    if (f->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_POINTER_VALID) packet_flags |= 1U << 3;
    if (f->flags & ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PARSED) packet_flags |= 1U << 4;
    record[97] = UINT8_MAX;
    if (m->frame_control_available) {
        rx_meta_u16(record + 88, m->frame_control);
        record[97] = (m->frame_control >> 4) & 15U;
        packet_flags |= (1U << 15) | ((uint32_t)(m->frame_control >> 8) << 5);
    }
    if (m->duration_available) { rx_meta_u16(record + 90, m->duration_id); packet_flags |= 1U << 16; }
    if (m->sequence_available) { rx_meta_u16(record + 92, m->sequence_control); packet_flags |= 1U << 13; }
    if (m->qos_available) { rx_meta_u16(record + 94, m->qos_control); packet_flags |= 1U << 14; }
    if (m->driver_payload_length_available) packet_flags |= 1U << 17;
    if (m->driver_packet_length_available) packet_flags |= 1U << 18;
    record[96] = m->packet_type;
    record[98] = m->fcs_state;
    record[99] = m->capture_mode;
    rx_meta_u16(record + 100, m->header_length);
    rx_meta_u32(record + 104, f->driver_payload_length);
    rx_meta_u32(record + 108, m->driver_packet_length);
    rx_meta_u32(record + 112, f->packet_length);
    rx_meta_u32(record + 116, packet_flags);
    record[120] = m->legacy_rate;
    record[121] = m->signal_mode;
    record[122] = m->ampdu_count;
    record[123] = m->rx_state;
    rx_meta_u16(record + 124, m->guard_interval_ns);
    record[126] = m->he_ltf_size;
    memcpy(output, record, sizeof(record));
    return true;
}
