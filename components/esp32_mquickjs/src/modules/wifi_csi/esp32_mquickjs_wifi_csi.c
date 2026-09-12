#include "esp32_mquickjs_wifi_csi.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_reaper.h"
#include "esp32_mquickjs_wifi_csi_resources.h"
#include "esp32_mquickjs_wifi_csi_store.h"
#include "esp32_mquickjs_wifi_csi_rx_native.h"
#include "esp32_mquickjs_wifi_csi_wire.h"
#include "esp32_mquickjs_wifi_csi_batch.h"
#include "esp32_mquickjs_wifi_csi_target.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS
#define WIFI_CSI_PROMISCUOUS_SUPPORTED true
#else
#define WIFI_CSI_PROMISCUOUS_SUPPORTED false
#endif

#include "esp32_mquickjs_wifi_radio.h"
#include "utils/esp32_mquickjs_byte_source.h"

#include <stdatomic.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_CSI_QUEUE_LEN > \
    CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY
#error "Wi-Fi CSI queue capacity must not exceed the pool capacity"
#endif

#if CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES > \
    CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY
#error "Wi-Fi CSI maximum batch size must not exceed the pool capacity"
#endif

#define WIFI_CSI_EVENT_QUEUE_KEY "_eventQueue"
#define WIFI_CSI_BATCH_METADATA_BYTES ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES

typedef enum {
    WIFI_CSI_CLOSED = 0,
    WIFI_CSI_OPENING,
    WIFI_CSI_RUNNING,
    WIFI_CSI_STOPPING,
    WIFI_CSI_STOPPED,
    WIFI_CSI_FAULTED,
} wifi_csi_lifecycle_t;

typedef enum {
    WIFI_CSI_SOURCE_ASSOCIATED = 0,
    WIFI_CSI_SOURCE_PROMISCUOUS,
} wifi_csi_source_t;

typedef enum {
    WIFI_CSI_POWER_SAVE_PRESERVE = 0,
    WIFI_CSI_POWER_SAVE_REQUIRE_NONE,
} wifi_csi_power_save_policy_t;

typedef struct {
    wifi_csi_source_t source;
    bool fixed_channel;
    uint8_t channel;
    wifi_csi_power_save_policy_t power_save_policy;
    uint32_t pool_capacity;
    uint32_t queue_capacity;
    esp32_mquickjs_wifi_csi_capture_config_t capture;
    esp32_mquickjs_wifi_csi_packet_options_t packet;
    esp32_mquickjs_wifi_csi_filter_t filter;
} wifi_csi_options_t;

typedef struct {
    uint32_t callbacks;
    uint32_t accepted;
    uint32_t delivered_frames;
    uint32_t delivered_batches;
    uint32_t filtered_mac;
    uint32_t filtered_bssid, filtered_frame_type, filtered_frame_subtype, dropped_identity_exhausted;
    uint32_t filtered_rssi;
    uint32_t filtered_decimation;
    uint32_t filtered_rate_limit;
    uint32_t filtered_first_word_invalid;
    uint32_t filtered_channel_estimate_invalid;
    uint32_t invalid_callback_data;
    uint32_t dropped_pool_full;
    uint32_t dropped_queue_full;
    uint32_t dropped_frame_too_large;
    uint32_t dropped_closing;
    uint32_t received_bytes;
    uint32_t packet_unavailable, packet_malformed, packet_truncated;
    uint32_t dropped_packet_required, dropped_packet_incomplete, received_packet_bytes;
    uint32_t leased_frames;
} wifi_csi_stats_snapshot_t;

typedef struct {
    portMUX_TYPE lock;
    _Atomic wifi_csi_lifecycle_t lifecycle;
    uint32_t generation;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_wifi_radio_lease_t radio_lease;
    esp32_mquickjs_wifi_radio_promiscuous_lease_t promiscuous_lease;
    esp32_mquickjs_event_queue_t *event_queue;
    bool event_queue_retained;
    bool callback_registered;
    bool csi_enabled;
    bool reaper_registered;
    _Atomic bool cleanup_scheduled;
    _Atomic bool close_requested;
    _Atomic bool channel_conflicted;
    wifi_csi_options_t options;
    _Atomic(esp32_mquickjs_wifi_csi_resources_t *) resources;
    wifi_csi_stats_snapshot_t final_stats;
    uint8_t effective_channel;
    wifi_second_chan_t effective_secondary;
    uint32_t radio_generation;
    wifi_ps_type_t effective_power_save;
    esp_err_t last_error;
    const char *last_error_code;
    const char *last_error_stage;
} wifi_csi_session_t;

typedef struct {
    uint32_t generation;
} wifi_csi_session_ref_t;

typedef struct {
    uint32_t session_generation;
    uint16_t slot_index;
    uint32_t slot_generation;
    bool closed;
} wifi_csi_frame_ref_t;

typedef struct {
    uint16_t frame_count;
    bool closed;
    esp32_mquickjs_wifi_csi_event_t events[];
} wifi_csi_batch_ref_t;

typedef struct {
    esp32_mquickjs_wifi_csi_event_t event;
    bool released;
} wifi_csi_lease_ref_t;

typedef struct {
    wifi_csi_lease_ref_t lease;
    size_t offset;
    bool packet;
    bool opened;
    bool iterator_active;
    bool destroy_requested;
} wifi_csi_sample_source_t;

typedef struct {
    uint16_t frame_count;
    uint16_t payload_index;
    bool packet_pending;
    uint16_t retained_count;
    uint8_t padding_pending;
    uint32_t total_length;
    uint8_t *control;
    size_t control_length;
    bool control_emitted;
    bool opened;
    bool iterator_active;
    bool destroy_requested;
    wifi_csi_lease_ref_t leases[];
} wifi_csi_wire_source_t;
_Static_assert(ESP32_MQUICKJS_WIFI_RX_WIRE_MAX_FRAMES <=
    (SIZE_MAX - sizeof(wifi_csi_wire_source_t)) / sizeof(wifi_csi_lease_ref_t),
    "CSI wire Source allocation must fit size_t");

static const char *TAG = "esp32qjs_wifi_csi";
static wifi_csi_session_t s_wifi_csi = {.lock = portMUX_INITIALIZER_UNLOCKED};
static portMUX_TYPE s_wifi_csi_store_lock = portMUX_INITIALIZER_UNLOCKED;
static void wifi_csi_store_lock(void *opaque)
{
    (void)opaque;
    taskENTER_CRITICAL(&s_wifi_csi_store_lock);
}
static void wifi_csi_store_unlock(void *opaque)
{
    (void)opaque;
    taskEXIT_CRITICAL(&s_wifi_csi_store_lock);
}
static esp32_mquickjs_wifi_csi_store_t s_wifi_csi_store = {
    .lock = wifi_csi_store_lock, .unlock = wifi_csi_store_unlock,
    .maximum_slots = CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY,
    .max_frame_bytes = CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_FRAME_BYTES,
    .next_generation = 1,
};

static bool wifi_csi_pool_retirement_pending(void)
{
    /* Only the active control's detach suffix blocks runtime/open. Retained
     * data generations independently consume the store's bounded budget. */
    return atomic_load_explicit(&s_wifi_csi.resources, memory_order_acquire) != NULL;
}

static const char *wifi_csi_lifecycle_name(wifi_csi_lifecycle_t lifecycle)
{
    switch (lifecycle) {
        case WIFI_CSI_OPENING: return "stopped";
        case WIFI_CSI_RUNNING: return "running";
        case WIFI_CSI_STOPPING: return "stopping";
        case WIFI_CSI_STOPPED: return "stopped";
        case WIFI_CSI_FAULTED: return "faulted";
        case WIFI_CSI_CLOSED:
        default: return "closed";
    }
}

static const char *wifi_csi_source_name(wifi_csi_source_t source)
{
    return source == WIFI_CSI_SOURCE_PROMISCUOUS
        ? "promiscuous" : "associated";
}

static const char *wifi_csi_secondary_name(
    esp32_mquickjs_wifi_csi_secondary_t secondary)
{
    return secondary == ESP32_MQUICKJS_WIFI_CSI_SECONDARY_ABOVE ? "above" :
           secondary == ESP32_MQUICKJS_WIFI_CSI_SECONDARY_BELOW ? "below" :
                                                                  "none";
}

static const char *wifi_csi_wifi_secondary_name(wifi_second_chan_t secondary)
{
    return secondary == WIFI_SECOND_CHAN_ABOVE ? "above" :
           secondary == WIFI_SECOND_CHAN_BELOW ? "below" : "none";
}

static const char *wifi_csi_phy_name(esp32_mquickjs_wifi_csi_phy_t phy)
{
    switch (phy) {
        case ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY: return "legacy";
        case ESP32_MQUICKJS_WIFI_CSI_PHY_HT: return "ht";
        case ESP32_MQUICKJS_WIFI_CSI_PHY_VHT: return "vht";
        case ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU: return "he-su";
        case ESP32_MQUICKJS_WIFI_CSI_PHY_HE_MU: return "he-mu";
        case ESP32_MQUICKJS_WIFI_CSI_PHY_HE_ER_SU: return "he-er-su";
        case ESP32_MQUICKJS_WIFI_CSI_PHY_HE_TB: return "he-tb";
        case ESP32_MQUICKJS_WIFI_CSI_PHY_UNKNOWN:
        default: return "unknown";
    }
}

static void wifi_csi_format_mac(const uint8_t mac[6], char output[18])
{
    (void)snprintf(output, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int wifi_csi_hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool wifi_csi_parse_mac_text(const char *text, uint8_t output[6])
{
    size_t index;

    if (text == NULL || strlen(text) != 17U) {
        return false;
    }
    for (index = 0; index < 6U; ++index) {
        size_t offset = index * 3U;
        int high = wifi_csi_hex_digit(text[offset]);
        int low = wifi_csi_hex_digit(text[offset + 1U]);

        if (high < 0 || low < 0 ||
            (index < 5U && text[offset + 2U] != ':')) {
            return false;
        }
        output[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool wifi_csi_string_equals(JSContext *ctx, JSValue value,
                                   const char *expected)
{
    JSCStringBuf buffer;
    size_t length = 0;
    const char *text = JS_IsString(ctx, value)
        ? JS_ToCStringLen(ctx, &length, value, &buffer) : NULL;

    return text != NULL && length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static bool wifi_csi_get_optional_bool(JSContext *ctx, JSValue object,
                                       const char *name, bool *value)
{
    JSValue property = JS_GetPropertyStr(ctx, object, name);

    if (JS_IsException(property)) return false;
    if (JS_IsUndefined(property)) return true;
    if (!JS_IsBool(property)) {
        JS_ThrowTypeError(ctx, "%s must be a boolean", name);
        return false;
    }
    *value = property == JS_TRUE;
    return true;
}

static void wifi_csi_default_options(wifi_csi_options_t *options)
{
    memset(options, 0, sizeof(*options));
    options->source = WIFI_CSI_SOURCE_ASSOCIATED;
    options->power_save_policy = WIFI_CSI_POWER_SAVE_PRESERVE;
    options->pool_capacity = CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY;
    options->queue_capacity = CONFIG_ESP32_MQUICKJS_WIFI_CSI_QUEUE_LEN;
    options->packet.snap_length = ESP32_MQUICKJS_WIFI_CSI_DEFAULT_PACKET_BYTES;
    options->filter.sample_every = 1U;
    options->filter.valid_only = true;
    if (esp32_mquickjs_wifi_csi_target_is_he()) {
        options->capture.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE;
        options->capture.config.he.enable = true;
        options->capture.config.he.enable_legacy = true;
        options->capture.config.he.ht20 = true;
        options->capture.config.he.ht40 = true;
        options->capture.config.he.vht =
            esp32_mquickjs_wifi_csi_target_supports_vht();
        options->capture.config.he.he_su = true;
        options->capture.config.he.he_mu = true;
        options->capture.config.he.he_dcm = true;
        options->capture.config.he.he_beamformed = true;
        options->capture.config.he.he_stbc_ltf =
            ESP32_MQUICKJS_WIFI_CSI_HE_STBC_FIRST;
        options->capture.config.he.lltf_bits = 12U;
    } else {
        options->capture.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY;
        options->capture.config.legacy.lltf = true;
        options->capture.config.legacy.ht_ltf = true;
        options->capture.config.legacy.stbc_ht_ltf2 = true;
        options->capture.config.legacy.ltf_merge = true;
        options->capture.config.legacy.adjacent_subcarrier_filter = true;
    }
}

static bool wifi_csi_parse_mac_values_rooted(
    JSContext *ctx, JSValue *value, const char *name,
    uint8_t output[][6], uint8_t *count)
{
    uint32_t length = 1U;
    uint32_t index;
    bool array = JS_IsArray(ctx, *value);

    if (array) {
        JSValue length_value = JS_GetPropertyStr(ctx, *value, "length");

        if (JS_IsException(length_value) ||
            JS_ToUint32(ctx, &length, length_value) != 0 ||
            length == 0U || length > ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS) {
            JS_ThrowRangeError(ctx, "%s supports 1..%u MAC addresses", name,
                               ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS);
            return false;
        }
    }
    for (index = 0; index < length; ++index) {
        JSValue entry = array
            ? JS_GetPropertyUint32(ctx, *value, index) : *value;
        JSCStringBuf buffer;
        size_t text_length = 0;
        const char *text = JS_IsString(ctx, entry)
            ? JS_ToCStringLen(ctx, &text_length, entry, &buffer) : NULL;

        if (text == NULL || text_length != 17 || !wifi_csi_parse_mac_text(text, output[index])) {
            JS_ThrowTypeError(ctx,
                "%s expects xx:xx:xx:xx:xx:xx or an array of addresses",
                name);
            return false;
        }
    }
    *count = (uint8_t)length;
    return true;
}

static bool wifi_csi_parse_mac_values(
    JSContext *ctx, JSValue value, const char *name,
    uint8_t output[][6], uint8_t *count)
{
    JSGCRef value_ref;
    JSValue *rooted_value = JS_PushGCRef(ctx, &value_ref);
    bool result;

    *rooted_value = value;
    result = wifi_csi_parse_mac_values_rooted(
        ctx, rooted_value, name, output, count);
    JS_PopGCRef(ctx, &value_ref);
    return result;
}

static const char *wifi_csi_packet_type_name(unsigned type)
{
    static const char *const names[] = {"management", "control", "data", "misc", "unknown"};
    return names[type < 5 ? type : 4];
}

static bool wifi_csi_parse_frame_filter(JSContext *ctx, JSValue value,
    bool subtypes, uint16_t *mask)
{
    JSGCRef value_ref, entry_ref;
    JSValue *root = JS_PushGCRef(ctx, &value_ref);
    JSValue *entry = JS_PushGCRef(ctx, &entry_ref);
    *root = value;
    uint32_t count;
    uint16_t result = 0;
    if (!JS_IsArray(ctx, *root)) {
        JS_ThrowTypeError(ctx, "CSI frameTypes/frameSubtypes must be arrays");
        goto fail;
    }
    JSValue length = JS_GetPropertyStr(ctx, *root, "length");
    if (JS_IsException(length) || !esp32_mquickjs_value_to_bounded_u32(
            ctx, length, 0, subtypes ? 16 : 5, &count)) {
        if (!JS_HasException(ctx)) JS_ThrowRangeError(ctx, "Too many CSI frame types/subtypes");
        goto fail;
    }
    for (uint32_t i = 0; i < count; ++i) {
        *entry = JS_GetPropertyUint32(ctx, *root, i);
        uint32_t index = 0;
        if (JS_IsException(*entry)) goto fail;
        if (subtypes) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *entry, 0, 15, &index)) {
                if (!JS_HasException(ctx)) JS_ThrowRangeError(ctx, "CSI frame subtype must be 0..15");
                goto fail;
            }
        } else {
            while (index < 5 && !wifi_csi_string_equals(ctx, *entry, wifi_csi_packet_type_name(index))) ++index;
            if (JS_HasException(ctx)) goto fail;
            if (index == 5) { JS_ThrowTypeError(ctx, "Invalid CSI frame type"); goto fail; }
        }
        if (result & (1U << index)) {
            JS_ThrowTypeError(ctx, "Duplicate CSI frame type/subtype"); goto fail;
        }
        result |= 1U << index;
    }
    *mask = result;
    JS_PopGCRef(ctx, &entry_ref);
    JS_PopGCRef(ctx, &value_ref);
    return true;
fail:
    JS_PopGCRef(ctx, &entry_ref);
    JS_PopGCRef(ctx, &value_ref);
    return false;
}

static JSValue wifi_csi_frame_filter_to_js(JSContext *ctx, uint16_t mask, bool subtypes)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewArray(ctx, 0);
    if (JS_IsException(*result)) goto fail;
    uint32_t count = 0;
    for (uint32_t i = 0; i < (subtypes ? 16U : 5U); ++i) {
        if (!(mask & (1U << i))) continue;
        JSValue value = subtypes ? JS_NewUint32(ctx, i) : JS_NewString(ctx, wifi_csi_packet_type_name(i));
        if (JS_IsException(value) || JS_IsException(JS_SetPropertyUint32(ctx, *result, count++, value))) goto fail;
    }
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

static bool wifi_csi_parse_filter_rooted(JSContext *ctx, JSValue *value,
                                  esp32_mquickjs_wifi_csi_filter_t *filter)
{
    static const char *const allowed[] = {
        "sourceMac", "destinationMac", "minimumRssi", "sampleEvery",
        "maximumRateHz", "validOnly", "bssid", "frameTypes", "frameSubtypes",
    };
    JSValue property;
    int32_t signed_value;
    uint32_t unsigned_value;

    if (!esp32_mquickjs_validate_plain_options(
            ctx, *value, "wifi.csi.open({ filter })", allowed, 9U)) {
        return false;
    }
    property = JS_GetPropertyStr(ctx, *value, "sourceMac");
    if (JS_IsException(property) ||
        (!JS_IsUndefined(property) &&
         !wifi_csi_parse_mac_values(ctx, property, "filter.sourceMac",
                                    filter->source_macs,
                                    &filter->source_mac_count))) return false;
    property = JS_GetPropertyStr(ctx, *value, "destinationMac");
    if (JS_IsException(property) ||
        (!JS_IsUndefined(property) &&
         !wifi_csi_parse_mac_values(ctx, property, "filter.destinationMac",
                                    filter->destination_macs,
                                    &filter->destination_mac_count))) return false;
    property = JS_GetPropertyStr(ctx, *value, "bssid");
    if (JS_IsException(property) || (!JS_IsUndefined(property) &&
        !wifi_csi_parse_mac_values(ctx, property, "filter.bssid", filter->bssids, &filter->bssid_count))) return false;
    uint16_t mask;
    property = JS_GetPropertyStr(ctx, *value, "frameTypes");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (!wifi_csi_parse_frame_filter(ctx, property, false, &mask)) return false;
        filter->frame_types = (uint8_t)mask;
        filter->frame_types_set = true;
    }
    property = JS_GetPropertyStr(ctx, *value, "frameSubtypes");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (!wifi_csi_parse_frame_filter(ctx, property, true, &mask)) return false;
        filter->frame_subtypes = mask;
        filter->frame_subtypes_set = true;
    }
    property = JS_GetPropertyStr(ctx, *value, "minimumRssi");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (!esp32_mquickjs_value_to_bounded_i32(
                ctx, property, -128, 127, &signed_value)) {
            return JS_ThrowRangeError(
                ctx, "filter.minimumRssi must be -128..127"), false;
        }
        filter->minimum_rssi = (int8_t)signed_value;
        filter->minimum_rssi_set = true;
    }
    property = JS_GetPropertyStr(ctx, *value, "sampleEvery");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (!esp32_mquickjs_value_to_bounded_u32(
                ctx, property, 1U, UINT32_MAX, &unsigned_value)) {
            return JS_ThrowRangeError(
                ctx, "filter.sampleEvery must be a positive integer"), false;
        }
        filter->sample_every = unsigned_value;
    }
    property = JS_GetPropertyStr(ctx, *value, "maximumRateHz");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (!esp32_mquickjs_value_to_bounded_u32(
                ctx, property, 1U, 1000000U, &unsigned_value)) {
            return JS_ThrowRangeError(
                ctx, "filter.maximumRateHz must be 1..1000000"), false;
        }
        filter->maximum_rate_hz = unsigned_value;
    }
    return wifi_csi_get_optional_bool(
        ctx, *value, "validOnly", &filter->valid_only);
}

static bool wifi_csi_parse_filter(JSContext *ctx, JSValue value,
                                  esp32_mquickjs_wifi_csi_filter_t *filter)
{
    JSGCRef value_ref;
    JSValue *rooted_value = JS_PushGCRef(ctx, &value_ref);
    bool result;

    *rooted_value = value;
    result = wifi_csi_parse_filter_rooted(ctx, rooted_value, filter);
    JS_PopGCRef(ctx, &value_ref);
    return result;
}

static bool wifi_csi_parse_legacy_capture_rooted(
    JSContext *ctx, JSValue *value,
    esp32_mquickjs_wifi_csi_capture_config_t *capture)
{
    static const char *const allowed[] = {
        "schema", "lltf", "htLtf", "stbcHtLtf2", "ltfMerge",
        "adjacentSubcarrierFilter", "scale", "dumpAck",
    };
    JSValue property;
    uint32_t shift;
    static const char *const scale_allowed[] = {"shiftBits"};

    if (!esp32_mquickjs_validate_plain_options(
            ctx, *value, "wifi.csi legacy capture", allowed, 8U)) return false;
    if (!wifi_csi_get_optional_bool(ctx, *value, "lltf",
            &capture->config.legacy.lltf) ||
        !wifi_csi_get_optional_bool(ctx, *value, "htLtf",
            &capture->config.legacy.ht_ltf) ||
        !wifi_csi_get_optional_bool(ctx, *value, "stbcHtLtf2",
            &capture->config.legacy.stbc_ht_ltf2) ||
        !wifi_csi_get_optional_bool(ctx, *value, "ltfMerge",
            &capture->config.legacy.ltf_merge) ||
        !wifi_csi_get_optional_bool(ctx, *value, "adjacentSubcarrierFilter",
            &capture->config.legacy.adjacent_subcarrier_filter) ||
        !wifi_csi_get_optional_bool(ctx, *value, "dumpAck",
            &capture->config.legacy.dump_ack)) return false;
    property = JS_GetPropertyStr(ctx, *value, "scale");
    if (JS_IsException(property)) return false;
    if (JS_IsUndefined(property) || wifi_csi_string_equals(ctx, property, "auto")) {
        capture->config.legacy.manual_scale = false;
        capture->config.legacy.shift_bits = 0U;
        return true;
    }
    if (!esp32_mquickjs_validate_plain_options(
            ctx, property, "wifi.csi legacy scale", scale_allowed, 1U)) {
        return false;
    }
    property = JS_GetPropertyStr(ctx, *value, "scale");
    if (JS_IsException(property)) return false;
    property = JS_GetPropertyStr(ctx, property, "shiftBits");
    if (JS_IsException(property) ||
        !esp32_mquickjs_value_to_bounded_u32(ctx, property, 0U, 15U, &shift)) {
        return JS_ThrowRangeError(ctx, "scale.shiftBits must be 0..15"), false;
    }
    capture->config.legacy.manual_scale = true;
    capture->config.legacy.shift_bits = (uint8_t)shift;
    return true;
}

static bool wifi_csi_parse_legacy_capture(
    JSContext *ctx, JSValue value,
    esp32_mquickjs_wifi_csi_capture_config_t *capture)
{
    JSGCRef value_ref;
    JSValue *rooted_value = JS_PushGCRef(ctx, &value_ref);
    bool result;

    *rooted_value = value;
    result = wifi_csi_parse_legacy_capture_rooted(
        ctx, rooted_value, capture);
    JS_PopGCRef(ctx, &value_ref);
    return result;
}

static bool wifi_csi_parse_he_capture_rooted(
    JSContext *ctx, JSValue *value,
    esp32_mquickjs_wifi_csi_capture_config_t *capture)
{
    static const char *const allowed[] = {
        "schema", "enableLegacy", "forceLegacyLtf", "ht20", "ht40",
        "vht", "heSu", "heMu", "heDcm", "heBeamformed", "heStbcLtf",
        "valueScale", "dumpAck", "lltfBits",
    };
    JSValue property;
    uint32_t number;
    size_t choice;
    static const char *const stbc_choices[] = {
        "first", "second", "alternate",
    };

    if (!esp32_mquickjs_validate_plain_options(
            ctx, *value, "wifi.csi HE capture", allowed, 14U)) return false;
    if (!wifi_csi_get_optional_bool(ctx, *value, "enableLegacy",
            &capture->config.he.enable_legacy) ||
        !wifi_csi_get_optional_bool(ctx, *value, "forceLegacyLtf",
            &capture->config.he.force_legacy_ltf) ||
        !wifi_csi_get_optional_bool(ctx, *value, "ht20",
            &capture->config.he.ht20) ||
        !wifi_csi_get_optional_bool(ctx, *value, "ht40",
            &capture->config.he.ht40) ||
        !wifi_csi_get_optional_bool(ctx, *value, "vht",
            &capture->config.he.vht) ||
        !wifi_csi_get_optional_bool(ctx, *value, "heSu",
            &capture->config.he.he_su) ||
        !wifi_csi_get_optional_bool(ctx, *value, "heMu",
            &capture->config.he.he_mu) ||
        !wifi_csi_get_optional_bool(ctx, *value, "heDcm",
            &capture->config.he.he_dcm) ||
        !wifi_csi_get_optional_bool(ctx, *value, "heBeamformed",
            &capture->config.he.he_beamformed) ||
        !wifi_csi_get_optional_bool(ctx, *value, "dumpAck",
            &capture->config.he.dump_ack)) return false;
    property = JS_GetPropertyStr(ctx, *value, "heStbcLtf");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (!esp32_mquickjs_value_to_enum(
                ctx, property, stbc_choices, 3U, &choice)) {
            return JS_ThrowTypeError(
                ctx, "capture.heStbcLtf expects first, second, or alternate"), false;
        }
        capture->config.he.he_stbc_ltf =
            (esp32_mquickjs_wifi_csi_he_stbc_t)choice;
    }
    property = JS_GetPropertyStr(ctx, *value, "valueScale");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (!esp32_mquickjs_value_to_bounded_u32(
                ctx, property, 0U, 8U, &number)) {
            return JS_ThrowRangeError(ctx, "capture.valueScale must be 0..8"), false;
        }
        capture->config.he.value_scale = (uint8_t)number;
    }
    property = JS_GetPropertyStr(ctx, *value, "lltfBits");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (!esp32_mquickjs_value_to_bounded_u32(
                ctx, property, 8U, 12U, &number) ||
            (number != 8U && number != 12U)) {
            return JS_ThrowRangeError(ctx, "capture.lltfBits expects 8 or 12"), false;
        }
        capture->config.he.lltf_bits = (uint8_t)number;
    }
    return true;
}

static bool wifi_csi_parse_he_capture(
    JSContext *ctx, JSValue value,
    esp32_mquickjs_wifi_csi_capture_config_t *capture)
{
    JSGCRef value_ref;
    JSValue *rooted_value = JS_PushGCRef(ctx, &value_ref);
    bool result;

    *rooted_value = value;
    result = wifi_csi_parse_he_capture_rooted(ctx, rooted_value, capture);
    JS_PopGCRef(ctx, &value_ref);
    return result;
}

static bool wifi_csi_parse_source(JSContext *ctx, JSValue value,
                                  wifi_csi_options_t *options)
{
    static const char *const allowed[] = {"mode", "channel"};
    static const char *const associated_allowed[] = {"mode"};
    JSGCRef value_ref;
    JSValue *rooted_value = JS_PushGCRef(ctx, &value_ref);
    *rooted_value = value;
    bool result = false;
    JSValue property;
    uint32_t number;
    if (!esp32_mquickjs_validate_plain_options(
            ctx, *rooted_value, "CSI source", allowed, 2U)) goto done;
    property = JS_GetPropertyStr(ctx, *rooted_value, "mode");
    if (JS_IsException(property)) goto done;
    if (wifi_csi_string_equals(ctx, property, "associated")) {
        /* No channel key at all, including channel:undefined. */
        result = esp32_mquickjs_validate_plain_options(
            ctx, *rooted_value, "associated CSI source", associated_allowed, 1U);
        if (result) options->source = WIFI_CSI_SOURCE_ASSOCIATED;
        goto done;
    }
    if (!wifi_csi_string_equals(ctx, property, "promiscuous")) {
        JS_ThrowTypeError(ctx, "source.mode expects associated or promiscuous");
        goto done;
    }
#if CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS
    options->source = WIFI_CSI_SOURCE_PROMISCUOUS;
#else
    JS_ThrowTypeError(ctx,
        "WIFI_CSI_CONFIG_UNSUPPORTED: promiscuous capture is disabled by this Build Context");
    goto done;
#endif
    property = JS_GetPropertyStr(ctx, *rooted_value, "channel");
    if (JS_IsException(property)) goto done;
    if (!JS_IsUndefined(property) &&
        !wifi_csi_string_equals(ctx, property, "current")) {
        if (!esp32_mquickjs_value_to_bounded_u32(
                ctx, property, 1U, UINT8_MAX, &number) ||
            !esp32_mquickjs_wifi_csi_target_channel_structurally_valid(
#if CONFIG_SOC_WIFI_SUPPORT_5G
                true,
#else
                false,
#endif
                (uint8_t)number)) {
            JS_ThrowRangeError(ctx, "source.channel is not structurally valid for this target");
            goto done;
        }
        options->fixed_channel = true;
        options->channel = (uint8_t)number;
    }
    result = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    return result;
}

static bool wifi_csi_parse_buffering(JSContext *ctx, JSValue value,
                                     wifi_csi_options_t *options)
{
    static const char *const allowed[] = {"poolCapacity", "queueCapacity", "overflow"};
    JSGCRef value_ref;
    JSValue *rooted_value = JS_PushGCRef(ctx, &value_ref);
    *rooted_value = value;
    bool result = false;
    if (!esp32_mquickjs_validate_plain_options(
            ctx, *rooted_value, "CSI buffering", allowed, 3U)) goto done;
    JSValue property = JS_GetPropertyStr(ctx, *rooted_value, "poolCapacity");
    if (JS_IsException(property)) goto done;
    if (!JS_IsUndefined(property) &&
        !esp32_mquickjs_value_to_bounded_u32(ctx, property, 1U,
            CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY, &options->pool_capacity)) {
        JS_ThrowRangeError(ctx, "buffering.poolCapacity exceeds this Build Context");
        goto done;
    }
    /* A small pool remains usable when queueCapacity is omitted. Explicit
     * queue sizes are checked, never silently clipped. */
    if (options->queue_capacity > options->pool_capacity)
        options->queue_capacity = options->pool_capacity;
    property = JS_GetPropertyStr(ctx, *rooted_value, "queueCapacity");
    if (JS_IsException(property)) goto done;
    if (!JS_IsUndefined(property) &&
        !esp32_mquickjs_value_to_bounded_u32(ctx, property, 1U,
            options->pool_capacity, &options->queue_capacity)) {
        JS_ThrowRangeError(ctx, "buffering.queueCapacity must not exceed poolCapacity");
        goto done;
    }
    property = JS_GetPropertyStr(ctx, *rooted_value, "overflow");
    if (JS_IsException(property)) goto done;
    if (!JS_IsUndefined(property) && !wifi_csi_string_equals(ctx, property, "drop-newest")) {
        JS_ThrowTypeError(ctx, "buffering.overflow only supports drop-newest");
        goto done;
    }
    result = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    return result;
}

static const char *wifi_csi_packet_mode_name(esp32_mquickjs_wifi_csi_packet_mode_t mode)
{
    return mode == ESP32_MQUICKJS_WIFI_CSI_PACKET_HEADER ? "header" :
        mode == ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL ? "full" : "none";
}

static bool wifi_csi_parse_packet(JSContext *ctx, JSValue value,
    esp32_mquickjs_wifi_csi_packet_options_t *options)
{
    static const char *const allowed[] = {"content", "snapLength", "required", "requireComplete"};
    JSGCRef ref;
    JSValue *root = JS_PushGCRef(ctx, &ref);
    *root = value;
    bool result = false, content_set = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "CSI packet", allowed, 4)) goto done;
    JSValue property = JS_GetPropertyStr(ctx, *root, "content");
    if (JS_IsException(property)) goto done;
    if (!JS_IsUndefined(property)) {
        content_set = true;
        if (wifi_csi_string_equals(ctx, property, "none")) options->mode = ESP32_MQUICKJS_WIFI_CSI_PACKET_NONE;
        else if (wifi_csi_string_equals(ctx, property, "header")) options->mode = ESP32_MQUICKJS_WIFI_CSI_PACKET_HEADER;
        else if (wifi_csi_string_equals(ctx, property, "full")) options->mode = ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL;
        else { JS_ThrowTypeError(ctx, "packet.content expects none, header or full"); goto done; }
    }
    property = JS_GetPropertyStr(ctx, *root, "snapLength");
    if (JS_IsException(property)) goto done;
    if (!JS_IsUndefined(property) && !esp32_mquickjs_value_to_bounded_u32(ctx, property,
            1, ESP32_MQUICKJS_WIFI_CSI_MAX_PACKET_BYTES, &options->snap_length)) {
        JS_ThrowRangeError(ctx, "packet.snapLength is outside the supported range"); goto done;
    }
    if (!wifi_csi_get_optional_bool(ctx, *root, "required", &options->required) ||
        !wifi_csi_get_optional_bool(ctx, *root, "requireComplete", &options->require_complete)) goto done;
    if (options->require_complete) {
        if (!content_set) options->mode = ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL;
        options->required = true;
    }
    if (!esp32_mquickjs_wifi_csi_packet_options_valid(options)) {
        JS_ThrowRangeError(ctx, "CSI packet requires a complete header capacity; required needs header/full and requireComplete needs full");
        goto done;
    }
    result = true;
done:
    JS_PopGCRef(ctx, &ref);
    return result;
}

static JSValue wifi_csi_packet_options_to_js(JSContext *ctx,
    const esp32_mquickjs_wifi_csi_packet_options_t *options)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    bool ok = !JS_IsException(*result) &&
        esp32_mquickjs_set_property_ref(ctx, result, "content", JS_NewString(ctx, wifi_csi_packet_mode_name(options->mode))) &&
        esp32_mquickjs_set_property_ref(ctx, result, "snapLength", JS_NewUint32(ctx, options->snap_length)) &&
        esp32_mquickjs_set_property_ref(ctx, result, "required", JS_NewBool(options->required)) &&
        esp32_mquickjs_set_property_ref(ctx, result, "requireComplete", JS_NewBool(options->require_complete));
    JSValue value = JS_PopGCRef(ctx, &ref);
    return ok ? value : JS_EXCEPTION;
}

static bool wifi_csi_parse_open_options_rooted(JSContext *ctx, JSValue *value,
                                        wifi_csi_options_t *options)
{
    static const char *const allowed[] = {"source", "capture", "filter", "buffering", "powerSavePolicy", "packet"};
    JSValue property;
    esp32_mquickjs_wifi_csi_target_config_result_t target_result;

    wifi_csi_default_options(options);
    if (!esp32_mquickjs_validate_plain_options(
            ctx, *value, "wifi.csi.open(options)", allowed, 6U)) return false;
    property = JS_GetPropertyStr(ctx, *value, "source");
    if (JS_IsException(property) ||
        (!JS_IsUndefined(property) && !wifi_csi_parse_source(ctx, property, options))) return false;
    property = JS_GetPropertyStr(ctx, *value, "powerSavePolicy");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (wifi_csi_string_equals(ctx, property, "preserve")) {
            options->power_save_policy = WIFI_CSI_POWER_SAVE_PRESERVE;
        } else if (wifi_csi_string_equals(ctx, property, "require-none")) {
            options->power_save_policy = WIFI_CSI_POWER_SAVE_REQUIRE_NONE;
        } else {
            return JS_ThrowTypeError(
                ctx, "powerSavePolicy expects preserve or require-none"), false;
        }
    }
    property = JS_GetPropertyStr(ctx, *value, "packet");
    if (JS_IsException(property) ||
        (!JS_IsUndefined(property) && !wifi_csi_parse_packet(ctx, property, &options->packet))) return false;
    property = JS_GetPropertyStr(ctx, *value, "filter");
    if (JS_IsException(property) ||
        (!JS_IsUndefined(property) &&
         !wifi_csi_parse_filter(ctx, property, &options->filter))) return false;
    property = JS_GetPropertyStr(ctx, *value, "buffering");
    if (JS_IsException(property) ||
        (!JS_IsUndefined(property) && !wifi_csi_parse_buffering(ctx, property, options))) return false;
    property = JS_GetPropertyStr(ctx, *value, "capture");
    if (JS_IsException(property) || JS_IsUndefined(property)) {
        return JS_ThrowTypeError(ctx, "wifi.csi.open() requires capture"), false;
    }
    JSValue schema = JS_GetPropertyStr(ctx, property, "schema");
    if (JS_IsException(schema)) return false;
    if (wifi_csi_string_equals(ctx, schema, "wifi-csi-legacy/1")) {
        options->capture.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY;
        property = JS_GetPropertyStr(ctx, *value, "capture");
        if (JS_IsException(property)) return false;
        if (!wifi_csi_parse_legacy_capture(ctx, property,
                                           &options->capture)) return false;
    } else if (wifi_csi_string_equals(ctx, schema, "wifi-csi-he/1")) {
        options->capture.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE;
        property = JS_GetPropertyStr(ctx, *value, "capture");
        if (JS_IsException(property)) return false;
        if (!wifi_csi_parse_he_capture(ctx, property,
                                       &options->capture)) return false;
    } else {
        return JS_ThrowTypeError(
            ctx, "WIFI_CSI_CONFIG_SCHEMA_MISMATCH: unsupported capture schema"), false;
    }
    target_result = esp32_mquickjs_wifi_csi_target_config_validate(
        esp32_mquickjs_wifi_csi_target_is_he(),
        esp32_mquickjs_wifi_csi_target_supports_vht(),
        esp32_mquickjs_wifi_csi_target_supports_lltf_bit_mode(),
        &options->capture);
    if (target_result != ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK) {
        return JS_ThrowTypeError(
            ctx, target_result ==
                    ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_SCHEMA_MISMATCH
                ? "WIFI_CSI_CONFIG_SCHEMA_MISMATCH: capture schema does not match this target"
                : target_result ==
                        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_UNSUPPORTED
                    ? "WIFI_CSI_CONFIG_UNSUPPORTED: capture field is not supported by this target"
                    : "WIFI_CSI_CONFIG_INVALID: invalid capture config"), false;
    }
    return true;
}

static bool wifi_csi_parse_open_options(JSContext *ctx, JSValue value,
                                        wifi_csi_options_t *options)
{
    JSGCRef value_ref;
    JSValue *rooted_value = JS_PushGCRef(ctx, &value_ref);
    bool result;

    *rooted_value = value;
    result = wifi_csi_parse_open_options_rooted(ctx, rooted_value, options);
    JS_PopGCRef(ctx, &value_ref);
    return result;
}

static void *wifi_csi_resource_calloc(size_t count, size_t size, void *opaque)
{
    (void)opaque;
    return esp32_mquickjs_memory_wireless_calloc("wifi.csi", count, size,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
}

static void *wifi_csi_resource_malloc(size_t size, void *opaque)
{
    (void)opaque;
    return esp32_mquickjs_memory_wireless_alloc("wifi.csi", size,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
}

static void wifi_csi_resource_free(void *ptr, void *opaque)
{
    (void)opaque;
    esp32_mquickjs_memory_payload_free(ptr);
}

static void wifi_csi_resource_retire(void *ptr, void *opaque)
{
    (void)opaque;
    (void)esp32_mquickjs_memory_wireless_retire(ptr);
}

static esp32_mquickjs_wifi_csi_allocator_t wifi_csi_resource_allocator(void)
{
    esp32_mquickjs_wifi_csi_allocator_t allocator = {
        .calloc_fn = wifi_csi_resource_calloc,
        .malloc_fn = wifi_csi_resource_malloc,
        .free_fn = wifi_csi_resource_free,
        .retire_fn = wifi_csi_resource_retire,
    };

    return allocator;
}

static void wifi_csi_snapshot_stats(wifi_csi_session_t *session)
{
    esp32_mquickjs_wifi_csi_counters_t *counters =
        &session->resources->counters;

#define WIFI_CSI_SNAPSHOT(name) \
    session->final_stats.name = atomic_load_explicit( \
        &counters->name, memory_order_acquire)
    WIFI_CSI_SNAPSHOT(callbacks);
    WIFI_CSI_SNAPSHOT(accepted);
    WIFI_CSI_SNAPSHOT(delivered_frames);
    WIFI_CSI_SNAPSHOT(delivered_batches);
    WIFI_CSI_SNAPSHOT(filtered_mac);
    WIFI_CSI_SNAPSHOT(filtered_bssid);
    WIFI_CSI_SNAPSHOT(filtered_frame_type);
    WIFI_CSI_SNAPSHOT(filtered_frame_subtype);
    WIFI_CSI_SNAPSHOT(dropped_identity_exhausted);
    WIFI_CSI_SNAPSHOT(filtered_rssi);
    WIFI_CSI_SNAPSHOT(filtered_decimation);
    WIFI_CSI_SNAPSHOT(filtered_rate_limit);
    WIFI_CSI_SNAPSHOT(filtered_first_word_invalid);
    WIFI_CSI_SNAPSHOT(filtered_channel_estimate_invalid);
    WIFI_CSI_SNAPSHOT(invalid_callback_data);
    WIFI_CSI_SNAPSHOT(dropped_pool_full);
    WIFI_CSI_SNAPSHOT(dropped_queue_full);
    WIFI_CSI_SNAPSHOT(dropped_frame_too_large);
    WIFI_CSI_SNAPSHOT(dropped_closing);
    WIFI_CSI_SNAPSHOT(received_bytes);
    WIFI_CSI_SNAPSHOT(packet_unavailable);
    WIFI_CSI_SNAPSHOT(packet_malformed);
    WIFI_CSI_SNAPSHOT(packet_truncated);
    WIFI_CSI_SNAPSHOT(dropped_packet_required);
    WIFI_CSI_SNAPSHOT(dropped_packet_incomplete);
    WIFI_CSI_SNAPSHOT(received_packet_bytes);
    WIFI_CSI_SNAPSHOT(leased_frames);
#undef WIFI_CSI_SNAPSHOT
}

static void wifi_csi_maybe_destroy_resources(wifi_csi_session_t *session)
{
    esp32_mquickjs_wifi_csi_resources_t *resources = NULL;
    if (session == NULL) return;
    taskENTER_CRITICAL(&session->lock);
    if (session->resources != NULL &&
        atomic_load_explicit(&session->lifecycle, memory_order_acquire) == WIFI_CSI_CLOSED) {
        wifi_csi_snapshot_stats(session);
        resources = atomic_exchange_explicit(&session->resources, NULL, memory_order_acq_rel);
    }
    taskEXIT_CRITICAL(&session->lock);
    if (resources != NULL && !esp32_mquickjs_wifi_csi_store_retire(&s_wifi_csi_store, resources)) {
        /* Unexpected native liveness remains owned and prevents reopen. */
        atomic_store_explicit(&session->resources, resources, memory_order_release);
    }
}

static esp32_mquickjs_wifi_csi_slot_t *wifi_csi_resolve_event(
    const esp32_mquickjs_wifi_csi_event_t *event)
{
    if (event == NULL) return NULL;
    esp32_mquickjs_wifi_csi_resources_t *resources =
        esp32_mquickjs_wifi_csi_store_acquire(&s_wifi_csi_store, event->session_generation);
    esp32_mquickjs_wifi_csi_slot_t *slot = esp32_mquickjs_wifi_csi_slot_from_event(resources, event);
    esp32_mquickjs_wifi_csi_store_release(&s_wifi_csi_store, resources);
    /* The caller's existing event/public/view/source owner pins this slot. */
    return slot;
}

typedef enum {
    WIFI_CSI_EVENT_RETAIN, WIFI_CSI_EVENT_RELEASE, WIFI_CSI_EVENT_CLOSE,
    WIFI_CSI_EVENT_TAKE, WIFI_CSI_EVENT_DISCARD, WIFI_CSI_EVENT_DELIVERED,
    WIFI_CSI_EVENT_BATCH_DELIVERED,
} wifi_csi_event_action_t;

static bool wifi_csi_update_event(const esp32_mquickjs_wifi_csi_event_t *event,
    wifi_csi_event_action_t action)
{
    if (event == NULL) return false;
    esp32_mquickjs_wifi_csi_resources_t *resources =
        esp32_mquickjs_wifi_csi_store_acquire(&s_wifi_csi_store, event->session_generation);
    esp32_mquickjs_wifi_csi_slot_t *slot = esp32_mquickjs_wifi_csi_slot_from_event(resources, event);
    bool result = false;
    if (slot != NULL) {
        switch (action) {
            case WIFI_CSI_EVENT_RETAIN:
                result = esp32_mquickjs_wifi_csi_slot_retain(resources, slot); break;
            case WIFI_CSI_EVENT_RELEASE:
                result = esp32_mquickjs_wifi_csi_slot_release(resources, slot); break;
            case WIFI_CSI_EVENT_CLOSE:
                result = esp32_mquickjs_wifi_csi_slot_close_public_owner(resources, slot); break;
            case WIFI_CSI_EVENT_TAKE:
                result = esp32_mquickjs_wifi_csi_slot_take_event_owner(resources, event); break;
            case WIFI_CSI_EVENT_DISCARD:
                result = esp32_mquickjs_wifi_csi_slot_discard_event(resources, event); break;
            case WIFI_CSI_EVENT_DELIVERED:
                atomic_fetch_add_explicit(&resources->counters.delivered_frames, 1U, memory_order_relaxed);
                result = true; break;
            case WIFI_CSI_EVENT_BATCH_DELIVERED:
                atomic_fetch_add_explicit(&resources->counters.delivered_batches, 1U, memory_order_relaxed);
                result = true; break;
        }
    }
    esp32_mquickjs_wifi_csi_store_release(&s_wifi_csi_store, resources);
    return result;
}

static void wifi_csi_lease_release(wifi_csi_lease_ref_t *lease)
{
    if (lease == NULL || lease->released) return;
    (void)wifi_csi_update_event(&lease->event, WIFI_CSI_EVENT_RELEASE);
    lease->released = true;
}

static void wifi_csi_lease_request_close(wifi_csi_lease_ref_t *lease)
{
    if (lease == NULL || lease->released) return;
    (void)wifi_csi_update_event(&lease->event, WIFI_CSI_EVENT_CLOSE);
    lease->released = true;
}

static wifi_csi_lease_ref_t *wifi_csi_retain_event(
    const esp32_mquickjs_wifi_csi_event_t *event)
{
    if (!wifi_csi_update_event(event, WIFI_CSI_EVENT_RETAIN)) return NULL;
    wifi_csi_lease_ref_t *lease = esp32_mquickjs_memory_wireless_calloc("wifi.csi", 1, sizeof(*lease), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (lease == NULL) {
        (void)wifi_csi_update_event(event, WIFI_CSI_EVENT_RELEASE);
        return NULL;
    }
    lease->event = *event;
    return lease;
}

static void wifi_csi_byte_view_release(void *opaque)
{
    wifi_csi_lease_ref_t *lease = opaque;

    wifi_csi_lease_release(lease);
    esp32_mquickjs_memory_payload_free(lease);
}

static bool wifi_csi_publish_event(
    const esp32_mquickjs_wifi_csi_event_t *event, void *opaque)
{
    wifi_csi_session_t *session = opaque;

    return session != NULL &&
        esp32_mquickjs_event_queue_try_send_from_callback(
            session->event_queue, event);
}

static bool wifi_csi_channel_admit(wifi_csi_session_t *session)
{
    esp32_mquickjs_wifi_radio_channel_status_t channel;
    esp_err_t err = esp32_mquickjs_wifi_radio_lease_channel_status(&session->radio_lease, &channel);
    if (channel.conflicted || atomic_load_explicit(&session->channel_conflicted, memory_order_acquire)) {
        atomic_store_explicit(&session->channel_conflicted, true, memory_order_release);
        esp32_mquickjs_wifi_csi_resources_set_accepting(session->resources, false);
        wifi_csi_lifecycle_t expected = WIFI_CSI_RUNNING;
        (void)atomic_compare_exchange_strong(&session->lifecycle, &expected, WIFI_CSI_FAULTED);
        return false;
    }
    return err == ESP_OK;
}

/* Runtime-only status refresh; callback admission uses the short Radio snapshot.
 * Retained Frame/View storage remains valid when a fixed session faults. */
static void wifi_csi_refresh_channel(wifi_csi_session_t *session)
{
    if (session->resources != NULL && atomic_load_explicit(&session->resources->identity_exhausted, memory_order_acquire)) {
        if (session->last_error == ESP_OK) {
            session->last_error = ESP_ERR_INVALID_STATE;
            session->last_error_code = "WIFI_CSI_IDENTITY_EXHAUSTED";
            session->last_error_stage = "capture-identity";
        }
        wifi_csi_lifecycle_t expected = WIFI_CSI_RUNNING;
        (void)atomic_compare_exchange_strong(&session->lifecycle, &expected, WIFI_CSI_FAULTED);
    }
    if (!session->radio_lease.acquired) return;
    uint8_t primary;
    wifi_second_chan_t secondary;
    uint32_t generation;
    (void)esp32_mquickjs_wifi_radio_get_channel(&primary, &secondary, &generation);
    esp32_mquickjs_wifi_radio_channel_status_t channel;
    if (esp32_mquickjs_wifi_radio_lease_channel_status(&session->radio_lease, &channel) == ESP_OK) {
        session->effective_channel = channel.primary;
        session->effective_secondary = channel.secondary;
        session->radio_generation = session->radio_lease.generation;
    }
    (void)wifi_csi_channel_admit(session);
    if (atomic_load_explicit(&session->channel_conflicted, memory_order_acquire) &&
        session->last_error == ESP_OK) {
        session->last_error = ESP_ERR_INVALID_STATE;
        session->last_error_code = "WIFI_CSI_CHANNEL_CONFLICT";
        session->last_error_stage = "radio-channel-change";
    }
}

static void wifi_csi_rx_callback(void *opaque, wifi_csi_info_t *info)
{
    uint64_t callback_time_us = (uint64_t)esp_timer_get_time();
    esp32_mquickjs_wifi_csi_rx_packet_t native_packet = {0};
    bool packet_available = esp32_mquickjs_wifi_csi_rx_native_take(info, &native_packet);
    wifi_csi_session_t *session = opaque;
    esp32_mquickjs_wifi_csi_metadata_t metadata;

    if (!esp32_mquickjs_wifi_csi_callback_enter(session->resources)) {
        esp32_mquickjs_wifi_csi_callback_leave(session->resources);
        return;
    }
    if (!wifi_csi_channel_admit(session)) {
        esp32_mquickjs_wifi_csi_callback_leave(session->resources);
        return;
    }
    if (info == NULL || info->buf == NULL) {
        atomic_fetch_add_explicit(
            &session->resources->counters.invalid_callback_data, 1U,
            memory_order_relaxed);
    } else {
        esp32_mquickjs_wifi_csi_target_normalize_metadata(
            info, &session->options.capture, &metadata);
        metadata.timestamp_us = callback_time_us;
        metadata.radio_generation = session->radio_lease.generation;
        /* RX may precede the default-loop home-channel notification. Do not
         * admit a packet from another primary into a strict fixed session. */
        if (session->options.fixed_channel && metadata.channel != session->options.channel) {
            atomic_store_explicit(&session->channel_conflicted, true, memory_order_release);
            (void)wifi_csi_channel_admit(session);
            esp32_mquickjs_wifi_csi_callback_leave(session->resources);
            return;
        }
        esp32_mquickjs_wifi_csi_packet_input_t packet = {
            .bytes = packet_available ? native_packet.bytes : NULL,
            .copied_length = native_packet.readable_bytes,
            .driver_packet_length = info->rx_ctrl.sig_len,
            .driver_payload_length = info->payload_len,
        };
        if (esp32_mquickjs_wifi_csi_callback_publish(
                session->resources, &metadata, (const uint8_t *)info->buf,
                info->len, &packet, wifi_csi_publish_event, session) ==
                ESP32_MQUICKJS_WIFI_CSI_PUBLISH_IDENTITY_EXHAUSTED) {
            wifi_csi_lifecycle_t expected = WIFI_CSI_RUNNING;
            (void)atomic_compare_exchange_strong(&session->lifecycle, &expected, WIFI_CSI_FAULTED);
        }
    }
    esp32_mquickjs_wifi_csi_callback_leave(session->resources);
}

static void wifi_csi_event_drop(void *event, void *opaque)
{
    (void)opaque;
    (void)wifi_csi_update_event(event, WIFI_CSI_EVENT_DISCARD);
}

static void wifi_csi_request_reap(wifi_csi_session_t *session);

static void wifi_csi_event_queue_close(void *opaque)
{
    wifi_csi_session_t *session = &s_wifi_csi;

    if ((uint32_t)(uintptr_t)opaque == session->generation && atomic_load_explicit(
            &session->lifecycle, memory_order_acquire) != WIFI_CSI_CLOSED) {
        wifi_csi_request_reap(session);
    }
}

static JSValue wifi_csi_throw_driver_error(JSContext *ctx,
                                            const char *code,
                                            const char *stage,
                                            esp_err_t err)
{
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    JSValue result;

    s_wifi_csi.last_error = err;
    s_wifi_csi.last_error_code = code;
    s_wifi_csi.last_error_stage = stage;
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "stage", JS_NewString(ctx, stage)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "espCode", JS_NewInt32(ctx, err)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "espName", JS_NewString(ctx, esp_err_to_name(err))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "radioGeneration",
            JS_NewUint32(ctx, s_wifi_csi.radio_generation)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "configSchema",
            JS_NewString(ctx,
                esp32_mquickjs_wifi_csi_target_schema_name()))) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    result = esp32_mquickjs_throw_native_error(
        ctx, code, "wifi.csi", "Wi-Fi CSI operation failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

static esp_err_t wifi_csi_start_native(wifi_csi_session_t *session)
{
    esp32_mquickjs_wifi_radio_status_t radio_status;
    esp_err_t err;

    if (session == NULL || !session->radio_lease.acquired) {
        return ESP_ERR_INVALID_STATE;
    }
    err = esp32_mquickjs_wifi_radio_ensure_started(&session->radio_lease);
    if (err != ESP_OK) {
        session->last_error_stage = "wifi_radio_ensure_started";
        return err;
    }
    err = esp32_mquickjs_wifi_radio_get_status(&radio_status);
    if (err != ESP_OK) {
        session->last_error_stage = "wifi_radio_get_status";
        return err;
    }
    if (session->options.power_save_policy ==
            WIFI_CSI_POWER_SAVE_REQUIRE_NONE &&
        (!radio_status.power_save_available ||
         radio_status.power_save != WIFI_PS_NONE)) {
        session->last_error_stage = "power_save_policy";
        return ESP_ERR_INVALID_STATE;
    }
    session->effective_power_save = radio_status.power_save_available
        ? radio_status.power_save : WIFI_PS_NONE;
    if (session->options.fixed_channel) {
        err = esp32_mquickjs_wifi_radio_set_channel(
            &session->radio_lease, session->options.channel,
            WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) {
            session->last_error_stage = "wifi_radio_set_channel";
            return err;
        }
    }
    if (session->options.source == WIFI_CSI_SOURCE_PROMISCUOUS) {
        err = esp32_mquickjs_wifi_radio_acquire_promiscuous(
            &session->radio_lease, &session->promiscuous_lease);
        if (err != ESP_OK) {
            session->last_error_stage = "wifi_radio_acquire_promiscuous";
            if (!session->promiscuous_lease.acquired)
                esp32_mquickjs_wifi_radio_release_channel(&session->radio_lease);
            return err;
        }
    }
    uint32_t channel_generation;
    err = esp32_mquickjs_wifi_radio_get_channel(
        &session->effective_channel, &session->effective_secondary,
        &channel_generation);
    if (err != ESP_OK) {
        session->last_error_stage = "wifi_radio_get_channel";
        goto fail_radio_policy;
    }
    session->radio_generation = session->radio_lease.generation;
    atomic_store_explicit(&session->channel_conflicted, false, memory_order_release);
    err = esp_wifi_set_csi_rx_cb(wifi_csi_rx_callback, session);
    if (err != ESP_OK) {
        session->last_error_stage = "esp_wifi_set_csi_rx_cb";
        goto fail_radio_policy;
    }
    session->callback_registered = true;
    err = esp32_mquickjs_wifi_csi_target_apply_config(
        &session->options.capture);
    if (err != ESP_OK) {
        session->last_error_stage = "esp_wifi_set_csi_config";
        goto fail_callback;
    }
    esp32_mquickjs_wifi_csi_rx_native_enable(true); /* Header facts/filters also apply to packet.content=none. */
    esp32_mquickjs_wifi_csi_resources_set_accepting(
        session->resources, true);
    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        session->last_error_stage = "esp_wifi_set_csi";
        esp32_mquickjs_wifi_csi_resources_set_accepting(
            session->resources, false);
        goto fail_callback;
    }
    session->csi_enabled = true;
    session->last_error = ESP_OK;
    session->last_error_code = NULL;
    session->last_error_stage = NULL;
    atomic_store_explicit(&session->lifecycle, WIFI_CSI_RUNNING,
                          memory_order_release);
    return ESP_OK;

fail_callback:
    esp32_mquickjs_wifi_csi_rx_native_enable(false);
    if (esp_wifi_set_csi_rx_cb(NULL, NULL) == ESP_OK) {
        session->callback_registered = false;
    }
fail_radio_policy:
    if (!session->callback_registered) {
        esp32_mquickjs_wifi_radio_release_promiscuous(
            &session->promiscuous_lease);
        if (!session->promiscuous_lease.acquired)
            esp32_mquickjs_wifi_radio_release_channel(&session->radio_lease);
    }
    return err;
}

static const char *wifi_csi_start_error_code(
    const wifi_csi_session_t *session, esp_err_t err)
{
    const char *stage = session != NULL && session->last_error_stage != NULL
        ? session->last_error_stage : "";

    if (strcmp(stage, "power_save_policy") == 0) {
        return "WIFI_CSI_POWER_SAVE_CONFLICT";
    }
    if (session != NULL &&
        session->options.source == WIFI_CSI_SOURCE_PROMISCUOUS &&
        strcmp(stage, "wifi_radio_acquire_promiscuous") == 0) {
        return "WIFI_CSI_PROMISCUOUS_CONFLICT";
    }
    if (session != NULL && session->options.fixed_channel &&
        strcmp(stage, "wifi_radio_set_channel") == 0) {
        return err == ESP_ERR_NOT_ALLOWED || err == ESP_ERR_INVALID_ARG
            ? "WIFI_CSI_REGULATORY_CONFLICT"
            : "WIFI_CSI_RADIO_CONFLICT";
    }
    return "WIFI_CSI_DRIVER_ERROR";
}

static void wifi_csi_radio_cleanup_failed(wifi_csi_session_t *session, const char *stage)
{
    esp32_mquickjs_wifi_radio_status_t status;
    session->last_error = ESP_ERR_INVALID_STATE;
    session->last_error_stage = stage;
    if (esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK && status.cleanup_error != ESP_OK) {
        session->last_error = status.cleanup_error;
        if (status.cleanup_stage != NULL) session->last_error_stage = status.cleanup_stage;
    }
    atomic_store_explicit(&session->lifecycle, WIFI_CSI_FAULTED, memory_order_release);
}

static void wifi_csi_finish_stop(wifi_csi_session_t *session)
{
    if (session == NULL ||
        atomic_load_explicit(&session->resources->callbacks_active,
                             memory_order_acquire) != 0U ||
        session->csi_enabled || session->callback_registered) return;
    if (session->event_queue != NULL) {
        (void)esp32_mquickjs_event_queue_discard_all(session->event_queue);
    }
    esp32_mquickjs_wifi_radio_release_promiscuous(
        &session->promiscuous_lease);
    if (session->promiscuous_lease.acquired) {
        wifi_csi_radio_cleanup_failed(session, "wifi_radio_release_promiscuous");
        return;
    }
    esp32_mquickjs_wifi_radio_release_channel(&session->radio_lease);
    atomic_store_explicit(&session->lifecycle, WIFI_CSI_STOPPED,
                          memory_order_release);
}

static void wifi_csi_finish_close(wifi_csi_session_t *session)
{
    esp32_mquickjs_event_queue_t *queue;

    if (session == NULL || session->csi_enabled || session->promiscuous_lease.acquired ||
        atomic_load_explicit(&session->lifecycle, memory_order_acquire) != WIFI_CSI_STOPPED ||
        session->callback_registered ||
        atomic_load_explicit(&session->resources->callbacks_active,
                             memory_order_acquire) != 0U) return;
    esp32_mquickjs_wifi_radio_release(&session->radio_lease);
    if (session->radio_lease.acquired) {
        wifi_csi_radio_cleanup_failed(session, "wifi_radio_release");
        return;
    }
    queue = session->event_queue;
    session->event_queue = NULL;
    atomic_store_explicit(&session->lifecycle, WIFI_CSI_CLOSED,
                          memory_order_release);
    if (queue != NULL) {
        (void)esp32_mquickjs_event_queue_close(queue);
        (void)esp32_mquickjs_event_queue_discard_all(queue);
    }
    if (session->event_queue_retained && queue != NULL) {
        session->event_queue_retained = false;
        esp32_mquickjs_event_queue_release(queue);
    }
    wifi_csi_maybe_destroy_resources(session);
}

static void wifi_csi_cleanup_worker(void *opaque)
{
    wifi_csi_session_t *session = opaque;
    esp32_mquickjs_runtime_t *runtime;

    if (session == NULL) return;
    runtime = session->runtime;
    while (atomic_load_explicit(&session->resources->callbacks_active,
                                memory_order_acquire) != 0U) {
        vTaskDelay(1);
    }
    wifi_csi_finish_stop(session);
    if (atomic_load_explicit(&session->close_requested,
                             memory_order_acquire)) {
        wifi_csi_finish_close(session);
    }
    if (runtime != NULL) {
        esp32_mquickjs_notify_activity(runtime);
    }
    atomic_store_explicit(&session->cleanup_scheduled, false,
                          memory_order_release);
}

static bool wifi_csi_schedule_cleanup(wifi_csi_session_t *session)
{
    bool expected = false;

    if (session == NULL || !atomic_compare_exchange_strong_explicit(
            &session->cleanup_scheduled, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        return session != NULL && expected;
    }
    if (esp32_mquickjs_submit_background_worker(
            wifi_csi_cleanup_worker, session)) return true;
    atomic_store_explicit(&session->cleanup_scheduled, false,
                          memory_order_release);
    return false;
}

static esp_err_t wifi_csi_begin_stop(wifi_csi_session_t *session)
{
    wifi_csi_lifecycle_t lifecycle;
    esp_err_t result = ESP_OK;
    esp_err_t err;

    if (session == NULL) return ESP_ERR_INVALID_ARG;
    lifecycle = atomic_load_explicit(&session->lifecycle,
                                     memory_order_acquire);
    if (lifecycle == WIFI_CSI_CLOSED ||
        (lifecycle == WIFI_CSI_STOPPED && !session->promiscuous_lease.acquired &&
         !session->csi_enabled && !session->callback_registered) ||
        lifecycle == WIFI_CSI_STOPPING) return ESP_OK;
    atomic_store_explicit(&session->lifecycle, WIFI_CSI_STOPPING,
                          memory_order_release);
    esp32_mquickjs_wifi_csi_resources_set_accepting(
        session->resources, false);
    esp32_mquickjs_wifi_csi_rx_native_enable(false);
    if (session->csi_enabled) {
        err = esp_wifi_set_csi(false);
        if (err == ESP_OK) {
            session->csi_enabled = false;
        } else if (result == ESP_OK) {
            result = err;
            session->last_error_stage = "esp_wifi_set_csi(false)";
        }
    }
    if (session->callback_registered) {
        err = esp_wifi_set_csi_rx_cb(NULL, NULL);
        if (err == ESP_OK) {
            session->callback_registered = false;
        } else if (result == ESP_OK) {
            result = err;
            session->last_error_stage = "esp_wifi_set_csi_rx_cb(NULL)";
        }
    }
    if (result != ESP_OK) {
        session->last_error = result;
        atomic_store_explicit(&session->lifecycle, WIFI_CSI_FAULTED,
                              memory_order_release);
    }
    return result;
}

static bool wifi_csi_poll_stop(wifi_csi_session_t *session)
{
    if (session == NULL) return false;
    if (atomic_load_explicit(&session->lifecycle, memory_order_acquire) !=
            WIFI_CSI_STOPPING) return true;
    if (atomic_load_explicit(&session->resources->callbacks_active,
                             memory_order_acquire) == 0U) {
        wifi_csi_finish_stop(session);
        if (atomic_load_explicit(&session->close_requested,
                                 memory_order_acquire)) {
            wifi_csi_finish_close(session);
        }
        return true;
    }
    (void)wifi_csi_schedule_cleanup(session);
    return false;
}

static esp_err_t wifi_csi_stop_native(wifi_csi_session_t *session)
{
    esp_err_t err = wifi_csi_begin_stop(session);

    if (err != ESP_OK) return err;
    while (!wifi_csi_poll_stop(session) ||
           atomic_load_explicit(&session->cleanup_scheduled,
                                memory_order_acquire)) {
        if (session->runtime == NULL ||
            !esp32_mquickjs_cooperative_delay(session->runtime, 1U)) {
            vTaskDelay(1);
        }
    }
    return atomic_load_explicit(&session->lifecycle, memory_order_acquire) ==
                WIFI_CSI_FAULTED
        ? session->last_error : ESP_OK;
}

static bool wifi_csi_close_native(wifi_csi_session_t *session)
{
    esp_err_t err;

    if (session == NULL) return true;
    if (atomic_load_explicit(&session->lifecycle, memory_order_acquire) ==
            WIFI_CSI_CLOSED &&
        !atomic_load_explicit(&session->cleanup_scheduled,
                              memory_order_acquire)) return true;
    atomic_store_explicit(&session->close_requested, true,
                          memory_order_release);
    err = wifi_csi_stop_native(session);
    if (err != ESP_OK) return false;
    if (atomic_load_explicit(&session->lifecycle, memory_order_acquire) !=
            WIFI_CSI_CLOSED) {
        wifi_csi_finish_close(session);
    }
    return atomic_load_explicit(&session->lifecycle, memory_order_acquire) ==
        WIFI_CSI_CLOSED;
}

static bool wifi_csi_reap(void *opaque)
{
    wifi_csi_session_t *session = &s_wifi_csi;
    if ((uint32_t)(uintptr_t)opaque != session->generation) return true;
    wifi_csi_lifecycle_t lifecycle;

    if (session == NULL) return true;
    atomic_store_explicit(&session->close_requested, true,
                          memory_order_release);
    if (wifi_csi_begin_stop(session) != ESP_OK) return false;
    (void)wifi_csi_poll_stop(session);
    lifecycle = atomic_load_explicit(&session->lifecycle,
                                     memory_order_acquire);
    if (lifecycle == WIFI_CSI_STOPPED &&
        !atomic_load_explicit(&session->cleanup_scheduled,
                              memory_order_acquire)) {
        wifi_csi_finish_close(session);
        lifecycle = atomic_load_explicit(&session->lifecycle,
                                         memory_order_acquire);
    }
    if (lifecycle == WIFI_CSI_CLOSED &&
        !atomic_load_explicit(&session->cleanup_scheduled,
                              memory_order_acquire)) {
        session->reaper_registered = false;
        return true;
    }
    return false;
}

static void wifi_csi_request_reap(wifi_csi_session_t *session)
{
    if (session == NULL || session->runtime == NULL ||
        atomic_load_explicit(&session->lifecycle,
                             memory_order_acquire) == WIFI_CSI_CLOSED) return;
    if (!session->reaper_registered) {
        session->reaper_registered = esp32_mquickjs_register_reaper(
            session->runtime, wifi_csi_reap, (void *)(uintptr_t)session->generation);
        if (!session->reaper_registered) {
            ESP_LOGE(TAG, "Wi-Fi CSI cleanup registry is full");
        }
    }
    esp32_mquickjs_notify_activity(session->runtime);
}

static JSValue wifi_csi_string_array(JSContext *ctx,
                                     const char *const *values,
                                     size_t count)
{
    JSGCRef array_ref;
    JSGCRef item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    size_t index;

    *array = JS_NewArray(ctx, 0);
    *item = JS_UNDEFINED;
    if (JS_IsException(*array)) {
        JS_PopGCRef(ctx, &item_ref);
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }
    for (index = 0; index < count; ++index) {
        *item = JS_NewString(ctx, values[index]);
        if (JS_IsException(*item) ||
            JS_IsException(JS_SetPropertyUint32(
                ctx, *array, (uint32_t)index, *item))) {
            JS_PopGCRef(ctx, &item_ref);
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
        *item = JS_UNDEFINED;
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
}

static JSValue wifi_csi_24ghz_channels_to_js(
    JSContext *ctx, const wifi_country_t *country)
{
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint16_t channel;
    uint16_t end;
    uint32_t index = 0U;

    *array = JS_NewArray(ctx, 0);
    *item = JS_UNDEFINED;
    if (JS_IsException(*array)) goto fail;
    end = (uint16_t)country->schan + country->nchan;
    for (channel = country->schan; channel < end && channel <= 14U;
         ++channel) {
        *item = JS_NewUint32(ctx, channel);
        if (JS_IsException(*item) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *array, index++, *item)))
            goto fail;
        *item = JS_UNDEFINED;
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);

fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

#if CONFIG_SOC_WIFI_SUPPORT_5G
static JSValue wifi_csi_5ghz_channels_to_js(
    JSContext *ctx, uint32_t channel_mask)
{
    static const uint8_t channels[] = {
        36U, 40U, 44U, 48U, 52U, 56U, 60U, 64U,
        100U, 104U, 108U, 112U, 116U, 120U, 124U, 128U,
        132U, 136U, 140U, 144U, 149U, 153U, 157U, 161U,
        165U, 169U, 173U, 177U,
    };
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint32_t input_index;
    uint32_t output_index = 0U;

    *array = JS_NewArray(ctx, 0);
    *item = JS_UNDEFINED;
    if (JS_IsException(*array)) goto fail;
    for (input_index = 0U; input_index < sizeof(channels); ++input_index) {
        if ((channel_mask & (1UL << (input_index + 1U))) == 0U) continue;
        *item = JS_NewUint32(ctx, channels[input_index]);
        if (JS_IsException(*item) ||
            JS_IsException(JS_SetPropertyUint32(
                ctx, *array, output_index++, *item))) goto fail;
        *item = JS_UNDEFINED;
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);

fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}
#endif

static JSValue wifi_csi_radio_capabilities_to_js(JSContext *ctx)
{
    JSGCRef result_ref, bands_ref, band_ref, allowed_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *bands = JS_PushGCRef(ctx, &bands_ref);
    JSValue *band = JS_PushGCRef(ctx, &band_ref);
    JSValue *allowed = JS_PushGCRef(ctx, &allowed_ref);
    wifi_country_t country = {0};
    bool country_available = esp_wifi_get_country(&country) == ESP_OK;
    char country_code[4] = {0};
    const char *policy = NULL;
    uint32_t band_index = 0U;

    *result = JS_NewObject(ctx);
    *bands = JS_NewArray(ctx, 0);
    *band = JS_UNDEFINED;
    *allowed = JS_UNDEFINED;
    if (JS_IsException(*result) || JS_IsException(*bands)) goto fail;
    if (country_available) {
        memcpy(country_code, country.cc, sizeof(country.cc));
        if (country.policy == WIFI_COUNTRY_POLICY_AUTO) policy = "auto";
        if (country.policy == WIFI_COUNTRY_POLICY_MANUAL) policy = "manual";
    }
    *band = JS_NewObject(ctx);
    *allowed = country_available
        ? wifi_csi_24ghz_channels_to_js(ctx, &country) : JS_NULL;
    if (JS_IsException(*band) || JS_IsException(*allowed) ||
        !esp32_mquickjs_set_property_ref(
            ctx, band, "band", JS_NewString(ctx, "2.4GHz")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, band, "allowedChannels", *allowed) ||
        JS_IsException(JS_SetPropertyUint32(
            ctx, *bands, band_index++, *band))) goto fail;
    *allowed = JS_UNDEFINED;
    *band = JS_UNDEFINED;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    *band = JS_NewObject(ctx);
    *allowed = country_available &&
            country.policy == WIFI_COUNTRY_POLICY_MANUAL &&
            country.wifi_5g_channel_mask != 0U
        ? wifi_csi_5ghz_channels_to_js(
            ctx, country.wifi_5g_channel_mask) : JS_NULL;
    if (JS_IsException(*band) || JS_IsException(*allowed) ||
        !esp32_mquickjs_set_property_ref(
            ctx, band, "band", JS_NewString(ctx, "5GHz")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, band, "allowedChannels", *allowed) ||
        JS_IsException(JS_SetPropertyUint32(
            ctx, *bands, band_index++, *band))) goto fail;
    *allowed = JS_UNDEFINED;
    *band = JS_UNDEFINED;
#endif
    if (!esp32_mquickjs_set_property_ref(
            ctx, result, "country", country_available
                ? JS_NewString(ctx, country_code) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "policy", policy != NULL
                ? JS_NewString(ctx, policy) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "bands", *bands))
        goto fail;
    *bands = JS_UNDEFINED;
    JS_PopGCRef(ctx, &allowed_ref);
    JS_PopGCRef(ctx, &band_ref);
    JS_PopGCRef(ctx, &bands_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    JS_PopGCRef(ctx, &allowed_ref);
    JS_PopGCRef(ctx, &band_ref);
    JS_PopGCRef(ctx, &bands_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_csi_mac_array(JSContext *ctx,
                                  const uint8_t values[][6],
                                  uint8_t count)
{
    JSGCRef array_ref;
    JSGCRef item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint8_t index;

    *array = JS_NewArray(ctx, 0);
    *item = JS_UNDEFINED;
    if (JS_IsException(*array)) {
        JS_PopGCRef(ctx, &item_ref);
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }
    for (index = 0; index < count; ++index) {
        char text[18];

        wifi_csi_format_mac(values[index], text);
        *item = JS_NewString(ctx, text);
        if (JS_IsException(*item) ||
            JS_IsException(JS_SetPropertyUint32(
                ctx, *array, index, *item))) {
            JS_PopGCRef(ctx, &item_ref);
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
        *item = JS_UNDEFINED;
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
}

static JSValue wifi_csi_capture_to_js(JSContext *ctx,
                                      const wifi_csi_options_t *options)
{
    JSGCRef object_ref;
    JSGCRef scale_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *scale = JS_PushGCRef(ctx, &scale_ref);
    bool ok;

    *object = JS_NewObject(ctx);
    *scale = JS_UNDEFINED;
    if (JS_IsException(*object)) goto fail;
    if (options->capture.schema == ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY) {
        const esp32_mquickjs_wifi_csi_legacy_config_t *config =
            &options->capture.config.legacy;

        if (config->manual_scale) {
            *scale = JS_NewObject(ctx);
            if (JS_IsException(*scale) ||
                !esp32_mquickjs_set_property_ref(
                    ctx, scale, "shiftBits",
                    JS_NewUint32(ctx, config->shift_bits))) goto fail;
        } else {
            *scale = JS_NewString(ctx, "auto");
        }
        ok = esp32_mquickjs_set_property_ref(
                 ctx, object, "schema",
                 JS_NewString(ctx, "wifi-csi-legacy/1")) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "lltf", JS_NewBool(config->lltf)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "htLtf", JS_NewBool(config->ht_ltf)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "stbcHtLtf2",
                 JS_NewBool(config->stbc_ht_ltf2)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "ltfMerge", JS_NewBool(config->ltf_merge)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "adjacentSubcarrierFilter",
                 JS_NewBool(config->adjacent_subcarrier_filter)) &&
             esp32_mquickjs_set_property_ref(ctx, object, "scale", *scale) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "dumpAck", JS_NewBool(config->dump_ack));
        *scale = JS_UNDEFINED;
    } else {
        const esp32_mquickjs_wifi_csi_he_config_t *config =
            &options->capture.config.he;
        const char *stbc = config->he_stbc_ltf ==
                ESP32_MQUICKJS_WIFI_CSI_HE_STBC_SECOND
            ? "second" : config->he_stbc_ltf ==
                    ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE
                ? "alternate" : "first";

        ok = esp32_mquickjs_set_property_ref(
                 ctx, object, "schema",
                 JS_NewString(ctx, "wifi-csi-he/1")) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "enableLegacy",
                 JS_NewBool(config->enable_legacy)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "forceLegacyLtf",
                 JS_NewBool(config->force_legacy_ltf)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "ht20", JS_NewBool(config->ht20)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "ht40", JS_NewBool(config->ht40)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "vht", JS_NewBool(config->vht)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "heSu", JS_NewBool(config->he_su)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "heMu", JS_NewBool(config->he_mu)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "heDcm", JS_NewBool(config->he_dcm)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "heBeamformed",
                 JS_NewBool(config->he_beamformed)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "heStbcLtf", JS_NewString(ctx, stbc)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "valueScale",
                 JS_NewUint32(ctx, config->value_scale)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "dumpAck", JS_NewBool(config->dump_ack)) &&
             esp32_mquickjs_set_property_ref(
                 ctx, object, "lltfBits",
                 JS_NewUint32(ctx, config->lltf_bits));
    }
    if (!ok) goto fail;
    JS_PopGCRef(ctx, &scale_ref);
    return JS_PopGCRef(ctx, &object_ref);

fail:
    JS_PopGCRef(ctx, &scale_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_csi_source_to_js(JSContext *ctx, const wifi_csi_options_t *options)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "mode",
            JS_NewString(ctx, wifi_csi_source_name(options->source)))) goto fail;
    if (options->source == WIFI_CSI_SOURCE_PROMISCUOUS &&
        !esp32_mquickjs_set_property_ref(ctx, result, "channel", options->fixed_channel
            ? JS_NewUint32(ctx, options->channel) : JS_NewString(ctx, "current"))) goto fail;
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

static JSValue wifi_csi_requested_to_js(JSContext *ctx,
                                        const wifi_csi_options_t *options)
{
    JSGCRef result_ref, capture_ref, filter_ref, queue_ref;
    JSGCRef source_macs_ref, destination_macs_ref, source_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *capture = JS_PushGCRef(ctx, &capture_ref);
    JSValue *filter = JS_PushGCRef(ctx, &filter_ref);
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    JSValue *source_macs = JS_PushGCRef(ctx, &source_macs_ref);
    JSValue *destination_macs = JS_PushGCRef(ctx, &destination_macs_ref);
    JSValue *source = JS_PushGCRef(ctx, &source_ref);

    *result = JS_NewObject(ctx);
    *capture = wifi_csi_capture_to_js(ctx, options);
    *filter = JS_NewObject(ctx);
    *queue = JS_NewObject(ctx);
    *source_macs = wifi_csi_mac_array(
        ctx, options->filter.source_macs, options->filter.source_mac_count);
    *destination_macs = wifi_csi_mac_array(
        ctx, options->filter.destination_macs,
        options->filter.destination_mac_count);
    *source = wifi_csi_source_to_js(ctx, options);
    if (JS_IsException(*result) || JS_IsException(*capture) ||
        JS_IsException(*filter) || JS_IsException(*queue) ||
        JS_IsException(*source_macs) || JS_IsException(*destination_macs) || JS_IsException(*source) ||
        !esp32_mquickjs_set_property_ref(
            ctx, filter, "sourceMac", *source_macs) ||
        !esp32_mquickjs_set_property_ref(
            ctx, filter, "destinationMac", *destination_macs) ||
        !esp32_mquickjs_set_property_ref(ctx, filter, "bssid",
            wifi_csi_mac_array(ctx, options->filter.bssids, options->filter.bssid_count)) ||
        (options->filter.frame_types_set && !esp32_mquickjs_set_property_ref(ctx, filter, "frameTypes",
            wifi_csi_frame_filter_to_js(ctx, options->filter.frame_types, false))) ||
        (options->filter.frame_subtypes_set && !esp32_mquickjs_set_property_ref(ctx, filter, "frameSubtypes",
            wifi_csi_frame_filter_to_js(ctx, options->filter.frame_subtypes, true))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, filter, "minimumRssi",
            options->filter.minimum_rssi_set
                ? JS_NewInt32(ctx, options->filter.minimum_rssi) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, filter, "sampleEvery",
            JS_NewUint32(ctx, options->filter.sample_every)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, filter, "maximumRateHz",
            options->filter.maximum_rate_hz > 0U
                ? JS_NewUint32(ctx, options->filter.maximum_rate_hz) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, filter, "validOnly",
            JS_NewBool(options->filter.valid_only)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, queue, "poolCapacity",
            JS_NewUint32(ctx, options->pool_capacity)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, queue, "queueCapacity",
            JS_NewUint32(ctx, options->queue_capacity)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, queue, "overflow", JS_NewString(ctx, "drop-newest")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "source", *source) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "capture", *capture) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "filter", *filter) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "buffering", *queue) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "packet", wifi_csi_packet_options_to_js(ctx, &options->packet)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "powerSavePolicy",
            JS_NewString(ctx,
                options->power_save_policy == WIFI_CSI_POWER_SAVE_REQUIRE_NONE
                    ? "require-none" : "preserve"))) goto fail;
    *capture = JS_UNDEFINED;
    *filter = JS_UNDEFINED;
    *queue = JS_UNDEFINED;
    *source_macs = JS_UNDEFINED;
    *destination_macs = JS_UNDEFINED;
    JS_PopGCRef(ctx, &source_ref);
    JS_PopGCRef(ctx, &destination_macs_ref);
    JS_PopGCRef(ctx, &source_macs_ref);
    JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &filter_ref);
    JS_PopGCRef(ctx, &capture_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    JS_PopGCRef(ctx, &source_ref);
    JS_PopGCRef(ctx, &destination_macs_ref);
    JS_PopGCRef(ctx, &source_macs_ref);
    JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &filter_ref);
    JS_PopGCRef(ctx, &capture_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static const char *wifi_csi_sample_encoding_name(
    esp32_mquickjs_wifi_csi_sample_encoding_t encoding)
{
    switch (encoding) {
        case ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8:
            return "signed-int8";
        case ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT12_LE:
            return "signed-int12-le";
        case ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT12_PACKED:
            return "signed-int12-packed";
        default:
            return "unknown";
    }
}

static const char *wifi_csi_layout_schema_name(
    esp32_mquickjs_wifi_csi_layout_schema_t schema)
{
    switch (schema) {
        case ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_LEGACY:
            return "wifi-csi-legacy-layout/1";
        case ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_HE:
            return "wifi-csi-he-layout/1";
        default:
            return "unknown";
    }
}

static const char *wifi_csi_segment_type_name(
    esp32_mquickjs_wifi_csi_segment_type_t type)
{
    switch (type) {
        case ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF: return "lltf";
        case ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HT_LTF: return "ht-ltf";
        case ESP32_MQUICKJS_WIFI_CSI_SEGMENT_STBC_HT_LTF2:
            return "stbc-ht-ltf2";
        case ESP32_MQUICKJS_WIFI_CSI_SEGMENT_VHT_LTF: return "vht-ltf";
        case ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HE_LTF1: return "he-ltf1";
        case ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HE_LTF2: return "he-ltf2";
        case ESP32_MQUICKJS_WIFI_CSI_SEGMENT_MIXED: return "mixed";
        default: return "unknown";
    }
}

static JSValue wifi_csi_layout_to_js(
    JSContext *ctx, const esp32_mquickjs_wifi_csi_layout_t *native_layout)
{
    JSGCRef layout_ref, segments_ref, segment_ref, ranges_ref;
    JSGCRef range_ref, nulls_ref, item_ref;
    JSValue *layout = JS_PushGCRef(ctx, &layout_ref);
    JSValue *segments = JS_PushGCRef(ctx, &segments_ref);
    JSValue *segment = JS_PushGCRef(ctx, &segment_ref);
    JSValue *ranges = JS_PushGCRef(ctx, &ranges_ref);
    JSValue *range = JS_PushGCRef(ctx, &range_ref);
    JSValue *nulls = JS_PushGCRef(ctx, &nulls_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint8_t segment_index;

    *layout = JS_NewObject(ctx);
    *segments = JS_NewArray(ctx, native_layout->segment_count);
    *segment = JS_UNDEFINED;
    *ranges = JS_UNDEFINED;
    *range = JS_UNDEFINED;
    *nulls = JS_UNDEFINED;
    *item = JS_UNDEFINED;
    if (JS_IsException(*layout) || JS_IsException(*segments)) goto fail;
    for (segment_index = 0U;
         segment_index < native_layout->segment_count;
         ++segment_index) {
        const esp32_mquickjs_wifi_csi_segment_t *native_segment =
            &native_layout->segments[segment_index];
        uint8_t range_index;
        uint8_t null_index;

        *segment = JS_NewObject(ctx);
        *ranges = JS_NewArray(ctx, native_segment->subcarrier_range_count);
        *nulls = JS_NewArray(ctx, native_segment->null_subcarrier_count);
        if (JS_IsException(*segment) || JS_IsException(*ranges) ||
            JS_IsException(*nulls)) goto fail;
        for (range_index = 0U;
             range_index < native_segment->subcarrier_range_count;
             ++range_index) {
            *range = JS_NewObject(ctx);
            if (JS_IsException(*range) ||
                !esp32_mquickjs_set_property_ref(
                    ctx, range, "start",
                    JS_NewInt32(ctx,
                        native_segment->subcarrier_ranges[range_index].start)) ||
                !esp32_mquickjs_set_property_ref(
                    ctx, range, "end",
                    JS_NewInt32(ctx,
                        native_segment->subcarrier_ranges[range_index].end)) ||
                JS_IsException(JS_SetPropertyUint32(
                    ctx, *ranges, range_index, *range))) goto fail;
            *range = JS_UNDEFINED;
        }
        for (null_index = 0U;
             null_index < native_segment->null_subcarrier_count;
             ++null_index) {
            *item = JS_NewInt32(
                ctx, native_segment->null_subcarriers[null_index]);
            if (JS_IsException(*item) ||
                JS_IsException(JS_SetPropertyUint32(
                    ctx, *nulls, null_index, *item))) goto fail;
            *item = JS_UNDEFINED;
        }
        if (!esp32_mquickjs_set_property_ref(
                ctx, segment, "type", JS_NewString(
                    ctx, wifi_csi_segment_type_name(native_segment->type))) ||
            !esp32_mquickjs_set_property_ref(
                ctx, segment, "offsetBytes",
                JS_NewUint32(ctx, native_segment->offset_bytes)) ||
            !esp32_mquickjs_set_property_ref(
                ctx, segment, "lengthBytes",
                JS_NewUint32(ctx, native_segment->length_bytes)) ||
            !esp32_mquickjs_set_property_ref(
                ctx, segment, "iqPairCount",
                JS_NewUint32(ctx, native_segment->iq_pair_count)) ||
            !esp32_mquickjs_set_property_ref(
                ctx, segment, "subcarrierRanges", *ranges) ||
            !esp32_mquickjs_set_property_ref(
                ctx, segment, "nullSubcarriers", *nulls) ||
            JS_IsException(JS_SetPropertyUint32(
                ctx, *segments, segment_index, *segment))) goto fail;
        *ranges = JS_UNDEFINED;
        *nulls = JS_UNDEFINED;
        *segment = JS_UNDEFINED;
    }
    if (!esp32_mquickjs_set_property_ref(
            ctx, layout, "schema", JS_NewString(
                ctx, wifi_csi_layout_schema_name(native_layout->schema))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, layout, "componentOrder",
            JS_NewString(ctx, "imaginary-real")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, layout, "sampleEncoding", JS_NewString(
                ctx, wifi_csi_sample_encoding_name(
                    native_layout->sample_encoding))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, layout, "sampleBits", native_layout->sample_bits > 0U
                ? JS_NewUint32(ctx, native_layout->sample_bits) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, layout, "byteLength",
            JS_NewUint32(ctx, native_layout->byte_length)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, layout, "iqPairCount",
            JS_NewUint32(ctx, native_layout->iq_pair_count)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, layout, "trailingPaddingBytes",
            JS_NewUint32(ctx, native_layout->trailing_padding_bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx, layout, "segments", *segments))
        goto fail;
    *segments = JS_UNDEFINED;
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &nulls_ref);
    JS_PopGCRef(ctx, &range_ref);
    JS_PopGCRef(ctx, &ranges_ref);
    JS_PopGCRef(ctx, &segment_ref);
    JS_PopGCRef(ctx, &segments_ref);
    return JS_PopGCRef(ctx, &layout_ref);

fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &nulls_ref);
    JS_PopGCRef(ctx, &range_ref);
    JS_PopGCRef(ctx, &ranges_ref);
    JS_PopGCRef(ctx, &segment_ref);
    JS_PopGCRef(ctx, &segments_ref);
    JS_PopGCRef(ctx, &layout_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_csi_packet_to_js(JSContext *ctx, const esp32_mquickjs_wifi_csi_packet_t *packet)
{
    if (packet == NULL || packet->length == 0) return JS_NULL;
    const esp32_mquickjs_wifi_rx_header_t *header = &packet->header;
    static const char *const types[] = {"management", "control", "data", "misc", "unknown"};
    static const char *const flags[] = {"toDs", "fromDs", "moreFragments", "retry",
        "powerManagement", "moreData", "protected", "order"};
    JSGCRef result_ref, child_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *child = JS_PushGCRef(ctx, &child_ref);
    *result = JS_NewObject(ctx);
    *child = JS_NewObject(ctx);
#define PACKET_SET(object, key, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, key, value)) goto fail; } while (0)
#define PACKET_NUMBER(object, key, value) PACKET_SET(object, key, JS_NewUint32(ctx, value))
    if (JS_IsException(*result) || JS_IsException(*child)) goto fail;
    PACKET_SET(result, "type", JS_NewString(ctx, types[(unsigned)header->type < 5 ? header->type : 4]));
    PACKET_NUMBER(result, "subtype", header->subtype);
    const char *name = esp32_mquickjs_wifi_rx_subtype_name(header->type, header->subtype);
    PACKET_SET(result, "subtypeName", name != NULL ? JS_NewString(ctx, name) : JS_NULL);
    PACKET_NUMBER(result, "frameControl", header->frame_control);
    PACKET_NUMBER(result, "durationId", header->duration_id);
    PACKET_SET(result, "sequenceControl", header->sequence_valid ? JS_NewUint32(ctx, header->sequence_control) : JS_NULL);
    PACKET_SET(result, "qosControl", header->qos_valid ? JS_NewUint32(ctx, header->qos_control) : JS_NULL);
    for (unsigned i = 0; i < 8; ++i)
        PACKET_SET(child, flags[i], JS_NewBool((header->frame_control & (1U << (8 + i))) != 0));
    PACKET_SET(result, "flags", *child);
    *child = JS_NewObject(ctx);
    if (JS_IsException(*child)) goto fail;
    PACKET_SET(child, "mode", JS_NewString(ctx, wifi_csi_packet_mode_name(packet->mode)));
    PACKET_NUMBER(child, "headerLength", header->header_length);
    PACKET_NUMBER(child, "payloadLength", packet->readable_length - header->header_length);
    PACKET_NUMBER(child, "driverLength", packet->driver_packet_length);
    PACKET_NUMBER(child, "driverPayloadLength", packet->driver_payload_length);
    PACKET_NUMBER(child, "readableLength", packet->readable_length);
    PACKET_NUMBER(child, "capturedLength", packet->length);
    PACKET_NUMBER(child, "payloadCapturedLength", packet->length - header->header_length);
    PACKET_SET(child, "truncated", JS_NewBool(packet->truncated));
    PACKET_SET(child, "fcs", JS_NewString(ctx, "unknown"));
    PACKET_SET(child, "payloadRepresentation", JS_NewString(ctx, "unknown"));
    PACKET_SET(child, "pointerLayoutValid", JS_TRUE);
    PACKET_SET(child, "parseValid", JS_TRUE);
    PACKET_SET(result, "capture", *child);
    JS_PopGCRef(ctx, &child_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &child_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
#undef PACKET_NUMBER
#undef PACKET_SET
}

static JSValue wifi_csi_frame_info_to_js(
    JSContext *ctx, const esp32_mquickjs_wifi_csi_slot_t *slot)
{
    const esp32_mquickjs_wifi_csi_metadata_t *m = &slot->metadata;
    JSGCRef result_ref, child_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *child = JS_PushGCRef(ctx, &child_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
#define INFO_SET(object, key, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, key, value)) goto fail; } while (0)
#define INFO_NUMBER(object, key, value) INFO_SET(object, key, JS_NewUint32(ctx, value))
#define INFO_CHILD() do { *child = JS_NewObject(ctx); if (JS_IsException(*child)) goto fail; } while (0)
#define PHY_BOOL(key, available, value) INFO_SET(child, key, (m->phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_##available) \
    ? JS_NewBool((m->phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_##value) != 0) : JS_NULL)
    INFO_NUMBER(result, "sequence", slot->sequence);
    INFO_NUMBER(result, "generation", slot->session_generation);
    INFO_NUMBER(result, "radioGeneration", m->radio_generation);
    INFO_SET(result, "timestampUs", JS_NewInt64(ctx, (int64_t)m->timestamp_us));
    INFO_SET(result, "timestampAccuracy", JS_NewString(ctx, "callback-time"));
    INFO_SET(result, "rxSequence", m->rx_sequence != UINT32_MAX ? JS_NewUint32(ctx, m->rx_sequence) : JS_NULL);
    INFO_CHILD();
    INFO_SET(child, "rssi", JS_NewInt32(ctx, m->rssi));
    INFO_SET(child, "noiseFloor", m->noise_floor_available ? JS_NewInt32(ctx, m->noise_floor) : JS_NULL);
    INFO_SET(child, "antenna", m->antenna_available ? JS_NewUint32(ctx, m->antenna) : JS_NULL);
    INFO_SET(result, "signal", *child);
    INFO_CHILD();
    INFO_SET(child, "band", m->channel >= 1 && m->channel <= 14 ? JS_NewString(ctx, "2.4GHz") :
        m->channel >= 32 && m->channel <= 177 ? JS_NewString(ctx, "5GHz") : JS_NULL);
    INFO_NUMBER(child, "primary", m->channel);
    INFO_SET(child, "secondary", (unsigned)m->secondary <= ESP32_MQUICKJS_WIFI_CSI_SECONDARY_BELOW
        ? JS_NewString(ctx, wifi_csi_secondary_name(m->secondary)) : JS_NULL);
    INFO_SET(result, "channel", *child);
    INFO_CHILD();
    INFO_SET(child, "format", JS_NewString(ctx, wifi_csi_phy_name(m->phy)));
    INFO_SET(child, "bandwidthMHz", m->bandwidth_available ? JS_NewUint32(ctx, m->bandwidth_mhz) : JS_NULL);
    INFO_SET(child, "guardIntervalNs", m->guard_interval_ns ? JS_NewUint32(ctx, m->guard_interval_ns) : JS_NULL);
    INFO_SET(child, "heLtfSize", m->he_ltf_size ? JS_NewUint32(ctx, m->he_ltf_size) : JS_NULL);
    INFO_SET(child, "dcm", m->dcm_state ? JS_NewBool(m->dcm_state == 2) : JS_NULL);
    INFO_SET(child, "mcs", m->mcs_available ? JS_NewUint32(ctx, m->mcs) : JS_NULL);
    INFO_SET(child, "legacyRate", JS_NULL); /* Raw rate codes have no proven bitrate mapping. */
    INFO_SET(child, "stbc", m->stbc_available ? JS_NewBool(m->stbc) : JS_NULL);
    PHY_BOOL("shortGuardInterval", SGI_AVAILABLE, SGI);
    PHY_BOOL("aggregation", AGGREGATION_AVAILABLE, AGGREGATION);
    PHY_BOOL("smoothingRecommended", SMOOTHING_AVAILABLE, SMOOTHING);
    PHY_BOOL("sounding", SOUNDING_AVAILABLE, SOUNDING);
    INFO_SET(child, "fecCoding", (m->phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_FEC_AVAILABLE)
        ? JS_NewString(ctx, (m->phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_LDPC) ? "ldpc" : "bcc") : JS_NULL);
    INFO_SET(child, "ampduCount", m->ampdu_count_available ? JS_NewUint32(ctx, m->ampdu_count) : JS_NULL);
    INFO_SET(result, "phy", *child);
    INFO_CHILD();
    static const char *const roles[] = {"source", "destination", "transmitter", "receiver", "bssid"};
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RX_ADDRESS_COUNT; ++i) {
        char address[18];
        wifi_csi_format_mac(m->addresses[i], address);
        INFO_SET(child, roles[i], (m->address_mask & (1U << i)) ? JS_NewString(ctx, address) : JS_NULL);
    }
    INFO_SET(result, "addresses", *child);
    INFO_CHILD();
    INFO_SET(child, "firstWordInvalid", JS_NewBool(m->first_word_invalid));
    INFO_SET(child, "channelEstimateValid", m->channel_estimate_valid_available ? JS_NewBool(m->channel_estimate_valid) : JS_NULL);
    INFO_SET(child, "callbackDataValid", JS_TRUE);
    INFO_SET(child, "layoutKnown", JS_NewBool(m->layout.known));
    INFO_SET(result, "validity", *child);
    INFO_SET(result, "layout", wifi_csi_layout_to_js(ctx, &m->layout));
    INFO_SET(result, "packet", wifi_csi_packet_to_js(ctx, slot->packet));
    JS_PopGCRef(ctx, &child_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &child_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
#undef PHY_BOOL
#undef INFO_CHILD
#undef INFO_NUMBER
#undef INFO_SET
}

static JSValue wifi_csi_make_frame(
    JSContext *ctx, const esp32_mquickjs_wifi_csi_event_t *event)
{
    esp32_mquickjs_wifi_csi_slot_t *slot = wifi_csi_resolve_event(event);
    wifi_csi_frame_ref_t *ref;
    JSGCRef object_ref, info_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *info = JS_PushGCRef(ctx, &info_ref);

    *object = JS_UNDEFINED;
    *info = JS_UNDEFINED;
    if (slot == NULL) {
        JS_ThrowReferenceError(ctx,
            "WIFI_CSI_STALE_FRAME: receive event is stale");
        goto fail;
    }
    if (!wifi_csi_update_event(event, WIFI_CSI_EVENT_TAKE)) {
        JS_ThrowReferenceError(ctx,
            "WIFI_CSI_STALE_FRAME: receive event owner was already consumed");
        goto fail;
    }
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_CSI_FRAME);
    if (JS_IsException(*object)) goto fail_close;
    ref = esp32_mquickjs_memory_wireless_calloc("wifi.csi", 1, sizeof(*ref), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail_close;
    }
    ref->session_generation = event->session_generation;
    ref->slot_index = event->slot_index;
    ref->slot_generation = event->slot_generation;
    JS_SetOpaque(ctx, *object, ref);
    *info = wifi_csi_frame_info_to_js(ctx, slot);
    if (JS_IsException(*info) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "info", *info)) {
        JS_SetOpaque(ctx, *object, NULL);
        esp32_mquickjs_memory_payload_free(ref);
        goto fail_close;
    }
    *info = JS_UNDEFINED;
    (void)wifi_csi_update_event(event, WIFI_CSI_EVENT_DELIVERED);
    JS_PopGCRef(ctx, &info_ref);
    return JS_PopGCRef(ctx, &object_ref);

fail_close:
    {
        wifi_csi_lease_ref_t lease = {.event = *event};
        wifi_csi_lease_request_close(&lease);
    }
fail:
    JS_PopGCRef(ctx, &info_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_csi_event_to_js(JSContext *ctx,
                                    const void *event,
                                    void *opaque)
{
    (void)opaque;
    return wifi_csi_make_frame(ctx, event);
}

static bool wifi_csi_sample_source_next(
    JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out)
{
    wifi_csi_sample_source_t *source = opaque;
    esp32_mquickjs_wifi_csi_slot_t *slot;
    (void)ctx;
    if (source == NULL || source->lease.released ||
        (slot = wifi_csi_resolve_event(&source->lease.event)) == NULL) return false;
    size_t length = source->packet ? (slot->packet != NULL ? slot->packet->length : 0) : slot->length;
    if (source->offset >= length) return false;
    const uint8_t *bytes = source->packet ? slot->packet->bytes : slot->payload;
    out->data = bytes + source->offset;
    out->length = length - source->offset;
    out->owner = JS_UNDEFINED;
    out->dma_capable = false;
    source->offset = length;
    return true;
}

static void wifi_csi_sample_source_iterator_close(JSContext *ctx, void *opaque)
{
    wifi_csi_sample_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) return;
    source->iterator_active = false;
    wifi_csi_lease_release(&source->lease);
    if (source->destroy_requested) esp32_mquickjs_memory_payload_free(source);
}

static bool wifi_csi_sample_source_open(
    JSContext *ctx, JSValue source_value, void *opaque,
    esp32_mquickjs_byte_span_source_t *out, JSValue *out_error)
{
    wifi_csi_sample_source_t *source = opaque;

    (void)source_value;
    if (source == NULL || source->opened || source->lease.released ||
        wifi_csi_resolve_event(&source->lease.event) == NULL) {
        *out_error = JS_ThrowReferenceError(
            ctx, "WIFI_CSI_STALE_FRAME: frame source is closed or consumed");
        return false;
    }
    source->opened = true;
    source->iterator_active = true;
    out->opaque = source;
    out->next = wifi_csi_sample_source_next;
    out->close = wifi_csi_sample_source_iterator_close;
    return true;
}

static size_t wifi_csi_sample_source_known_length(void *opaque)
{
    wifi_csi_sample_source_t *source = opaque;
    esp32_mquickjs_wifi_csi_slot_t *slot = source != NULL
        ? wifi_csi_resolve_event(&source->lease.event) : NULL;

    return slot != NULL && !source->lease.released
        ? (source->packet ? (slot->packet != NULL ? slot->packet->length : 0) : slot->length) : 0U;
}

static void wifi_csi_sample_source_destroy(JSContext *ctx, void *opaque)
{
    wifi_csi_sample_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) return;
    if (source->iterator_active) {
        source->destroy_requested = true;
        return;
    }
    wifi_csi_lease_release(&source->lease);
    esp32_mquickjs_memory_payload_free(source);
}

static const esp32_mquickjs_byte_span_source_object_ops_t
    s_wifi_csi_sample_source_ops = {
        .class_id = JS_CLASS_BYTE_SPAN_SOURCE,
        .open = wifi_csi_sample_source_open,
        .known_length = wifi_csi_sample_source_known_length,
        .destroy = wifi_csi_sample_source_destroy,
    };

static bool wifi_csi_wire_source_build_control(wifi_csi_wire_source_t *source, bool *invalid)
{
    *invalid = true;
    if (source->frame_count == 0 || source->frame_count > ESP32_MQUICKJS_WIFI_RX_WIRE_MAX_FRAMES) return false;
    esp32_mquickjs_wifi_rx_wire_frame_t *frames = esp32_mquickjs_memory_wireless_calloc("wifi.csi", source->frame_count, sizeof(*frames), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
    if (frames == NULL) { *invalid = false; return false; }
    bool success = false;
    for (uint16_t i = 0; i < source->frame_count; ++i) {
        esp32_mquickjs_wifi_csi_slot_t *slot = wifi_csi_resolve_event(&source->leases[i].event);
        esp32_mquickjs_wifi_csi_wire_snapshot_t snapshot;
        if (!esp32_mquickjs_wifi_csi_wire_snapshot(slot, &snapshot)) goto done;
        frames[i] = snapshot.frame;
    }
    esp32_mquickjs_wifi_rx_wire_layout_t layout;
    if (!esp32_mquickjs_wifi_rx_wire_layout(ESP32_MQUICKJS_WIFI_RX_WIRE_CSI, frames, source->frame_count, &layout)) goto done;
    source->control = esp32_mquickjs_memory_wireless_calloc("wifi.csi", 1, layout.control_bytes, ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
    if (source->control == NULL) { *invalid = false; goto done; }
    source->control_length = layout.control_bytes;
    source->total_length = layout.total_bytes;
    if (!esp32_mquickjs_wifi_rx_wire_write_control(ESP32_MQUICKJS_WIFI_RX_WIRE_CSI, frames,
            source->frame_count, source->control, source->control_length)) goto done;
    for (uint16_t i = 0; i < source->frame_count; ++i) {
        esp32_mquickjs_wifi_csi_slot_t *slot = wifi_csi_resolve_event(&source->leases[i].event);
        esp32_mquickjs_wifi_csi_wire_snapshot_t snapshot;
        if (!esp32_mquickjs_wifi_csi_wire_snapshot(slot, &snapshot) ||
            !esp32_mquickjs_wifi_rx_wire_write_metadata(ESP32_MQUICKJS_WIFI_RX_WIRE_CSI, &frames[i], &snapshot.metadata,
                source->control + layout.metadata_base + (size_t)i * WIFI_CSI_BATCH_METADATA_BYTES,
                WIFI_CSI_BATCH_METADATA_BYTES)) goto done;
    }
    success = true;
done:
    esp32_mquickjs_memory_payload_free(frames);
    return success;
}

static void wifi_csi_wire_source_release(wifi_csi_wire_source_t *source)
{
    if (source == NULL) return;
    for (uint16_t index = 0; index < source->retained_count; ++index)
        wifi_csi_lease_release(&source->leases[index]);
    source->retained_count = 0;
    esp32_mquickjs_memory_payload_free(source->control);
    source->control = NULL;
}

static bool wifi_csi_wire_source_next(
    JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out)
{
    wifi_csi_wire_source_t *source = opaque;

    (void)ctx;
    if (source == NULL || source->retained_count == 0) return false;
    if (!source->control_emitted) {
        source->control_emitted = true;
        out->data = source->control;
        out->length = source->control_length;
        out->owner = JS_UNDEFINED;
        out->dma_capable = false;
        return true;
    }
    if (source->padding_pending != 0) {
        static const uint8_t padding[3] = {0};
        out->data = padding;
        out->length = source->padding_pending;
        out->owner = JS_UNDEFINED;
        out->dma_capable = false;
        source->padding_pending = 0;
        return true;
    }
    while (source->payload_index < source->frame_count) {
        wifi_csi_lease_ref_t *lease = &source->leases[source->payload_index];
        esp32_mquickjs_wifi_csi_slot_t *slot = wifi_csi_resolve_event(&lease->event);
        if (slot == NULL || lease->released) {
            (void)JS_ThrowReferenceError(ctx, "WIFI_CSI_STALE_FRAME: retained source slot unavailable");
            return false;
        }
        if (!source->packet_pending) {
            out->data = slot->payload;
            out->length = slot->length;
            source->packet_pending = true;
        } else {
            ++source->payload_index;
            source->packet_pending = false;
            if (slot->packet == NULL || slot->packet->length == 0) continue;
            out->data = slot->packet->bytes;
            out->length = slot->packet->length;
        }
        out->owner = JS_UNDEFINED;
        out->dma_capable = false;
        source->padding_pending = (uint8_t)((4U - (out->length & 3U)) & 3U);
        return true;
    }
    return false;
}

static void wifi_csi_wire_source_iterator_close(JSContext *ctx, void *opaque)
{
    wifi_csi_wire_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) return;
    source->iterator_active = false;
    wifi_csi_wire_source_release(source);
    if (source->destroy_requested) {
        esp32_mquickjs_memory_payload_free(source);
    }
}

static bool wifi_csi_wire_source_open(
    JSContext *ctx, JSValue source_value, void *opaque,
    esp32_mquickjs_byte_span_source_t *out, JSValue *out_error)
{
    wifi_csi_wire_source_t *source = opaque;

    (void)source_value;
    if (source == NULL || source->opened) {
        *out_error = JS_ThrowReferenceError(
            ctx, "CSI wire Source is closed or consumed");
        return false;
    }
    source->opened = true;
    source->iterator_active = true;
    out->opaque = source;
    out->next = wifi_csi_wire_source_next;
    out->close = wifi_csi_wire_source_iterator_close;
    return true;
}

static size_t wifi_csi_wire_source_known_length(void *opaque)
{
    wifi_csi_wire_source_t *source = opaque;
    return source != NULL ? source->total_length : 0;
}

static void wifi_csi_wire_source_destroy(JSContext *ctx, void *opaque)
{
    wifi_csi_wire_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) return;
    if (source->iterator_active) {
        source->destroy_requested = true;
        return;
    }
    wifi_csi_wire_source_release(source);
    esp32_mquickjs_memory_payload_free(source);
}

static const esp32_mquickjs_byte_span_source_object_ops_t
    s_wifi_csi_wire_source_ops = {
        .class_id = JS_CLASS_BYTE_SPAN_SOURCE,
        .open = wifi_csi_wire_source_open,
        .known_length = wifi_csi_wire_source_known_length,
        .destroy = wifi_csi_wire_source_destroy,
    };

/* Capture before resolving Frame/Batch native adapters. JS property access may
 * close the owner, collect it, or throw; preserve that exception unchanged. */
static bool wifi_csi_capture_source_options(JSContext *ctx, int argc, JSValue *argv)
{
    static const char *const allowed[] = {"format"};
    if (argc != 1) {
        JS_ThrowTypeError(ctx, "CSI Source expects {format: 'esp32qjs-csi/1'}"); return false;
    }
    if (!esp32_mquickjs_validate_plain_options(ctx, argv[0], "CSI Source", allowed, 1U)) return false;
    JSValue format = JS_GetPropertyStr(ctx, argv[0], "format");
    if (JS_IsException(format)) return false;
    if (!wifi_csi_string_equals(ctx, format, "esp32qjs-csi/1")) {
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "CSI Source format must be esp32qjs-csi/1");
        return false;
    }
    return true;
}

/* Copies exact event identities before the first JS allocation. Source owns its
 * leases without retaining the JS Frame/Batch or their copied metadata objects. */
static JSValue wifi_csi_new_wire_source(JSContext *ctx,
    const esp32_mquickjs_wifi_csi_event_t *events, uint16_t count)
{
    if (events == NULL || count == 0 || count > ESP32_MQUICKJS_WIFI_RX_WIRE_MAX_FRAMES)
        return JS_ThrowInternalError(ctx, "WIFI_CSI_INVALID_DATA: invalid Source frame count");
    wifi_csi_wire_source_t *source = esp32_mquickjs_memory_wireless_calloc("wifi.csi", 1, sizeof(*source) + (size_t)count * sizeof(source->leases[0]), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (source == NULL) return JS_ThrowOutOfMemory(ctx);
    source->frame_count = count;
    bool invalid = true;
    for (uint16_t index = 0; index < count; ++index) {
        if (!wifi_csi_update_event(&events[index], WIFI_CSI_EVENT_RETAIN)) goto fail;
        source->leases[index].event = events[index];
        ++source->retained_count;
    }
    if (!wifi_csi_wire_source_build_control(source, &invalid)) goto fail;
    return esp32_mquickjs_new_wireless_byte_span_source("wifi.csi", ctx, JS_UNDEFINED, &s_wifi_csi_wire_source_ops, source);
fail:
    wifi_csi_wire_source_release(source);
    esp32_mquickjs_memory_payload_free(source);
    return invalid ? JS_ThrowInternalError(ctx, "WIFI_CSI_INVALID_DATA: retained source metadata is invalid") :
        JS_ThrowOutOfMemory(ctx);
}

static wifi_csi_session_t *wifi_csi_session_from_value(
    JSContext *ctx, JSValue value, bool allow_closed)
{
    wifi_csi_session_ref_t *ref;
    wifi_csi_lifecycle_t lifecycle;

    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_CSI_SESSION ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected a WiFiCsiSession");
        return NULL;
    }
    lifecycle = atomic_load_explicit(&s_wifi_csi.lifecycle,
                                     memory_order_acquire);
    if (ref->generation != s_wifi_csi.generation ||
        (!allow_closed && lifecycle == WIFI_CSI_CLOSED)) {
        JS_ThrowReferenceError(ctx,
            "WIFI_CSI_NOT_OPEN: session is closed or stale");
        return NULL;
    }
    return &s_wifi_csi;
}

static esp32_mquickjs_wifi_csi_slot_t *wifi_csi_frame_from_value(
    JSContext *ctx, JSValue value, const char *api_name,
    wifi_csi_frame_ref_t **out_ref,
    esp32_mquickjs_wifi_csi_event_t *out_event)
{
    wifi_csi_frame_ref_t *ref;
    esp32_mquickjs_wifi_csi_event_t event;
    esp32_mquickjs_wifi_csi_slot_t *slot;

    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_CSI_FRAME ||
        (ref = JS_GetOpaque(ctx, value)) == NULL || ref->closed) {
        JS_ThrowReferenceError(ctx,
            "WIFI_CSI_STALE_FRAME: %s requires an open frame", api_name);
        return NULL;
    }
    event.session_generation = ref->session_generation;
    event.slot_index = ref->slot_index;
    event.slot_generation = ref->slot_generation;
    slot = wifi_csi_resolve_event(&event);
    if (slot == NULL) {
        JS_ThrowReferenceError(ctx,
            "WIFI_CSI_STALE_FRAME: %s frame lease is stale", api_name);
        return NULL;
    }
    if (out_ref != NULL) *out_ref = ref;
    if (out_event != NULL) *out_event = event;
    return slot;
}

static wifi_csi_batch_ref_t *wifi_csi_batch_from_value(
    JSContext *ctx, JSValue value, const char *api_name)
{
    wifi_csi_batch_ref_t *batch;

    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_CSI_BATCH ||
        (batch = JS_GetOpaque(ctx, value)) == NULL || batch->closed) {
        JS_ThrowReferenceError(ctx,
            "WIFI_CSI_STALE_FRAME: %s requires an open batch", api_name);
        return NULL;
    }
    return batch;
}

static JSValue wifi_csi_packet_capabilities(JSContext *ctx)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    bool ok = !JS_IsException(*result) &&
        esp32_mquickjs_set_property_ref(ctx, result, "header", JS_TRUE) &&
        esp32_mquickjs_set_property_ref(ctx, result, "full", JS_TRUE) &&
        esp32_mquickjs_set_property_ref(ctx, result, "required", JS_TRUE) &&
        esp32_mquickjs_set_property_ref(ctx, result, "requireComplete", JS_TRUE) &&
        esp32_mquickjs_set_property_ref(ctx, result, "maxHeaderBytes", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_CSI_MAX_HEADER_BYTES)) &&
        esp32_mquickjs_set_property_ref(ctx, result, "maxPacketBytes", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_CSI_MAX_PACKET_BYTES)) &&
        esp32_mquickjs_set_property_ref(ctx, result, "fcs", JS_NewString(ctx, "unknown")) &&
        esp32_mquickjs_set_property_ref(ctx, result, "payloadRepresentation", JS_NewString(ctx, "unknown")) &&
        esp32_mquickjs_set_property_ref(ctx, result, "qualification", JS_NewString(ctx, "candidate"));
    JSValue value = JS_PopGCRef(ctx, &ref);
    return ok ? value : JS_EXCEPTION;
}

JSValue js_wifi_csi_capabilities(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
#if CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS
    static const char *const sources_promiscuous[] = {
        "associated", "promiscuous",
    };
#else
    static const char *const sources_associated[] = {"associated"};
#endif
    static const char *const phy_legacy[] = {"legacy", "ht"};
    static const char *const phy_he[] = {
        "legacy", "ht", "vht", "he-su", "he-mu", "he-er-su", "he-tb",
    };
    static const char *const phy_he_no_vht[] = {
        "legacy", "ht", "he-su", "he-mu", "he-er-su", "he-tb",
    };
    static const char *const encodings_legacy[] = {"signed-int8"};
    static const char *const encodings_he[] = {
        "signed-int8", "signed-int12-le", "signed-int12-packed",
    };
    JSGCRef result_ref, sources_ref, formats_ref, limits_ref, supports_ref;
    JSGCRef radio_ref, encodings_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *sources = JS_PushGCRef(ctx, &sources_ref);
    JSValue *formats = JS_PushGCRef(ctx, &formats_ref);
    JSValue *limits = JS_PushGCRef(ctx, &limits_ref);
    JSValue *supports = JS_PushGCRef(ctx, &supports_ref);
    JSValue *radio = JS_PushGCRef(ctx, &radio_ref);
    JSValue *encodings = JS_PushGCRef(ctx, &encodings_ref);
    bool he = esp32_mquickjs_wifi_csi_target_is_he();
    bool vht = esp32_mquickjs_wifi_csi_target_supports_vht();

    (void)this_val;
    (void)argc;
    (void)argv;
    *result = JS_NewObject(ctx);
#if CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS
    *sources = wifi_csi_string_array(
        ctx, sources_promiscuous, 2U);
#else
    *sources = wifi_csi_string_array(ctx, sources_associated, 1U);
#endif
    *formats = he
        ? (vht
            ? wifi_csi_string_array(
                ctx, phy_he, sizeof(phy_he) / sizeof(phy_he[0]))
            : wifi_csi_string_array(
                ctx, phy_he_no_vht,
                sizeof(phy_he_no_vht) / sizeof(phy_he_no_vht[0])))
        : wifi_csi_string_array(ctx, phy_legacy,
                                sizeof(phy_legacy) / sizeof(phy_legacy[0]));
    *limits = JS_NewObject(ctx);
    *supports = JS_NewObject(ctx);
    *radio = wifi_csi_radio_capabilities_to_js(ctx);
    *encodings = he
        ? wifi_csi_string_array(
            ctx, encodings_he, sizeof(encodings_he) / sizeof(encodings_he[0]))
        : wifi_csi_string_array(
            ctx, encodings_legacy,
            sizeof(encodings_legacy) / sizeof(encodings_legacy[0]));
    if (JS_IsException(*result) || JS_IsException(*sources) ||
        JS_IsException(*formats) || JS_IsException(*limits) ||
        JS_IsException(*supports) || JS_IsException(*radio) ||
        JS_IsException(*encodings) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "maxCsiBytes",
            JS_NewUint32(ctx,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_FRAME_BYTES)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "maxPoolCapacity",
            JS_NewUint32(ctx,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "maxTotalPoolCapacity",
            JS_NewUint32(ctx,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "maxPoolGenerations",
            JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "maxQueueCapacity",
            JS_NewUint32(ctx,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "maxBatchFrames",
            JS_NewUint32(ctx,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "scaleMinimum", he ? JS_NewUint32(ctx, 0U) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "scaleMaximum", he ? JS_NewUint32(ctx, 8U) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "shiftMinimum", he ? JS_NULL : JS_NewUint32(ctx, 0U)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "shiftMaximum", he ? JS_NULL : JS_NewUint32(ctx, 15U)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "fixedChannel",
            JS_NewBool(WIFI_CSI_PROMISCUOUS_SUPPORTED)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "promiscuous",
            JS_NewBool(WIFI_CSI_PROMISCUOUS_SUPPORTED)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "sourceMacFilter", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "destinationMacFilter", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, supports, "bssidFilter", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, supports, "frameTypeFilter", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, supports, "frameSubtypeFilter", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "rssiFilter", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "nativeDecimation", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "nativeRateLimit", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "vht",
            JS_NewBool(vht)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "he", JS_NewBool(he)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "heStbcSelection", JS_NewBool(he)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "captureConfigReadback", JS_NewBool(he)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "manualScaling", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "lltfBitMode",
            JS_NewBool(
                esp32_mquickjs_wifi_csi_target_supports_lltf_bit_mode())) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "layout", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "apiVersion", JS_NewString(ctx, "wifi-csi/1")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "target", JS_NewString(ctx, CONFIG_IDF_TARGET)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "idfVersion",
            JS_NewString(ctx, esp_get_idf_version())) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "configSchema",
            JS_NewString(ctx,
                esp32_mquickjs_wifi_csi_target_schema_name())) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "sources", *sources) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "phyFormats", *formats) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sampleEncodings", *encodings) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "limits", *limits) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "supports", *supports) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "radio", *radio) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "packetCapture", wifi_csi_packet_capabilities(ctx)))
        goto fail;
    *sources = JS_UNDEFINED;
    *formats = JS_UNDEFINED;
    *limits = JS_UNDEFINED;
    *supports = JS_UNDEFINED;
    *radio = JS_UNDEFINED;
    *encodings = JS_UNDEFINED;
    JS_PopGCRef(ctx, &encodings_ref);
    JS_PopGCRef(ctx, &radio_ref);
    JS_PopGCRef(ctx, &supports_ref);
    JS_PopGCRef(ctx, &limits_ref);
    JS_PopGCRef(ctx, &formats_ref);
    JS_PopGCRef(ctx, &sources_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    JS_PopGCRef(ctx, &encodings_ref);
    JS_PopGCRef(ctx, &radio_ref);
    JS_PopGCRef(ctx, &supports_ref);
    JS_PopGCRef(ctx, &limits_ref);
    JS_PopGCRef(ctx, &formats_ref);
    JS_PopGCRef(ctx, &sources_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_csi_open(JSContext *ctx, JSValue *this_val,
                         int argc, JSValue *argv)
{
    wifi_csi_options_t options;
    esp32_mquickjs_wifi_csi_allocator_t allocator;
    wifi_csi_session_ref_t *ref = NULL;
    JSGCRef queue_ref, object_ref;
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    uint32_t generation;
    esp_err_t err;

    (void)this_val;
    *queue = JS_UNDEFINED;
    *object = JS_UNDEFINED;
    if (argc != 1) {
        JS_ThrowTypeError(ctx,
            "wifi.csi.open(options) expects one options object");
        goto fail;
    }
    if (!wifi_csi_parse_open_options(ctx, argv[0], &options)) goto fail;
    if (atomic_load_explicit(&s_wifi_csi.lifecycle,
                             memory_order_acquire) != WIFI_CSI_CLOSED) {
        JS_ThrowReferenceError(ctx,
            "WIFI_CSI_ALREADY_OPEN: a CSI session already exists");
        goto fail;
    }
    if (atomic_load_explicit(&s_wifi_csi.cleanup_scheduled,
                             memory_order_acquire)) {
        JS_ThrowReferenceError(ctx,
            "WIFI_CSI_CLEANUP_PENDING: native cleanup worker is still active");
        goto fail;
    }
    if (wifi_csi_pool_retirement_pending()) {
        JS_ThrowReferenceError(ctx,
            "WIFI_CSI_CLEANUP_PENDING: previous control has not detached its pool");
        goto fail;
    }
    allocator = wifi_csi_resource_allocator();
    esp32_mquickjs_wifi_csi_store_result_t allocation_result;
    esp32_mquickjs_wifi_csi_resources_t *resources = esp32_mquickjs_wifi_csi_store_open(
        &s_wifi_csi_store, options.pool_capacity, esp32_mquickjs_wifi_csi_packet_capacity(&options.packet),
        &allocator, &allocation_result);
    if (resources == NULL) {
        if (allocation_result == ESP32_MQUICKJS_WIFI_CSI_STORE_IDENTITY)
            JS_ThrowInternalError(ctx, "WIFI_CSI_IDENTITY_EXHAUSTED: device reboot required");
        else if (allocation_result == ESP32_MQUICKJS_WIFI_CSI_STORE_BUDGET)
            JS_ThrowInternalError(ctx, "WIFI_CSI_RESOURCE_EXHAUSTED: retained CSI pools exhaust the slot or generation budget");
        else JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    generation = resources->generation;
    memset(&s_wifi_csi, 0, sizeof(s_wifi_csi));
    s_wifi_csi.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    atomic_init(&s_wifi_csi.lifecycle, WIFI_CSI_OPENING);
    atomic_init(&s_wifi_csi.cleanup_scheduled, false);
    atomic_init(&s_wifi_csi.close_requested, false);
    atomic_init(&s_wifi_csi.resources, resources);
    s_wifi_csi.generation = generation;
    s_wifi_csi.runtime = esp32_mquickjs_get_active_runtime();
    s_wifi_csi.options = options;
    resources->filter = options.filter;
    resources->packet_options = options.packet;
    *queue = esp32_mquickjs_event_queue_new_wireless("wifi.csi",
        ctx, s_wifi_csi.runtime,
        sizeof(esp32_mquickjs_wifi_csi_event_t), options.queue_capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        wifi_csi_event_to_js, wifi_csi_event_drop,
        wifi_csi_event_queue_close, (void *)(uintptr_t)generation);
    if (JS_IsException(*queue)) goto fail_open;
    s_wifi_csi.event_queue = esp32_mquickjs_event_queue_from_value(ctx, *queue);
    if (s_wifi_csi.event_queue == NULL ||
        !esp32_mquickjs_event_queue_retain(s_wifi_csi.event_queue)) {
        JS_ThrowInternalError(ctx,
            "WIFI_CSI_RESOURCE_EXHAUSTED: failed to retain receive queue");
        goto fail_open;
    }
    s_wifi_csi.event_queue_retained = true;
    err = esp32_mquickjs_wifi_radio_acquire(
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI, WIFI_MODE_STA,
        &s_wifi_csi.radio_lease);
    if (err != ESP_OK) {
        wifi_csi_throw_driver_error(ctx, "WIFI_CSI_RESOURCE_EXHAUSTED",
                                    "wifi_radio_acquire", err);
        goto fail_open;
    }
    err = wifi_csi_start_native(&s_wifi_csi);
    if (err != ESP_OK) {
        const char *code = wifi_csi_start_error_code(&s_wifi_csi, err);
        wifi_csi_throw_driver_error(
            ctx, code,
            s_wifi_csi.last_error_stage != NULL
                ? s_wifi_csi.last_error_stage : "start",
            err);
        goto fail_open;
    }
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_CSI_SESSION);
    if (JS_IsException(*object)) goto fail_open;
    ref = esp32_mquickjs_memory_wireless_calloc("wifi.csi", 1, sizeof(*ref), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail_open;
    }
    ref->generation = generation;
    JS_SetOpaque(ctx, *object, ref);
    if (!esp32_mquickjs_set_property_ref(
            ctx, object, WIFI_CSI_EVENT_QUEUE_KEY, *queue)) {
        JS_SetOpaque(ctx, *object, NULL);
        esp32_mquickjs_memory_payload_free(ref);
        ref = NULL;
        goto fail_open;
    }
    *queue = JS_UNDEFINED;
    {
        JSValue return_value = JS_PopGCRef(ctx, &object_ref);
        JS_PopGCRef(ctx, &queue_ref);
        return return_value;
    }

fail_open:
    if (!wifi_csi_close_native(&s_wifi_csi)) {
        wifi_csi_request_reap(&s_wifi_csi);
    }
    if (!JS_IsUndefined(*queue) &&
        JS_GetClassID(ctx, *queue) == JS_CLASS_EVENT_QUEUE) {
        (void)esp32_mquickjs_event_queue_dispose(ctx, *queue);
    }
fail:
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &queue_ref);
    return JS_EXCEPTION;
}

static const char *wifi_csi_power_save_name(wifi_ps_type_t power_save)
{
    return power_save == WIFI_PS_NONE ? "none" :
           power_save == WIFI_PS_MIN_MODEM ? "minimum-modem" :
           power_save == WIFI_PS_MAX_MODEM ? "maximum-modem" : "unknown";
}

static JSValue wifi_csi_status_to_js(JSContext *ctx,
                                     wifi_csi_session_t *session)
{
    wifi_csi_refresh_channel(session);
    JSGCRef result_ref, requested_ref, effective_ref, error_ref;
    JSGCRef details_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *requested = JS_PushGCRef(ctx, &requested_ref);
    JSValue *effective = JS_PushGCRef(ctx, &effective_ref);
    JSValue *error = JS_PushGCRef(ctx, &error_ref);
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    wifi_csi_lifecycle_t lifecycle = atomic_load_explicit(
        &session->lifecycle, memory_order_acquire);

    *result = JS_NewObject(ctx);
    *requested = wifi_csi_requested_to_js(ctx, &session->options);
    *effective = JS_NewObject(ctx);
    *error = JS_NULL;
    *details = JS_UNDEFINED;
    if (session->last_error != ESP_OK) {
        *error = JS_NewObject(ctx);
        *details = JS_NewObject(ctx);
        if (JS_IsException(*error) || JS_IsException(*details) ||
            !esp32_mquickjs_set_property_ref(
                ctx, details, "stage",
                session->last_error_stage != NULL
                    ? JS_NewString(ctx, session->last_error_stage) : JS_NULL) ||
            !esp32_mquickjs_set_property_ref(
                ctx, details, "espCode",
                JS_NewInt32(ctx, session->last_error)) ||
            !esp32_mquickjs_set_property_ref(
                ctx, details, "espName",
                JS_NewString(ctx, esp_err_to_name(session->last_error))) ||
            !esp32_mquickjs_set_property_ref(
                ctx, error, "code",
                JS_NewString(ctx, session->last_error_code != NULL
                    ? session->last_error_code : "WIFI_CSI_DRIVER_ERROR")) ||
            !esp32_mquickjs_set_property_ref(
                ctx, error, "operation", JS_NewString(ctx, "wifi.csi")) ||
            !esp32_mquickjs_set_property_ref(
                ctx, error, "message",
                JS_NewString(ctx, "Wi-Fi CSI operation failed")) ||
            !esp32_mquickjs_set_property_ref(
                ctx, error, "details", *details)) goto fail;
        *details = JS_UNDEFINED;
    }
    if (JS_IsException(*result) || JS_IsException(*requested) ||
        JS_IsException(*effective) || JS_IsException(*error) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "source",
            JS_NewString(ctx, wifi_csi_source_name(session->options.source))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "channel",
            JS_NewUint32(ctx, session->effective_channel)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "secondaryChannel",
            JS_NewString(ctx,
                wifi_csi_wifi_secondary_name(session->effective_secondary))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "radioGeneration",
            JS_NewUint32(ctx, session->radio_generation)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "configSchema",
            JS_NewString(ctx,
                esp32_mquickjs_wifi_csi_target_schema_name())) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "maxCsiBytes",
            JS_NewUint32(ctx,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_FRAME_BYTES)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "queueCapacity",
            JS_NewUint32(ctx, session->options.queue_capacity)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "poolCapacity",
            JS_NewUint32(ctx, session->options.pool_capacity)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "powerSave",
            JS_NewString(ctx,
                wifi_csi_power_save_name(session->effective_power_save))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "timestampAccuracy",
            JS_NewString(ctx, "callback-time")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "generation",
            JS_NewUint32(ctx, session->generation)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "state",
            JS_NewString(ctx, wifi_csi_lifecycle_name(lifecycle))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "requested", *requested) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "effective", *effective) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "lastError", *error))
        goto fail;
    *requested = JS_UNDEFINED;
    *effective = JS_UNDEFINED;
    *error = JS_UNDEFINED;
    JS_PopGCRef(ctx, &details_ref);
    JS_PopGCRef(ctx, &error_ref);
    JS_PopGCRef(ctx, &effective_ref);
    JS_PopGCRef(ctx, &requested_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    JS_PopGCRef(ctx, &details_ref);
    JS_PopGCRef(ctx, &error_ref);
    JS_PopGCRef(ctx, &effective_ref);
    JS_PopGCRef(ctx, &requested_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static const char *wifi_csi_store_state_name(esp32_mquickjs_wifi_csi_store_state_t state)
{
    switch (state) {
        case ESP32_MQUICKJS_WIFI_CSI_STORE_ALLOCATING: return "allocating";
        case ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE: return "active";
        case ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED: return "retained";
        case ESP32_MQUICKJS_WIFI_CSI_STORE_RETIRING: return "retiring";
        default: return "none";
    }
}

uint32_t esp32_mquickjs_wifi_csi_reset_counters(void)
{
    return esp32_mquickjs_wifi_csi_store_reset_counters(&s_wifi_csi_store);
}

JSValue esp32_mquickjs_wifi_csi_diagnostics(JSContext *ctx)
{
    esp32_mquickjs_wifi_csi_store_snapshot_t entries[ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS];
    size_t count = esp32_mquickjs_wifi_csi_store_snapshot(&s_wifi_csi_store, entries);
    uint32_t generation = s_wifi_csi.generation;
    wifi_csi_lifecycle_t lifecycle = atomic_load_explicit(&s_wifi_csi.lifecycle, memory_order_acquire);
    bool cleanup = atomic_load_explicit(&s_wifi_csi.cleanup_scheduled, memory_order_acquire);
    bool closing = atomic_load_explicit(&s_wifi_csi.close_requested, memory_order_acquire);
    bool identity_exhausted = atomic_load_explicit(&s_wifi_csi_store.next_generation, memory_order_acquire) == 0;
    uint64_t active_bytes = 0, retained_bytes = 0, pending_bytes = 0;
    uint32_t reserved_slots = 0;
    /* Each entry is a pointer-free native copy before the first JS allocation.
     * Storage bytes include its resource control, slots and sample allocation;
     * neither queue/JS/allocator overhead nor a global wireless budget is implied. */
    JSGCRef result_ref, array_ref, item_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *result = JS_NewObject(ctx);
    *array = JS_NewArray(ctx, 0);
    *item = JS_UNDEFINED;
    if (JS_IsException(*result) || JS_IsException(*array)) goto fail;
#define CSI_DIAG_SET(owner, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, owner, name, value)) goto fail; } while (0)
#define CSI_DIAG_NUMBER(owner, name, value) CSI_DIAG_SET(owner, name, JS_NewFloat64(ctx, (double)(value)))
    for (size_t i = 0; i < count; ++i) {
        const esp32_mquickjs_wifi_csi_store_snapshot_t *entry = &entries[i];
        bool readable = entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE ||
            entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED;
        reserved_slots += entry->capacity;
        if (entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE) active_bytes += entry->bytes;
        else if (entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED) retained_bytes += entry->bytes;
        else pending_bytes += entry->bytes;
        *item = JS_NewObject(ctx);
        if (JS_IsException(*item)) goto fail;
        CSI_DIAG_NUMBER(item, "generation", entry->generation);
        CSI_DIAG_SET(item, "state", JS_NewString(ctx, wifi_csi_store_state_name(entry->state)));
        CSI_DIAG_NUMBER(item, "capacity", entry->capacity);
        CSI_DIAG_NUMBER(item, "storageBytes", entry->bytes);
        CSI_DIAG_SET(item, "freeSlots", readable ? JS_NewUint32(ctx, entry->free_slots) : JS_NULL);
        CSI_DIAG_SET(item, "identityExhausted", readable ? JS_NewBool(entry->identity_exhausted) : JS_NULL);
        CSI_DIAG_SET(item, "droppedIdentityExhausted", readable ? JS_NewUint32(ctx, entry->dropped_identity_exhausted) : JS_NULL);
        CSI_DIAG_SET(item, "leasedFrames", readable ? JS_NewUint32(ctx, entry->leased_frames) : JS_NULL);
        CSI_DIAG_SET(item, "callbacks", readable ? JS_NewUint32(ctx, entry->callbacks) : JS_NULL);
        CSI_DIAG_SET(item, "accepted", readable ? JS_NewUint32(ctx, entry->accepted) : JS_NULL);
        CSI_DIAG_SET(item, "droppedPoolFull", readable ? JS_NewUint32(ctx, entry->dropped_pool_full) : JS_NULL);
        CSI_DIAG_SET(item, "droppedQueueFull", readable ? JS_NewUint32(ctx, entry->dropped_queue_full) : JS_NULL);
        CSI_DIAG_SET(item, "droppedClosing", readable ? JS_NewUint32(ctx, entry->dropped_closing) : JS_NULL);
        if (JS_IsException(JS_SetPropertyUint32(ctx, *array, (uint32_t)i, *item))) goto fail;
        *item = JS_UNDEFINED;
    }
    CSI_DIAG_NUMBER(result, "generation", generation);
    CSI_DIAG_SET(result, "state", JS_NewString(ctx, wifi_csi_lifecycle_name(lifecycle)));
    CSI_DIAG_NUMBER(result, "controlBytes", sizeof(s_wifi_csi) + sizeof(s_wifi_csi_store) + sizeof(s_wifi_csi_store_lock) + esp32_mquickjs_wifi_csi_rx_native_control_bytes());
    CSI_DIAG_NUMBER(result, "slotBudget", s_wifi_csi_store.maximum_slots);
    CSI_DIAG_NUMBER(result, "generationBudget", ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS);
    CSI_DIAG_NUMBER(result, "reservedSlots", reserved_slots);
    CSI_DIAG_NUMBER(result, "storageBytes", active_bytes + retained_bytes + pending_bytes);
    CSI_DIAG_NUMBER(result, "activeStorageBytes", active_bytes);
    CSI_DIAG_NUMBER(result, "retainedStorageBytes", retained_bytes);
    CSI_DIAG_NUMBER(result, "pendingStorageBytes", pending_bytes);
    CSI_DIAG_SET(result, "identityExhausted", JS_NewBool(identity_exhausted));
    CSI_DIAG_SET(result, "generations", *array);
    CSI_DIAG_SET(result, "cleanupScheduled", JS_NewBool(cleanup));
    CSI_DIAG_SET(result, "closeRequested", JS_NewBool(closing));
#undef CSI_DIAG_NUMBER
#undef CSI_DIAG_SET
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_csi_session_status(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    wifi_csi_session_t *session;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        (session = wifi_csi_session_from_value(
            ctx, *this_val, true)) == NULL) {
        if (!JS_HasException(ctx)) {
            return JS_ThrowTypeError(
                ctx, "WiFiCsiSession.status() expects no arguments");
        }
        return JS_EXCEPTION;
    }
    return wifi_csi_status_to_js(ctx, session);
}

/* Convert a native observation, independently of requested Session options.
 * Preserve otherwise inactive shift/enable bits too; no input validation may
 * silently normalize a driver readback into the requested configuration. */
static JSValue wifi_csi_native_config_to_js(JSContext *ctx,
    const wifi_csi_config_t *native, uint32_t generation)
{
#if CONFIG_SOC_WIFI_HE_SUPPORT
    unsigned stbc =
#if CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3
        native->acquire_csi_he_stbc_mode;
#else
        native->acquire_csi_he_stbc;
#endif
    if (stbc > ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE)
        return wifi_csi_throw_driver_error(ctx, "WIFI_CSI_DRIVER_ERROR", "csi-config-decode", ESP_ERR_INVALID_RESPONSE);
    JSGCRef result_ref, capture_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *capture = JS_PushGCRef(ctx, &capture_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    *capture = JS_NewObject(ctx);
    if (JS_IsException(*capture)) goto fail;
#define SET(object, name, value) do { \
    if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; \
} while (0)
    SET(result, "radioGeneration", JS_NewUint32(ctx, generation));
    SET(capture, "schema", JS_NewString(ctx, "wifi-csi-he/1"));
    SET(capture, "enable", JS_NewBool(native->enable));
    SET(capture, "enableLegacy", JS_NewBool(native->acquire_csi_legacy));
#if CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3
    SET(capture, "forceLegacyLtf", JS_NewBool(native->acquire_csi_force_lltf));
    SET(capture, "vht", JS_NewBool(native->acquire_csi_vht));
    SET(capture, "lltfBits", JS_NewUint32(ctx, native->lltf_bit_mode ? 8 : 12));
#else
    SET(capture, "forceLegacyLtf", JS_FALSE);
    SET(capture, "vht", JS_FALSE);
    SET(capture, "lltfBits", JS_NewUint32(ctx, 8));
#endif
    SET(capture, "ht20", JS_NewBool(native->acquire_csi_ht20));
    SET(capture, "ht40", JS_NewBool(native->acquire_csi_ht40));
    SET(capture, "heSu", JS_NewBool(native->acquire_csi_su));
    SET(capture, "heMu", JS_NewBool(native->acquire_csi_mu));
    SET(capture, "heDcm", JS_NewBool(native->acquire_csi_dcm));
    SET(capture, "heBeamformed", JS_NewBool(native->acquire_csi_beamformed));
    SET(capture, "heStbcLtf", JS_NewString(ctx, stbc == 0 ? "first" : stbc == 1 ? "second" : "alternate"));
    SET(capture, "valueScale", JS_NewUint32(ctx, native->val_scale_cfg));
    SET(capture, "dumpAck", JS_NewBool(native->dump_ack_en));
    SET(result, "capture", *capture);
#undef SET
    JS_PopGCRef(ctx, &capture_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &capture_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
#else
    (void)native; (void)generation;
    return wifi_csi_throw_driver_error(ctx, "WIFI_CSI_DRIVER_ERROR", "csi-config-read", ESP_ERR_NOT_SUPPORTED);
#endif
}

JSValue js_wifi_csi_session_get_capture_config(JSContext *ctx, JSValue *this_val,
                                               int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0 || this_val == NULL)
        return JS_ThrowTypeError(ctx, "WiFiCsiSession.getCaptureConfig() expects no arguments");
    wifi_csi_session_t *session = wifi_csi_session_from_value(ctx, *this_val, false);
    if (session == NULL) return JS_EXCEPTION;
    wifi_csi_config_t native;
    uint32_t generation;
    esp_err_t error = esp32_mquickjs_wifi_radio_read_csi_config(&session->radio_lease, &native, &generation);
    if (error != ESP_OK)
        return wifi_csi_throw_driver_error(ctx, "WIFI_CSI_DRIVER_ERROR", "csi-config-read", error);
    /* Only copied scalars survive any allocation/GC below. */
    return wifi_csi_native_config_to_js(ctx, &native, generation);
}

JSValue js_wifi_csi_session_stats(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    wifi_csi_session_t *session;
    wifi_csi_stats_snapshot_t snapshot;
    esp32_mquickjs_event_queue_stats_t queue_stats = {0};
    JSGCRef result_ref, queue_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    uint32_t free_slots = 0U;

    (void)argv;
    *result = JS_UNDEFINED;
    *queue = JS_UNDEFINED;
    if (argc != 0 || this_val == NULL ||
        (session = wifi_csi_session_from_value(
            ctx, *this_val, true)) == NULL) goto fail;
    taskENTER_CRITICAL(&session->lock);
    snapshot = session->final_stats;
    taskEXIT_CRITICAL(&session->lock);
    esp32_mquickjs_wifi_csi_resources_t *resources = esp32_mquickjs_wifi_csi_store_acquire(
        &s_wifi_csi_store, session->generation);
    if (resources != NULL) {
#define WIFI_CSI_STATS_COPY(name) snapshot.name = atomic_load_explicit(&resources->counters.name, memory_order_acquire)
        WIFI_CSI_STATS_COPY(callbacks);
        WIFI_CSI_STATS_COPY(accepted);
        WIFI_CSI_STATS_COPY(delivered_frames);
        WIFI_CSI_STATS_COPY(delivered_batches);
        WIFI_CSI_STATS_COPY(filtered_mac);
        WIFI_CSI_STATS_COPY(filtered_bssid);
        WIFI_CSI_STATS_COPY(filtered_frame_type);
        WIFI_CSI_STATS_COPY(filtered_frame_subtype);
        WIFI_CSI_STATS_COPY(dropped_identity_exhausted);
        WIFI_CSI_STATS_COPY(filtered_rssi);
        WIFI_CSI_STATS_COPY(filtered_decimation);
        WIFI_CSI_STATS_COPY(filtered_rate_limit);
        WIFI_CSI_STATS_COPY(filtered_first_word_invalid);
        WIFI_CSI_STATS_COPY(filtered_channel_estimate_invalid);
        WIFI_CSI_STATS_COPY(invalid_callback_data);
        WIFI_CSI_STATS_COPY(dropped_pool_full);
        WIFI_CSI_STATS_COPY(dropped_queue_full);
        WIFI_CSI_STATS_COPY(dropped_frame_too_large);
        WIFI_CSI_STATS_COPY(dropped_closing);
        WIFI_CSI_STATS_COPY(received_bytes);
        WIFI_CSI_STATS_COPY(packet_unavailable);
        WIFI_CSI_STATS_COPY(packet_malformed);
        WIFI_CSI_STATS_COPY(packet_truncated);
        WIFI_CSI_STATS_COPY(dropped_packet_required);
        WIFI_CSI_STATS_COPY(dropped_packet_incomplete);
        WIFI_CSI_STATS_COPY(received_packet_bytes);
        WIFI_CSI_STATS_COPY(leased_frames);
#undef WIFI_CSI_STATS_COPY
        free_slots = esp32_mquickjs_native_pool_available(&resources->pool);
        esp32_mquickjs_wifi_csi_store_release(&s_wifi_csi_store, resources);
    } else {
        snapshot.leased_frames = 0;
    }
    if (session->event_queue != NULL) {
        (void)esp32_mquickjs_event_queue_get_stats(
            session->event_queue, &queue_stats);
    }
    *result = JS_NewObject(ctx);
    *queue = JS_NewObject(ctx);
#define WIFI_CSI_COUNTER(name) snapshot.name
    if (JS_IsException(*result) || JS_IsException(*queue) ||
        !esp32_mquickjs_set_property_ref(
            ctx, queue, "open", JS_NewBool(queue_stats.open)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, queue, "queued", JS_NewUint32(ctx, queue_stats.queued)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, queue, "capacity", JS_NewUint32(ctx, queue_stats.capacity)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, queue, "dropped", JS_NewUint32(ctx, queue_stats.dropped)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, queue, "highWater", JS_NewUint32(ctx, queue_stats.high_water)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, queue, "receiverPending",
            JS_NewBool(queue_stats.receiver_pending)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "callbacks",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(callbacks))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "accepted",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(accepted))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "deliveredFrames",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(delivered_frames))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "deliveredBatches",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(delivered_batches))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "filteredMac",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(filtered_mac))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "filteredBssid", JS_NewUint32(ctx, WIFI_CSI_COUNTER(filtered_bssid))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "filteredFrameType", JS_NewUint32(ctx, WIFI_CSI_COUNTER(filtered_frame_type))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "filteredFrameSubtype", JS_NewUint32(ctx, WIFI_CSI_COUNTER(filtered_frame_subtype))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "droppedIdentityExhausted", JS_NewUint32(ctx, WIFI_CSI_COUNTER(dropped_identity_exhausted))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "filteredRssi",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(filtered_rssi))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "filteredDecimation",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(filtered_decimation))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "filteredRateLimit",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(filtered_rate_limit))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "filteredFirstWordInvalid",
            JS_NewUint32(ctx,
                WIFI_CSI_COUNTER(filtered_first_word_invalid))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "filteredChannelEstimateInvalid",
            JS_NewUint32(ctx,
                WIFI_CSI_COUNTER(filtered_channel_estimate_invalid))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "invalidCallbackData",
            JS_NewUint32(ctx,
                WIFI_CSI_COUNTER(invalid_callback_data))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "droppedPoolFull",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(dropped_pool_full))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "droppedQueueFull",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(dropped_queue_full))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "droppedFrameTooLarge",
            JS_NewUint32(ctx,
                WIFI_CSI_COUNTER(dropped_frame_too_large))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "droppedClosing",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(dropped_closing))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "receivedBytes",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(received_bytes))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "packetUnavailable",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(packet_unavailable))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "packetMalformed",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(packet_malformed))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "packetTruncated",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(packet_truncated))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "droppedPacketRequired",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(dropped_packet_required))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "droppedPacketIncomplete",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(dropped_packet_incomplete))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "receivedPacketBytes",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(received_packet_bytes))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "leasedFrames",
            JS_NewUint32(ctx, WIFI_CSI_COUNTER(leased_frames))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "freePoolSlots", JS_NewUint32(ctx, free_slots)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "queue", *queue)) {
#undef WIFI_CSI_COUNTER
        goto fail;
    }
#undef WIFI_CSI_COUNTER
    *queue = JS_UNDEFINED;
    JS_PopGCRef(ctx, &queue_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_csi_session_receive(JSContext *ctx, JSValue *this_val,
                                    int argc, JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    if (this_val == NULL || wifi_csi_session_from_value(
            ctx, *this_val, false) == NULL ||
        atomic_load_explicit(&s_wifi_csi.lifecycle,
                             memory_order_acquire) != WIFI_CSI_RUNNING) {
        JS_PopGCRef(ctx, &method_ref);
        if (!JS_HasException(ctx)) {
            return JS_ThrowReferenceError(
                ctx, "WIFI_CSI_NOT_RUNNING: receive requires a running session");
        }
        return JS_EXCEPTION;
    }
    *method = JS_GetPropertyStr(ctx, *this_val, "receive");
    result = JS_IsException(*method)
        ? JS_EXCEPTION
        : esp32_mquickjs_future_call_and_wait(
            ctx, esp32_mquickjs_get_active_runtime(), *method,
            *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

JSValue js_wifi_csi_session_stop(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    wifi_csi_session_t *session;
    esp_err_t err;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        (session = wifi_csi_session_from_value(
            ctx, *this_val, false)) == NULL) {
        if (!JS_HasException(ctx)) {
            return JS_ThrowTypeError(
                ctx, "WiFiCsiSession.stop() expects no arguments");
        }
        return JS_EXCEPTION;
    }
    err = wifi_csi_stop_native(session);
    if (err != ESP_OK) {
        return wifi_csi_throw_driver_error(ctx, "WIFI_CSI_DRIVER_ERROR",
                                           "stop", err);
    }
    return wifi_csi_status_to_js(ctx, session);
}

JSValue js_wifi_csi_session_configure(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv)
{
    wifi_csi_session_t *session;
    wifi_csi_options_t options;

    if (argc != 1 || this_val == NULL ||
        (session = wifi_csi_session_from_value(
            ctx, *this_val, false)) == NULL) {
        if (!JS_HasException(ctx)) {
            return JS_ThrowTypeError(
                ctx, "WiFiCsiSession.configure(options) expects one argument");
        }
        return JS_EXCEPTION;
    }
    if (atomic_load_explicit(&session->resources->identity_exhausted, memory_order_acquire)) {
        return wifi_csi_throw_driver_error(ctx, "WIFI_CSI_IDENTITY_EXHAUSTED",
            "capture-identity", ESP_ERR_INVALID_STATE);
    }

    if (atomic_load_explicit(&session->lifecycle,
                             memory_order_acquire) != WIFI_CSI_STOPPED) {
        return JS_ThrowReferenceError(ctx,
            "WIFI_CSI_NOT_RUNNING: configure requires stopped state");
    }
    if (!wifi_csi_parse_open_options(ctx, argv[0], &options)) {
        return JS_EXCEPTION;
    }
    if (options.queue_capacity != session->options.queue_capacity ||
        options.pool_capacity != session->options.pool_capacity) {
        return JS_ThrowTypeError(ctx,
            "WIFI_CSI_CONFIG_UNSUPPORTED: buffering capacities cannot change within a session");
    }
    if (esp32_mquickjs_wifi_csi_packet_capacity(&options.packet) > session->resources->max_packet_bytes) {
        return JS_ThrowRangeError(ctx, "WIFI_CSI_CONFIG_UNSUPPORTED: packet capacity exceeds the allocation made at open");
    }
    atomic_store_explicit(&session->channel_conflicted, false, memory_order_release);
    session->options = options;
    session->resources->packet_options = options.packet;
    session->resources->filter = options.filter;
    session->resources->filter_phase = 0U;
    session->resources->last_accepted_timestamp_set = false;
    session->last_error = ESP_OK;
    session->last_error_code = NULL;
    session->last_error_stage = NULL;
    return wifi_csi_status_to_js(ctx, session);
}

JSValue js_wifi_csi_session_start(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    wifi_csi_session_t *session;
    esp_err_t err;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        (session = wifi_csi_session_from_value(
            ctx, *this_val, false)) == NULL) {
        if (!JS_HasException(ctx)) {
            return JS_ThrowTypeError(
                ctx, "WiFiCsiSession.start() expects no arguments");
        }
        return JS_EXCEPTION;
    }
    if (atomic_load_explicit(&session->resources->identity_exhausted, memory_order_acquire)) {
        return wifi_csi_throw_driver_error(ctx, "WIFI_CSI_IDENTITY_EXHAUSTED",
            "capture-identity", ESP_ERR_INVALID_STATE);
    }

    if (atomic_load_explicit(&session->lifecycle,
                             memory_order_acquire) == WIFI_CSI_RUNNING) {
        return wifi_csi_status_to_js(ctx, session);
    }
    if (atomic_load_explicit(&session->lifecycle,
                             memory_order_acquire) != WIFI_CSI_STOPPED) {
        return JS_ThrowReferenceError(ctx,
            "WIFI_CSI_CLOSING: start requires stopped state");
    }
    err = wifi_csi_start_native(session);
    if (err != ESP_OK) {
        atomic_store(&session->lifecycle, WIFI_CSI_FAULTED);
        return wifi_csi_throw_driver_error(
            ctx, wifi_csi_start_error_code(session, err),
            session->last_error_stage != NULL
                ? session->last_error_stage : "start", err);
    }
    return wifi_csi_status_to_js(ctx, session);
}

JSValue js_wifi_csi_session_close(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    wifi_csi_session_ref_t *ref;
    JSGCRef queue_ref;
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);

    (void)argv;
    *queue = JS_UNDEFINED;
    if (argc != 0 || this_val == NULL ||
        JS_GetClassID(ctx, *this_val) != JS_CLASS_WIFI_CSI_SESSION ||
        (ref = JS_GetOpaque(ctx, *this_val)) == NULL) {
        JS_PopGCRef(ctx, &queue_ref);
        return JS_ThrowTypeError(
            ctx, "WiFiCsiSession.close() expects no arguments");
    }
    if (ref->generation != s_wifi_csi.generation ||
        atomic_load(&s_wifi_csi.lifecycle) == WIFI_CSI_CLOSED) {
        JS_PopGCRef(ctx, &queue_ref);
        return JS_TRUE;
    }
    *queue = JS_GetPropertyStr(ctx, *this_val, WIFI_CSI_EVENT_QUEUE_KEY);
    bool closed = wifi_csi_close_native(&s_wifi_csi);
    if (!JS_IsException(*queue) &&
        JS_GetClassID(ctx, *queue) == JS_CLASS_EVENT_QUEUE) {
        (void)esp32_mquickjs_event_queue_dispose(ctx, *queue);
    }
    (void)JS_SetPropertyStr(ctx, *this_val, WIFI_CSI_EVENT_QUEUE_KEY,
                            JS_UNDEFINED);
    if (!closed) wifi_csi_request_reap(&s_wifi_csi);
    JS_PopGCRef(ctx, &queue_ref);
    return JS_NewBool(closed);
}

JSValue js_wifi_csi_session_constructor(JSContext *ctx, JSValue *this_val,
                                        int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx,
        "WiFiCsiSession cannot be constructed directly");
}

void js_wifi_csi_session_finalizer(JSContext *ctx, void *opaque)
{
    wifi_csi_session_ref_t *ref = opaque;

    (void)ctx;
    if (ref != NULL && ref->generation == s_wifi_csi.generation) {
        wifi_csi_request_reap(&s_wifi_csi);
    }
    esp32_mquickjs_memory_payload_free(ref);
}

JSValue js_wifi_csi_frame_constructor(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx,
        "WiFiCsiFrame cannot be constructed directly");
}

void js_wifi_csi_frame_finalizer(JSContext *ctx, void *opaque)
{
    wifi_csi_frame_ref_t *ref = opaque;

    (void)ctx;
    if (ref != NULL && !ref->closed) {
        wifi_csi_lease_ref_t lease = {
            .event = {
                .session_generation = ref->session_generation,
                .slot_index = ref->slot_index,
                .slot_generation = ref->slot_generation,
            },
        };
        wifi_csi_lease_request_close(&lease);
    }
    esp32_mquickjs_memory_payload_free(ref);
}

static JSValue wifi_csi_frame_bytes(JSContext *ctx, JSValue *this_val,
    int argc, bool packet, bool copy)
{
    esp32_mquickjs_wifi_csi_event_t event;
    esp32_mquickjs_wifi_csi_slot_t *slot;
    if (argc != 0 || this_val == NULL)
        return JS_ThrowTypeError(ctx, "CSI frame byte methods require their Frame and no arguments");
    slot = wifi_csi_frame_from_value(ctx, *this_val, "WiFiCsiFrame bytes", NULL, &event);
    if (slot == NULL) return JS_EXCEPTION;
    if (packet && (slot->packet == NULL || slot->packet->length == 0)) return JS_NULL;
    uint8_t *bytes = packet ? slot->packet->bytes : slot->payload;
    size_t length = packet ? slot->packet->length : slot->length;
    if (copy) {
        uint8_t *owned = length != 0 ? esp32_mquickjs_memory_wireless_alloc("wifi.csi", length, ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY) : NULL;
        if (length != 0 && owned == NULL) return JS_ThrowOutOfMemory(ctx);
        if (length != 0) memcpy(owned, bytes, length);
        return esp32_mquickjs_new_wireless_owned_byte_view("wifi.csi", ctx, owned, length);
    }
    wifi_csi_lease_ref_t *lease = wifi_csi_retain_event(&event);
    if (lease == NULL) return JS_ThrowOutOfMemory(ctx);
    return esp32_mquickjs_new_wireless_retained_byte_view("wifi.csi", ctx, bytes, length, wifi_csi_byte_view_release, lease);
}

JSValue js_wifi_csi_frame_samples(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ (void)argv; return wifi_csi_frame_bytes(ctx, this_val, argc, false, false); }
JSValue js_wifi_csi_frame_copy_samples(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ (void)argv; return wifi_csi_frame_bytes(ctx, this_val, argc, false, true); }
JSValue js_wifi_csi_frame_packet_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ (void)argv; return wifi_csi_frame_bytes(ctx, this_val, argc, true, false); }
JSValue js_wifi_csi_frame_copy_packet_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ (void)argv; return wifi_csi_frame_bytes(ctx, this_val, argc, true, true); }


static JSValue wifi_csi_frame_data_source(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv, bool packet)
{
    esp32_mquickjs_wifi_csi_event_t event;
    esp32_mquickjs_wifi_csi_slot_t *slot;
    wifi_csi_sample_source_t *source;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        (slot = wifi_csi_frame_from_value(
            ctx, *this_val, "WiFiCsiFrame.sampleSource()", NULL, &event)) == NULL) {
        if (!JS_HasException(ctx)) {
            return JS_ThrowTypeError(
                ctx, "WiFiCsiFrame.sampleSource() expects no arguments");
        }
        return JS_EXCEPTION;
    }
    if (packet && (slot->packet == NULL || slot->packet->length == 0)) return JS_NULL;
    if (!wifi_csi_update_event(&event, WIFI_CSI_EVENT_RETAIN)) {
        return JS_ThrowReferenceError(
            ctx, "WIFI_CSI_STALE_FRAME: could not retain frame source");
    }
    source = esp32_mquickjs_memory_wireless_calloc("wifi.csi", 1, sizeof(*source), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (source == NULL) {
        (void)wifi_csi_update_event(&event, WIFI_CSI_EVENT_RELEASE);
        return JS_ThrowOutOfMemory(ctx);
    }
    source->lease.event = event;
    source->packet = packet;
    return esp32_mquickjs_new_wireless_byte_span_source("wifi.csi",
        ctx, JS_UNDEFINED, &s_wifi_csi_sample_source_ops, source);
}

JSValue js_wifi_csi_frame_sample_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return wifi_csi_frame_data_source(ctx, this_val, argc, argv, false); }
JSValue js_wifi_csi_frame_packet_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return wifi_csi_frame_data_source(ctx, this_val, argc, argv, true); }

JSValue js_wifi_csi_frame_source(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    if (this_val == NULL || JS_GetClassID(ctx, *this_val) != JS_CLASS_WIFI_CSI_FRAME)
        return JS_ThrowTypeError(ctx, "WiFiCsiFrame.source requires its Frame");
    if (!wifi_csi_capture_source_options(ctx, argc, argv)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_csi_event_t event;
    if (wifi_csi_frame_from_value(ctx, *this_val, "WiFiCsiFrame.source()", NULL, &event) == NULL)
        return JS_EXCEPTION;
    return wifi_csi_new_wire_source(ctx, &event, 1U);
}

JSValue js_wifi_csi_frame_close(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv)
{
    wifi_csi_frame_ref_t *ref;
    wifi_csi_lease_ref_t lease;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        JS_GetClassID(ctx, *this_val) != JS_CLASS_WIFI_CSI_FRAME) {
        return JS_ThrowTypeError(
            ctx, "WiFiCsiFrame.close() expects no arguments");
    }
    ref = JS_GetOpaque(ctx, *this_val);
    if (ref == NULL) return JS_TRUE;
    memset(&lease, 0, sizeof(lease));
    lease.event.session_generation = ref->session_generation;
    lease.event.slot_index = ref->slot_index;
    lease.event.slot_generation = ref->slot_generation;
    ref->closed = true;
    wifi_csi_lease_request_close(&lease);
    JS_SetOpaque(ctx, *this_val, NULL);
    esp32_mquickjs_memory_payload_free(ref);
    return JS_TRUE;
}

JSValue js_wifi_csi_batch_constructor(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx,
        "WiFiCsiBatch cannot be constructed directly");
}

static void wifi_csi_batch_release_owner(wifi_csi_batch_ref_t *batch)
{
    uint16_t index;

    if (batch == NULL || batch->closed) return;
    batch->closed = true;
    for (index = 0; index < batch->frame_count; ++index) {
        wifi_csi_lease_ref_t lease = {.event = batch->events[index]};

        wifi_csi_lease_request_close(&lease);
    }
}

void js_wifi_csi_batch_finalizer(JSContext *ctx, void *opaque)
{
    wifi_csi_batch_ref_t *batch = opaque;

    (void)ctx;
    wifi_csi_batch_release_owner(batch);
    esp32_mquickjs_memory_payload_free(batch);
}

static bool wifi_csi_batch_index(JSContext *ctx,
                                 wifi_csi_batch_ref_t *batch,
                                 int argc, JSValue *argv,
                                 uint32_t *index)
{
    if (argc != 1 || !esp32_mquickjs_value_to_bounded_u32(
            ctx, argv[0], 0U,
            batch->frame_count > 0U ? batch->frame_count - 1U : 0U,
            index)) {
        JS_ThrowRangeError(ctx, "batch index is out of range");
        return false;
    }
    return true;
}

JSValue js_wifi_csi_batch_info(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv)
{
    wifi_csi_batch_ref_t *batch;
    esp32_mquickjs_wifi_csi_slot_t *slot;
    uint32_t index;

    if (this_val == NULL ||
        (batch = wifi_csi_batch_from_value(
            ctx, *this_val, "WiFiCsiBatch.info()")) == NULL ||
        !wifi_csi_batch_index(ctx, batch, argc, argv, &index)) {
        return JS_EXCEPTION;
    }
    slot = wifi_csi_resolve_event(&batch->events[index]);
    if (slot == NULL) {
        return JS_ThrowReferenceError(
            ctx, "WIFI_CSI_STALE_FRAME: batch frame is stale");
    }
    return wifi_csi_frame_info_to_js(ctx, slot);
}

static JSValue wifi_csi_batch_bytes(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv, bool packet)
{
    wifi_csi_batch_ref_t *batch;
    esp32_mquickjs_wifi_csi_slot_t *slot;
    wifi_csi_lease_ref_t *lease;
    uint32_t index;

    if (this_val == NULL ||
        (batch = wifi_csi_batch_from_value(
            ctx, *this_val, "WiFiCsiBatch.samples()")) == NULL ||
        !wifi_csi_batch_index(ctx, batch, argc, argv, &index)) {
        return JS_EXCEPTION;
    }
    slot = wifi_csi_resolve_event(&batch->events[index]);
    if (slot == NULL) {
        return JS_ThrowReferenceError(
            ctx, "WIFI_CSI_STALE_FRAME: batch frame is stale");
    }
    if (packet && (slot->packet == NULL || slot->packet->length == 0)) return JS_NULL;
    lease = wifi_csi_retain_event(&batch->events[index]);
    if (lease == NULL) return JS_ThrowOutOfMemory(ctx);
    return esp32_mquickjs_new_wireless_retained_byte_view("wifi.csi",
        ctx, packet ? slot->packet->bytes : slot->payload,
        packet ? slot->packet->length : slot->length,
        wifi_csi_byte_view_release, lease);
}

JSValue js_wifi_csi_batch_samples(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return wifi_csi_batch_bytes(ctx, this_val, argc, argv, false); }
JSValue js_wifi_csi_batch_packet_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return wifi_csi_batch_bytes(ctx, this_val, argc, argv, true); }

JSValue js_wifi_csi_batch_source(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    if (this_val == NULL || JS_GetClassID(ctx, *this_val) != JS_CLASS_WIFI_CSI_BATCH)
        return JS_ThrowTypeError(ctx, "WiFiCsiBatch.source requires its Batch");
    if (!wifi_csi_capture_source_options(ctx, argc, argv)) return JS_EXCEPTION;
    /* A format getter can close the Batch: never keep its adapter across JS. */
    wifi_csi_batch_ref_t *batch = wifi_csi_batch_from_value(ctx, *this_val, "WiFiCsiBatch.source()");
    if (batch == NULL) return JS_EXCEPTION;
    return wifi_csi_new_wire_source(ctx, batch->events, batch->frame_count);
}

JSValue js_wifi_csi_batch_close(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv)
{
    wifi_csi_batch_ref_t *batch;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        JS_GetClassID(ctx, *this_val) != JS_CLASS_WIFI_CSI_BATCH) {
        return JS_ThrowTypeError(
            ctx, "WiFiCsiBatch.close() expects no arguments");
    }
    batch = JS_GetOpaque(ctx, *this_val);
    if (batch == NULL) return JS_TRUE;
    wifi_csi_batch_release_owner(batch);
    JS_SetOpaque(ctx, *this_val, NULL);
    esp32_mquickjs_memory_payload_free(batch);
    return JS_TRUE;
}

JSValue js_wifi_csi_session_receive_batch(JSContext *ctx,
                                          JSValue *this_val,
                                          int argc,
                                          JSValue *argv)
{
    static const char *const allowed[] = {
        "maximumFrames", "minimumFrames", "timeoutMs", "maximumLatencyMs",
    };
    wifi_csi_session_t *session;
    wifi_csi_batch_ref_t *batch = NULL;
    wifi_csi_frame_ref_t *frame_ref;
    JSGCRef first_ref, object_ref, options_ref;
    JSValue *first = JS_PushGCRef(ctx, &first_ref);
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *options = JS_PushGCRef(ctx, &options_ref);
    uint32_t maximum_frames = CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES;
    uint32_t minimum_frames = 1U;
    uint32_t timeout_ms = 0U;
    uint32_t maximum_latency_ms = 0U;
    bool timeout_set = false;
    int64_t latency_deadline_us = 0;

    *first = JS_UNDEFINED;
    *object = JS_UNDEFINED;
    *options = JS_UNDEFINED;
    if (this_val == NULL ||
        (session = wifi_csi_session_from_value(
            ctx, *this_val, false)) == NULL) goto fail;
    if (maximum_frames > session->options.queue_capacity)
        maximum_frames = session->options.queue_capacity;
    if (argc > 1) {
        JS_ThrowTypeError(
            ctx, "WiFiCsiSession.receiveBatch(options?) expects at most one argument");
        goto fail;
    }
    if (argc == 1 && !JS_IsUndefined(argv[0])) {
        JSValue property;

        *options = argv[0];
        if (!esp32_mquickjs_validate_plain_options(
                ctx, *options, "WiFiCsiSession.receiveBatch()",
                allowed, sizeof(allowed) / sizeof(allowed[0]))) goto fail;
        property = JS_GetPropertyStr(ctx, *options, "maximumFrames");
        if (JS_IsException(property)) goto fail;
        if (!JS_IsUndefined(property) &&
            !esp32_mquickjs_value_to_bounded_u32(
                ctx, property, 1U,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES,
                &maximum_frames)) {
            JS_ThrowRangeError(ctx,
                "maximumFrames exceeds this Build Context");
            goto fail;
        }
        property = JS_GetPropertyStr(ctx, *options, "minimumFrames");
        if (JS_IsException(property)) goto fail;
        if (!JS_IsUndefined(property) &&
            !esp32_mquickjs_value_to_bounded_u32(
                ctx, property, 1U,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES,
                &minimum_frames)) {
            JS_ThrowRangeError(ctx,
                "minimumFrames exceeds this Build Context");
            goto fail;
        }
        if (minimum_frames > maximum_frames) {
            JS_ThrowRangeError(
                ctx, "minimumFrames must not exceed maximumFrames");
            goto fail;
        }
        property = JS_GetPropertyStr(ctx, *options, "timeoutMs");
        if (JS_IsException(property)) goto fail;
        if (!JS_IsUndefined(property)) {
            if (!esp32_mquickjs_value_to_bounded_u32(
                    ctx, property, 0U, INT32_MAX, &timeout_ms)) {
                JS_ThrowRangeError(
                    ctx, "timeoutMs must be an integer from 0 to INT32_MAX");
                goto fail;
            }
            timeout_set = true;
        }
        property = JS_GetPropertyStr(ctx, *options, "maximumLatencyMs");
        if (JS_IsException(property)) goto fail;
        if (!JS_IsUndefined(property) &&
            !esp32_mquickjs_value_to_bounded_u32(
                ctx, property, 0U, INT32_MAX, &maximum_latency_ms)) {
            JS_ThrowRangeError(ctx,
                "maximumLatencyMs must be an integer from 0 to INT32_MAX");
            goto fail;
        }
    }
    if (maximum_frames > session->options.queue_capacity ||
        !esp32_mquickjs_wifi_csi_batch_options_valid(
            maximum_frames, minimum_frames, timeout_ms,
            maximum_latency_ms,
            CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES)) {
        JS_ThrowRangeError(ctx, "invalid receiveBatch aggregation options");
        goto fail;
    }
    if (timeout_set) {
        JSValue receive_argument = JS_NewUint32(ctx, timeout_ms);

        *first = js_wifi_csi_session_receive(
            ctx, this_val, 1, &receive_argument);
    } else {
        *first = js_wifi_csi_session_receive(ctx, this_val, 0, NULL);
    }
    if (JS_IsException(*first)) goto fail;
    if (JS_IsNull(*first)) {
        JS_PopGCRef(ctx, &options_ref);
        JS_PopGCRef(ctx, &object_ref);
        JS_PopGCRef(ctx, &first_ref);
        return JS_NULL;
    }
    if (JS_GetClassID(ctx, *first) != JS_CLASS_WIFI_CSI_FRAME ||
        (frame_ref = JS_GetOpaque(ctx, *first)) == NULL) {
        JS_ThrowInternalError(ctx,
            "Wi-Fi CSI receive returned an invalid frame");
        goto fail;
    }
    batch = esp32_mquickjs_memory_wireless_calloc("wifi.csi", 1, sizeof(*batch) +
            ((size_t)maximum_frames * sizeof(batch->events[0])), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (batch == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    batch->frame_count = 1U;
    batch->events[0].session_generation = frame_ref->session_generation;
    batch->events[0].slot_index = frame_ref->slot_index;
    batch->events[0].slot_generation = frame_ref->slot_generation;
    frame_ref->closed = true;
    JS_SetOpaque(ctx, *first, NULL);
    esp32_mquickjs_memory_payload_free(frame_ref);
    atomic_fetch_sub_explicit(
        &session->resources->counters.delivered_frames, 1U,
        memory_order_relaxed);
    *first = JS_UNDEFINED;
    latency_deadline_us = esp_timer_get_time() +
        ((int64_t)maximum_latency_ms * 1000LL);
    for (;;) {
        while (batch->frame_count < maximum_frames) {
            esp32_mquickjs_wifi_csi_event_t event;

            if (!esp32_mquickjs_event_queue_try_receive(
                    session->event_queue, &event)) break;
            if (wifi_csi_update_event(&event, WIFI_CSI_EVENT_TAKE)) {
                batch->events[batch->frame_count++] = event;
            }
        }
        if (esp32_mquickjs_wifi_csi_batch_should_return(
                batch->frame_count, minimum_frames, maximum_frames,
                maximum_latency_ms,
                esp_timer_get_time() >= latency_deadline_us)) break;
        {
            int64_t remaining_us = latency_deadline_us - esp_timer_get_time();
            uint32_t remaining_ms;
            JSValue receive_argument;

            if (remaining_us <= 0) break;
            remaining_ms = (uint32_t)((remaining_us + 999LL) / 1000LL);
            receive_argument = JS_NewUint32(ctx, remaining_ms);
            *first = js_wifi_csi_session_receive(
                ctx, this_val, 1, &receive_argument);
            if (JS_IsException(*first)) goto fail_batch;
            if (JS_IsNull(*first)) {
                *first = JS_UNDEFINED;
                break;
            }
            if (JS_GetClassID(ctx, *first) != JS_CLASS_WIFI_CSI_FRAME ||
                (frame_ref = JS_GetOpaque(ctx, *first)) == NULL) {
                JS_ThrowInternalError(ctx,
                    "Wi-Fi CSI receive returned an invalid frame");
                goto fail_batch;
            }
            batch->events[batch->frame_count].session_generation =
                frame_ref->session_generation;
            batch->events[batch->frame_count].slot_index =
                frame_ref->slot_index;
            batch->events[batch->frame_count].slot_generation =
                frame_ref->slot_generation;
            batch->frame_count += 1U;
            frame_ref->closed = true;
            JS_SetOpaque(ctx, *first, NULL);
            esp32_mquickjs_memory_payload_free(frame_ref);
            atomic_fetch_sub_explicit(
                &session->resources->counters.delivered_frames, 1U,
                memory_order_relaxed);
            *first = JS_UNDEFINED;
        }
    }
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_CSI_BATCH);
    if (JS_IsException(*object)) goto fail_batch;
    JS_SetOpaque(ctx, *object, batch);
    if (!esp32_mquickjs_set_property_ref(
            ctx, object, "frameCount",
            JS_NewUint32(ctx, batch->frame_count))) {
        JS_SetOpaque(ctx, *object, NULL);
        goto fail_batch;
    }
    (void)wifi_csi_update_event(&batch->events[0], WIFI_CSI_EVENT_BATCH_DELIVERED);
    *options = JS_UNDEFINED;
    JS_PopGCRef(ctx, &options_ref);
    JSValue return_value = JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &first_ref);
    return return_value;

fail_batch:
    wifi_csi_batch_release_owner(batch);
    esp32_mquickjs_memory_payload_free(batch);
    batch = NULL;
fail:
    if (batch != NULL) {
        wifi_csi_batch_release_owner(batch);
        esp32_mquickjs_memory_payload_free(batch);
    }
    if (!JS_IsUndefined(*first) &&
        JS_GetClassID(ctx, *first) == JS_CLASS_WIFI_CSI_FRAME) {
        (void)js_wifi_csi_frame_close(ctx, first, 0, NULL);
    }
    JS_PopGCRef(ctx, &options_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &first_ref);
    return JS_EXCEPTION;
}

bool esp32_mquickjs_init_wifi_csi_runtime(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef session_ref, method_ref;
    JSValue *session = JS_PushGCRef(ctx, &session_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    bool result;

    if (ctx == NULL || runtime == NULL ||
        atomic_load_explicit(&s_wifi_csi.lifecycle,
                             memory_order_acquire) != WIFI_CSI_CLOSED ||
        atomic_load_explicit(&s_wifi_csi.cleanup_scheduled,
                             memory_order_acquire) ||
        wifi_csi_pool_retirement_pending()) {
        JS_PopGCRef(ctx, &method_ref);
        JS_PopGCRef(ctx, &session_ref);
        return false;
    }
    *session = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_CSI_SESSION);
    *method = JS_IsException(*session)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *session, "receive");
    result = !JS_IsException(*method) &&
        esp32_mquickjs_event_queue_register_receive_alias(
            ctx, runtime, *method);
    if (!result && !JS_HasException(ctx)) {
        JS_ThrowInternalError(ctx,
            "failed to register Wi-Fi CSI receive Future driver");
    }
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &session_ref);
    return result;
}

void esp32_mquickjs_deinit_wifi_csi_runtime(JSContext *ctx)
{
    (void)ctx;
    if (!wifi_csi_close_native(&s_wifi_csi)) {
        ESP_LOGE(TAG, "Wi-Fi CSI runtime cleanup remains pending");
    }
}

#endif
