#include "esp32_mquickjs_wifi_monitor.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_core.h"
#include <stdio.h>
#include "esp32_mquickjs_wifi_monitor_wire.h"

static const char *monitor_phy(const esp32_mquickjs_wifi_rx_driver_metadata_t *driver)
{
    static const char *const names[] = {"legacy", "ht", "vht", "he-su", "he-mu", "he-er-su", "he-tb"};
    uint8_t format = esp32_mquickjs_wifi_monitor_phy_format(driver);
    return format < sizeof(names) / sizeof(names[0]) ? names[format] : "unknown";
}

/* Destination pointers remain rooted across value allocation and property set. */
#define SET(object, name, value) do { \
    if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; \
} while (0)
#define NUMBER(object, name, value) SET(object, name, JS_NewFloat64(ctx, (double)(value)))
#define STRING(object, name, value) SET(object, name, (value) != NULL ? JS_NewString(ctx, value) : JS_NULL)
#define OPTIONAL_NUMBER(object, name, valid, value) \
    SET(object, name, (valid) ? JS_NewFloat64(ctx, (double)(value)) : JS_NULL)
#define OPTIONAL_BOOL(object, name, valid, value) SET(object, name, (valid) ? JS_NewBool(value) : JS_NULL)

JSValue esp32_mquickjs_wifi_monitor_info_to_js(JSContext *ctx,
    const esp32_mquickjs_wifi_monitor_info_t *info, uint32_t sequence, uint32_t radio_generation)
{
    const esp32_mquickjs_wifi_rx_driver_metadata_t *d = &info->driver;
    const esp32_mquickjs_wifi_rx_header_t *h = &info->header;
    esp32_mquickjs_wifi_monitor_phy_snapshot_t phy = esp32_mquickjs_wifi_monitor_phy_snapshot(d);
    JSGCRef result_ref, child_ref, packet_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *child = JS_PushGCRef(ctx, &child_ref);
    JSValue *packet = JS_PushGCRef(ctx, &packet_ref);
    *child = *packet = JS_UNDEFINED;
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    NUMBER(result, "sequence", sequence);
    NUMBER(result, "timestampUs", info->callback_time_us);
    SET(result, "timestampAccuracy", JS_NewString(ctx, "callback-time"));
    SET(result, "rxSequence", JS_NULL);
    NUMBER(result, "radioGeneration", radio_generation);

    *child = JS_NewObject(ctx);
    if (JS_IsException(*child)) goto fail;
    NUMBER(child, "rssi", d->rssi);
    OPTIONAL_NUMBER(child, "noiseFloor", d->available, d->noise_floor);
    OPTIONAL_NUMBER(child, "antenna", d->antenna_available, d->antenna);
    SET(result, "signal", *child);

    *child = JS_NewObject(ctx);
    if (JS_IsException(*child)) goto fail;
    const char *band = d->primary == 0 ? NULL : d->primary <= 14 ? "2.4GHz" : "5GHz";
    STRING(child, "band", band);
    NUMBER(child, "primary", d->primary);
    const char *secondary = d->secondary_raw == 0 ? "none" :
        d->secondary_raw == 1 ? "above" : d->secondary_raw == 2 ? "below" : NULL;
    STRING(child, "secondary", secondary);
    SET(result, "channel", *child);

    *child = JS_NewObject(ctx);
    if (JS_IsException(*child)) goto fail;
    SET(child, "format", JS_NewString(ctx, monitor_phy(d)));
#define PHY_HAS(name) ((phy.flags & ESP32_MQUICKJS_WIFI_RX_WIRE_##name) != 0U)
    OPTIONAL_NUMBER(child, "bandwidthMHz", PHY_HAS(BANDWIDTH_AVAILABLE), phy.bandwidth_mhz);
    OPTIONAL_NUMBER(child, "mcs", PHY_HAS(MCS_AVAILABLE), phy.mcs);
    OPTIONAL_NUMBER(child, "guardIntervalNs", phy.guard_interval_ns != 0, phy.guard_interval_ns);
    OPTIONAL_NUMBER(child, "heLtfSize", phy.he_ltf_size != 0, phy.he_ltf_size);
    OPTIONAL_BOOL(child, "dcm", PHY_HAS(DCM_AVAILABLE), PHY_HAS(DCM));
    /* Raw SDK rate codes differ by target; no public bitrate is inferred. */
    SET(child, "legacyRate", JS_NULL);
    OPTIONAL_BOOL(child, "stbc", PHY_HAS(STBC_AVAILABLE), PHY_HAS(STBC));
    OPTIONAL_BOOL(child, "shortGuardInterval", PHY_HAS(SGI_AVAILABLE), PHY_HAS(SGI));
    const char *fec = PHY_HAS(FEC_AVAILABLE) ? (PHY_HAS(LDPC) ? "ldpc" : "bcc") : NULL;
    STRING(child, "fecCoding", fec);
    OPTIONAL_BOOL(child, "aggregation", PHY_HAS(AGGREGATION_AVAILABLE), PHY_HAS(AGGREGATION));
    OPTIONAL_NUMBER(child, "ampduCount", phy.ampdu_count != UINT8_MAX, phy.ampdu_count);
    OPTIONAL_BOOL(child, "smoothingRecommended", PHY_HAS(SMOOTHING_AVAILABLE), PHY_HAS(SMOOTHING));
    OPTIONAL_BOOL(child, "sounding", PHY_HAS(SOUNDING_AVAILABLE), PHY_HAS(SOUNDING));
#undef PHY_HAS
    SET(result, "phy", *child);

    *child = JS_NewObject(ctx);
    if (JS_IsException(*child)) goto fail;
    static const char *const roles[] = {"source", "destination", "transmitter", "receiver", "bssid"};
    bool parsed = h->status == ESP32_MQUICKJS_WIFI_RX_PARSED && info->header_type_matches;
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_RX_ADDRESS_COUNT; ++i) {
        if (!parsed || (h->address_mask & (1U << i)) == 0) { SET(child, roles[i], JS_NULL); continue; }
        char mac[18];
        const uint8_t *a = h->addresses[i];
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
        SET(child, roles[i], JS_NewString(ctx, mac));
    }
    SET(result, "addresses", *child);
    if (info->metadata_only) {
        SET(result, "packet", JS_NULL);
    } else {
        *packet = JS_NewObject(ctx);
        if (JS_IsException(*packet)) goto fail;
        static const char *const types[] = {"management", "control", "data", "misc", "unknown"};
        unsigned type = (unsigned)d->type;
        SET(packet, "type", JS_NewString(ctx, types[type < 5 ? type : 4]));
        OPTIONAL_NUMBER(packet, "subtype", h->frame_control_valid, h->subtype);
        const char *subtype = parsed ? esp32_mquickjs_wifi_rx_subtype_name(h->type, h->subtype) : NULL;
        STRING(packet, "subtypeName", subtype);
        OPTIONAL_NUMBER(packet, "frameControl", h->frame_control_valid, h->frame_control);
        OPTIONAL_NUMBER(packet, "durationId", h->duration_valid, h->duration_id);
        OPTIONAL_NUMBER(packet, "sequenceControl", h->sequence_valid, h->sequence_control);
        OPTIONAL_NUMBER(packet, "qosControl", h->qos_valid, h->qos_control);
        *child = JS_NewObject(ctx);
        if (JS_IsException(*child)) goto fail;
        static const char *const flags[] = {"toDs", "fromDs", "moreFragments", "retry",
            "powerManagement", "moreData", "protected", "order"};
        for (unsigned i = 0; i < 8; ++i)
            OPTIONAL_BOOL(child, flags[i], h->frame_control_valid, (h->frame_control & (1U << (8U + i))) != 0);
        SET(packet, "flags", *child);
        *child = JS_NewObject(ctx);
        if (JS_IsException(*child)) goto fail;
        SET(child, "mode", JS_NewString(ctx, "full"));
        NUMBER(child, "headerLength", h->header_length);
        NUMBER(child, "payloadLength", parsed && info->readable_length > h->header_length ?
            info->readable_length - h->header_length : 0);
        NUMBER(child, "driverLength", d->driver_length);
        NUMBER(child, "capturedLength", info->captured_length);
        NUMBER(child, "payloadCapturedLength", parsed && info->captured_length > h->header_length ?
            info->captured_length - h->header_length : 0);
        SET(child, "truncated", JS_NewBool(info->truncated));
        SET(child, "fcs", JS_NewString(ctx, "unknown"));
        SET(child, "pointerLayoutValid", JS_TRUE);
        SET(child, "parseValid", JS_NewBool(parsed));
        SET(packet, "capture", *child);
        SET(result, "packet", *packet);
    }
    JS_PopGCRef(ctx, &packet_ref);
    JS_PopGCRef(ctx, &child_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &packet_ref);
    JS_PopGCRef(ctx, &child_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}
#undef SET
#undef NUMBER
#undef STRING
#undef OPTIONAL_NUMBER
#undef OPTIONAL_BOOL
#endif
