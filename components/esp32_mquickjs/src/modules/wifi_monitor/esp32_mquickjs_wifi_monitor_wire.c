#include "esp32_mquickjs_wifi_monitor_wire.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include <string.h>
#if CONFIG_SOC_WIFI_HE_SUPPORT
#include "esp_wifi_he_types.h"
#endif

uint8_t esp32_mquickjs_wifi_monitor_phy_format(const esp32_mquickjs_wifi_rx_driver_metadata_t *d)
{
    if (d == NULL) return UINT8_MAX;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    if (d->he_layout) {
        switch (d->raw_format) {
        case RX_BB_FORMAT_11B: case RX_BB_FORMAT_11G: return 0;
        case RX_BB_FORMAT_HT: return 1;
        case RX_BB_FORMAT_VHT: case RX_BB_FORMAT_VHT_MU: return 2;
        case RX_BB_FORMAT_HE_SU: return 3;
        case RX_BB_FORMAT_HE_MU: return 4;
        case RX_BB_FORMAT_HE_ERSU: return 5;
        case RX_BB_FORMAT_HE_TB: return 6;
        default: return UINT8_MAX;
        }
    }
#endif
    return d->he_layout ? UINT8_MAX : d->raw_format == 0 ? 0 : d->raw_format == 1 ? 1 : UINT8_MAX;
}

esp32_mquickjs_wifi_monitor_phy_snapshot_t esp32_mquickjs_wifi_monitor_phy_snapshot(
    const esp32_mquickjs_wifi_rx_driver_metadata_t *d)
{
    esp32_mquickjs_wifi_monitor_phy_snapshot_t result = {.mcs = UINT8_MAX, .ampdu_count = UINT8_MAX};
    if (d == NULL || !d->available) return result;
#define FLAG(name) ESP32_MQUICKJS_WIFI_RX_WIRE_##name
#define VALUE(value, name, available) do { \
    result.flags |= FLAG(available); \
    if (value) result.flags |= FLAG(name); \
} while (0)
#if CONFIG_SOC_WIFI_HE_SUPPORT
    if (d->he_layout && (d->raw_format == RX_BB_FORMAT_VHT || d->raw_format == RX_BB_FORMAT_VHT_MU)) {
        esp32_mquickjs_wifi_rx_vht_signal_t vht = esp32_mquickjs_wifi_rx_decode_vht_signal(
            d->signal_word1, d->raw_format == RX_BB_FORMAT_VHT_MU);
        result.flags = FLAG(BANDWIDTH_AVAILABLE);
        result.bandwidth_mhz = vht.bandwidth_mhz;
        result.guard_interval_ns = vht.short_gi ? 400U : 800U;
        if (vht.mcs_available) { result.flags |= FLAG(MCS_AVAILABLE); result.mcs = vht.mcs; }
        VALUE(vht.stbc, STBC, STBC_AVAILABLE);
        VALUE(vht.short_gi, SGI, SGI_AVAILABLE);
        if (vht.fec_available) { VALUE(vht.ldpc, LDPC, FEC_AVAILABLE); }
        return result;
    }
    if (d->he_layout && (d->raw_format == RX_BB_FORMAT_HE_SU || d->raw_format == RX_BB_FORMAT_HE_MU ||
        d->raw_format == RX_BB_FORMAT_HE_ERSU || d->raw_format == RX_BB_FORMAT_HE_TB)) {
        esp32_mquickjs_wifi_rx_he_kind_t kind = d->raw_format == RX_BB_FORMAT_HE_SU ? ESP32_MQUICKJS_WIFI_RX_HE_SU :
            d->raw_format == RX_BB_FORMAT_HE_MU ? ESP32_MQUICKJS_WIFI_RX_HE_MU :
            d->raw_format == RX_BB_FORMAT_HE_ERSU ? ESP32_MQUICKJS_WIFI_RX_HE_ER_SU : ESP32_MQUICKJS_WIFI_RX_HE_TB;
        esp32_mquickjs_wifi_rx_he_signal_t he = esp32_mquickjs_wifi_rx_decode_he_signal(kind, d->signal_word1, d->signal_word2);
        result.guard_interval_ns = he.guard_interval_ns;
        result.he_ltf_size = he.he_ltf_size;
        if (he.dcm_available) { VALUE(he.dcm, DCM, DCM_AVAILABLE); }
        if (he.bandwidth_available) { result.flags |= FLAG(BANDWIDTH_AVAILABLE); result.bandwidth_mhz = he.bandwidth_mhz; }
        if (he.mcs_available) { result.flags |= FLAG(MCS_AVAILABLE); result.mcs = he.mcs; }
        if (he.stbc_available) { VALUE(he.stbc, STBC, STBC_AVAILABLE); }
        if (he.fec_available) { VALUE(he.ldpc, LDPC, FEC_AVAILABLE); }
        return result;
    }
#endif
    if (d->ht_fields_available && esp32_mquickjs_wifi_monitor_phy_format(d) == 1U) {
        result.flags = FLAG(MCS_AVAILABLE) | FLAG(BANDWIDTH_AVAILABLE);
        result.guard_interval_ns = d->short_gi ? 400U : 800U;
        result.mcs = d->mcs;
        result.bandwidth_mhz = d->bandwidth_mhz;
        if (!d->he_layout) result.ampdu_count = d->ampdu_count;
        VALUE(d->stbc, STBC, STBC_AVAILABLE);
        VALUE(d->short_gi, SGI, SGI_AVAILABLE);
        VALUE(d->ldpc, LDPC, FEC_AVAILABLE);
        VALUE(d->aggregation, AGGREGATION, AGGREGATION_AVAILABLE);
        VALUE(d->smoothing, SMOOTHING, SMOOTHING_AVAILABLE);
        VALUE(d->sounding, SOUNDING, SOUNDING_AVAILABLE);
    }
#undef VALUE
#undef FLAG
    return result;
}

bool esp32_mquickjs_wifi_monitor_wire_snapshot(const esp32_mquickjs_wifi_monitor_info_t *info,
    uint32_t sequence, uint32_t session_generation, uint32_t radio_generation,
    esp32_mquickjs_wifi_monitor_wire_snapshot_t *output)
{
    if (info == NULL || output == NULL || sequence == 0 || session_generation == 0 || radio_generation == 0 ||
        !info->driver.available || (unsigned)info->driver.type > ESP32_MQUICKJS_WIFI_PACKET_UNKNOWN) return false;
    if (info->metadata_only ? (info->readable_length != 0 || info->captured_length != 0 || info->truncated) :
        (info->readable_length == 0 || info->captured_length == 0 ||
         info->readable_length > info->driver.driver_length)) return false;
    esp32_mquickjs_wifi_monitor_wire_snapshot_t result = {0};
    esp32_mquickjs_wifi_rx_wire_frame_t *f = &result.frame;
    esp32_mquickjs_wifi_rx_wire_metadata_t *m = &result.metadata;
    const esp32_mquickjs_wifi_rx_driver_metadata_t *d = &info->driver;
    const esp32_mquickjs_wifi_rx_header_t *h = &info->header;
    esp32_mquickjs_wifi_rx_wire_metadata_init(m);
    f->sequence = sequence;
    m->timestamp_us = info->callback_time_us;
    m->session_generation = session_generation;
    m->radio_generation = radio_generation;
    m->rssi = d->rssi;
    m->noise_floor = d->noise_floor;
    m->rx_flags = ESP32_MQUICKJS_WIFI_RX_WIRE_NOISE_AVAILABLE;
    m->primary = d->primary;
    m->secondary = d->secondary_raw <= 2 ? d->secondary_raw : UINT8_MAX;
    m->phy = esp32_mquickjs_wifi_monitor_phy_format(d);
    if (d->antenna_available) {
        m->rx_flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_ANTENNA_AVAILABLE;
        m->antenna = d->antenna;
    }
    esp32_mquickjs_wifi_monitor_phy_snapshot_t phy = esp32_mquickjs_wifi_monitor_phy_snapshot(d);
    m->rx_flags |= phy.flags;
    m->mcs = phy.mcs;
    m->bandwidth_mhz = phy.bandwidth_mhz;
    m->ampdu_count = phy.ampdu_count;
    m->guard_interval_ns = phy.guard_interval_ns;
    m->he_ltf_size = phy.he_ltf_size;
    m->rx_state = d->rx_state;
    m->driver_packet_length = d->driver_length;
    m->driver_packet_length_available = true;
    /* Monitor SDK supplies sig_len, not an independent payload_len. Keep the
     * latter unavailable; a parsed remainder is a different derived fact. */
    m->packet_type = d->type == ESP32_MQUICKJS_WIFI_PACKET_UNKNOWN ? 0 : (uint8_t)d->type + 1U;
    if (!info->metadata_only) {
        f->packet_length = info->captured_length;
        f->packet_readable_length = info->readable_length;
        f->captured_header_length = h->header_length < info->captured_length ? h->header_length : info->captured_length;
        f->flags = ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PRESENT | ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_POINTER_VALID;
        if (info->truncated) f->flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_TRUNCATED;
        bool parsed = h->status == ESP32_MQUICKJS_WIFI_RX_PARSED && info->header_type_matches;
        if (parsed) {
            f->flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PARSED;
            m->address_mask = h->address_mask;
            _Static_assert(sizeof(m->addresses) == sizeof(h->addresses), "shared RX address roles must match");
            memcpy(m->addresses, h->addresses, sizeof(m->addresses));
        }
        m->frame_control_available = h->frame_control_valid;
        m->duration_available = h->duration_valid;
        m->sequence_available = h->sequence_valid;
        m->qos_available = h->qos_valid;
        m->frame_control = h->frame_control;
        m->duration_id = h->duration_id;
        m->sequence_control = h->sequence_control;
        m->qos_control = h->qos_control;
        m->header_length = h->header_length;
        m->capture_mode = 2;
    }
    if (!esp32_mquickjs_wifi_rx_wire_metadata_valid(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR, f, m)) return false;
    *output = result;
    return true;
}
#endif
