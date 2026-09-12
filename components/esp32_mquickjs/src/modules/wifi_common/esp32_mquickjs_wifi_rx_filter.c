#include "esp32_mquickjs_wifi_rx_filter.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include <string.h>

bool esp32_mquickjs_wifi_rx_filter_valid(const esp32_mquickjs_wifi_rx_filter_t *filter)
{
    return filter != NULL && (filter->type_mask & ~ESP32_MQUICKJS_WIFI_RX_FILTER_TYPE_MASK) == 0 &&
        filter->source.count <= ESP32_MQUICKJS_WIFI_RX_FILTER_MAC_CAPACITY &&
        filter->destination.count <= ESP32_MQUICKJS_WIFI_RX_FILTER_MAC_CAPACITY &&
        filter->bssid.count <= ESP32_MQUICKJS_WIFI_RX_FILTER_MAC_CAPACITY &&
        filter->sample_every != 0 && filter->maximum_rate_hz <= 1000000U;
}

static bool wifi_rx_filter_mac(const esp32_mquickjs_wifi_rx_mac_filter_t *list,
    const esp32_mquickjs_wifi_rx_target_view_t *view, esp32_mquickjs_wifi_rx_address_t role)
{
    if (list->count == 0) return true;
    if (!view->header_type_matches || view->header.status != ESP32_MQUICKJS_WIFI_RX_PARSED ||
        (view->header.address_mask & (1U << role)) == 0) return false;
    for (uint8_t i = 0; i < list->count; ++i) {
        if (memcmp(list->values[i], view->header.addresses[role], 6) == 0) return true;
    }
    return false;
}

esp32_mquickjs_wifi_rx_filter_result_t esp32_mquickjs_wifi_rx_filter_evaluate(
    const esp32_mquickjs_wifi_rx_filter_t *filter,
    esp32_mquickjs_wifi_rx_filter_state_t *state,
    const esp32_mquickjs_wifi_rx_target_view_t *view, uint64_t now_us)
{
    /* Validate bounded lists before any iteration, including internal callers. */
    if (!esp32_mquickjs_wifi_rx_filter_valid(filter) || state == NULL ||
        state->sample_phase >= filter->sample_every)
        return ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG;
    if (view == NULL || !view->metadata.available ||
        (view->status != ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET &&
         view->status != ESP32_MQUICKJS_WIFI_RX_SPAN_METADATA_ONLY) ||
        (unsigned)view->metadata.type > ESP32_MQUICKJS_WIFI_PACKET_MISC)
        return ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CALLBACK;
    if ((filter->type_mask & (1U << view->metadata.type)) == 0)
        return ESP32_MQUICKJS_WIFI_RX_FILTER_TYPE;
    if (filter->valid_only) {
        if (view->metadata.rx_state != 0) return ESP32_MQUICKJS_WIFI_RX_FILTER_RX_ERROR;
        if (view->status != ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET ||
            !view->header_type_matches || view->header.status != ESP32_MQUICKJS_WIFI_RX_PARSED)
            return ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_HEADER;
    }
    if (filter->subtype_filter && (!view->header_type_matches ||
        view->header.subtype >= 16 || (filter->subtype_mask & (1U << view->header.subtype)) == 0))
        return ESP32_MQUICKJS_WIFI_RX_FILTER_SUBTYPE;
    if (!wifi_rx_filter_mac(&filter->source, view, ESP32_MQUICKJS_WIFI_RX_SOURCE) ||
        !wifi_rx_filter_mac(&filter->destination, view, ESP32_MQUICKJS_WIFI_RX_DESTINATION) ||
        !wifi_rx_filter_mac(&filter->bssid, view, ESP32_MQUICKJS_WIFI_RX_BSSID))
        return ESP32_MQUICKJS_WIFI_RX_FILTER_MAC;
    if (filter->minimum_rssi_set && view->metadata.rssi < filter->minimum_rssi)
        return ESP32_MQUICKJS_WIFI_RX_FILTER_RSSI;

    /* Phase cycles within sample_every instead of relying on a wrapping
     * lifetime counter. The first qualified packet, then every Nth, is sampled. */
    bool sampled = state->sample_phase == 0;
    if (state->sample_phase == filter->sample_every - 1U) state->sample_phase = 0;
    else ++state->sample_phase;
    if (!sampled) return ESP32_MQUICKJS_WIFI_RX_FILTER_DECIMATION;
    if (filter->maximum_rate_hz != 0 && state->last_admitted_set) {
        uint32_t interval = (1000000U + filter->maximum_rate_hz - 1U) / filter->maximum_rate_hz;
        /* Regressing time cannot underflow into a false admission. Restarting
         * a capture run requires resetting its state, never guessing a wrap. */
        if (now_us < state->last_admitted_us || now_us - state->last_admitted_us < interval)
            return ESP32_MQUICKJS_WIFI_RX_FILTER_RATE;
    }
    state->last_admitted_set = true;
    state->last_admitted_us = now_us;
    return ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT;
}
#endif
