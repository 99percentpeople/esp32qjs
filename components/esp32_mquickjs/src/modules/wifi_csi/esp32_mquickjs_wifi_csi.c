#include "esp32_mquickjs_wifi_csi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_reaper.h"
#include "esp32_mquickjs_wifi_csi_resources.h"
#include "esp32_mquickjs_wifi_csi_target.h"

#ifdef CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS
#define WIFI_CSI_PROMISCUOUS_SUPPORTED true
#else
#define WIFI_CSI_PROMISCUOUS_SUPPORTED false
#endif

#ifdef CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_FIXED_CHANNEL
#define WIFI_CSI_FIXED_CHANNEL_SUPPORTED true
#else
#define WIFI_CSI_FIXED_CHANNEL_SUPPORTED false
#endif
#include "esp32_mquickjs_wifi_radio.h"
#include "utils/esp32_mquickjs_byte_source.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
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
#define WIFI_CSI_BATCH_HEADER_BYTES 24U
#define WIFI_CSI_BATCH_DIRECTORY_BYTES 16U
#define WIFI_CSI_BATCH_METADATA_BYTES 64U
#define WIFI_CSI_BINARY_VERSION 1U

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
    uint32_t queue_capacity;
    esp32_mquickjs_wifi_csi_capture_config_t capture;
    esp32_mquickjs_wifi_csi_filter_t filter;
} wifi_csi_options_t;

typedef struct {
    uint32_t callbacks;
    uint32_t accepted;
    uint32_t delivered_frames;
    uint32_t delivered_batches;
    uint32_t filtered_mac;
    uint32_t filtered_rssi;
    uint32_t filtered_decimation;
    uint32_t filtered_rate_limit;
    uint32_t invalid_channel_estimate;
    uint32_t dropped_pool_full;
    uint32_t dropped_queue_full;
    uint32_t dropped_frame_too_large;
    uint32_t dropped_closing;
    uint32_t received_bytes;
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
    bool resources_destroying;
    _Atomic bool cleanup_scheduled;
    _Atomic bool close_requested;
    wifi_csi_options_t options;
    esp32_mquickjs_wifi_csi_resources_t resources;
    wifi_csi_stats_snapshot_t final_stats;
    uint8_t effective_channel;
    wifi_second_chan_t effective_secondary;
    uint32_t radio_generation;
    wifi_ps_type_t effective_power_save;
    esp_err_t last_error;
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
    bool opened;
    bool iterator_active;
    bool destroy_requested;
} wifi_csi_frame_source_t;

typedef struct {
    uint16_t frame_count;
    uint16_t payload_index;
    uint8_t *control;
    size_t control_length;
    bool control_emitted;
    bool opened;
    bool iterator_active;
    bool destroy_requested;
    wifi_csi_lease_ref_t leases[];
} wifi_csi_batch_source_t;

static const char *TAG = "esp32qjs_wifi_csi";
static wifi_csi_session_t s_wifi_csi;
static _Atomic uint32_t s_wifi_csi_next_generation = 1U;

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
    const char *text = JS_IsString(ctx, value)
        ? JS_ToCString(ctx, value, &buffer) : NULL;

    return text != NULL && strcmp(text, expected) == 0;
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
    options->queue_capacity = CONFIG_ESP32_MQUICKJS_WIFI_CSI_QUEUE_LEN;
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
        const char *text = JS_IsString(ctx, entry)
            ? JS_ToCString(ctx, entry, &buffer) : NULL;

        if (text == NULL || !wifi_csi_parse_mac_text(text, output[index])) {
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

static bool wifi_csi_parse_filter_rooted(JSContext *ctx, JSValue *value,
                                  esp32_mquickjs_wifi_csi_filter_t *filter)
{
    static const char *const allowed[] = {
        "sourceMac", "destinationMac", "minimumRssi", "sampleEvery",
        "maximumRateHz", "validOnly",
    };
    JSValue property;
    int32_t signed_value;
    uint32_t unsigned_value;

    if (!esp32_mquickjs_validate_plain_options(
            ctx, *value, "wifiCsi.open({ filter })", allowed, 6U)) {
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
            ctx, *value, "wifiCsi legacy capture", allowed, 8U)) return false;
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
            ctx, property, "wifiCsi legacy scale", scale_allowed, 1U)) {
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
            ctx, *value, "wifiCsi HE capture", allowed, 14U)) return false;
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

static bool wifi_csi_parse_open_options_rooted(JSContext *ctx, JSValue *value,
                                        wifi_csi_options_t *options)
{
    static const char *const allowed[] = {
        "source", "channel", "conflict", "capture", "filter", "queue",
        "powerSavePolicy",
    };
    static const char *const queue_allowed[] = {"capacity", "overflow"};
    JSValue property;
    uint32_t number;
    esp32_mquickjs_wifi_csi_target_config_result_t target_result;

    wifi_csi_default_options(options);
    if (!esp32_mquickjs_validate_plain_options(
            ctx, *value, "wifiCsi.open(options)", allowed, 7U)) return false;
    property = JS_GetPropertyStr(ctx, *value, "source");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (wifi_csi_string_equals(ctx, property, "associated")) {
            options->source = WIFI_CSI_SOURCE_ASSOCIATED;
        } else if (wifi_csi_string_equals(ctx, property, "promiscuous")) {
#if CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS
            options->source = WIFI_CSI_SOURCE_PROMISCUOUS;
#else
            return JS_ThrowTypeError(
                ctx, "WIFI_CSI_CONFIG_UNSUPPORTED: promiscuous capture is disabled by this Build Context"), false;
#endif
        } else {
            return JS_ThrowTypeError(
                ctx, "source expects associated or promiscuous"), false;
        }
    }
    property = JS_GetPropertyStr(ctx, *value, "channel");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property) &&
        !wifi_csi_string_equals(ctx, property, "current")) {
#if CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_FIXED_CHANNEL
        if (!esp32_mquickjs_value_to_bounded_u32(
                ctx, property, 1U, 196U, &number)) {
            return JS_ThrowRangeError(
                ctx, "channel expects current or a channel number"), false;
        }
        options->fixed_channel = true;
        options->channel = (uint8_t)number;
#else
        return JS_ThrowTypeError(
            ctx, "WIFI_CSI_CONFIG_UNSUPPORTED: fixed channel is disabled by this Build Context"), false;
#endif
    }
    property = JS_GetPropertyStr(ctx, *value, "conflict");
    if (JS_IsException(property) ||
        (!JS_IsUndefined(property) &&
         !wifi_csi_string_equals(ctx, property, "fail"))) {
        return JS_ThrowTypeError(ctx, "conflict only supports fail"), false;
    }
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
    property = JS_GetPropertyStr(ctx, *value, "filter");
    if (JS_IsException(property) ||
        (!JS_IsUndefined(property) &&
         !wifi_csi_parse_filter(ctx, property, &options->filter))) return false;
    property = JS_GetPropertyStr(ctx, *value, "queue");
    if (JS_IsException(property)) return false;
    if (!JS_IsUndefined(property)) {
        if (!esp32_mquickjs_validate_plain_options(
                ctx, property, "wifiCsi.open({ queue })",
                queue_allowed, 2U)) return false;
        property = JS_GetPropertyStr(ctx, *value, "queue");
        if (JS_IsException(property)) return false;
        JSValue queue_property = JS_GetPropertyStr(ctx, property, "capacity");
        if (JS_IsException(queue_property)) return false;
        if (!JS_IsUndefined(queue_property)) {
            if (!esp32_mquickjs_value_to_bounded_u32(
                    ctx, queue_property, 1U,
                    CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY, &number)) {
                return JS_ThrowRangeError(
                    ctx, "queue.capacity exceeds this Build Context"), false;
            }
            options->queue_capacity = number;
        }
        property = JS_GetPropertyStr(ctx, *value, "queue");
        if (JS_IsException(property)) return false;
        queue_property = JS_GetPropertyStr(ctx, property, "overflow");
        if (JS_IsException(queue_property) ||
            (!JS_IsUndefined(queue_property) &&
             !wifi_csi_string_equals(ctx, queue_property, "drop-newest"))) {
            return JS_ThrowTypeError(
                ctx, "queue.overflow only supports drop-newest"), false;
        }
    }
    property = JS_GetPropertyStr(ctx, *value, "capture");
    if (JS_IsException(property) || JS_IsUndefined(property)) {
        return JS_ThrowTypeError(ctx, "wifiCsi.open() requires capture"), false;
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
    return heap_caps_calloc(count, size,
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void *wifi_csi_resource_malloc(size_t size, void *opaque)
{
    (void)opaque;
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void wifi_csi_resource_free(void *ptr, void *opaque)
{
    (void)opaque;
    heap_caps_free(ptr);
}

static esp32_mquickjs_wifi_csi_allocator_t wifi_csi_resource_allocator(void)
{
    esp32_mquickjs_wifi_csi_allocator_t allocator = {
        .calloc_fn = wifi_csi_resource_calloc,
        .malloc_fn = wifi_csi_resource_malloc,
        .free_fn = wifi_csi_resource_free,
    };

    return allocator;
}

static void wifi_csi_snapshot_stats(wifi_csi_session_t *session)
{
    esp32_mquickjs_wifi_csi_counters_t *counters =
        &session->resources.counters;

#define WIFI_CSI_SNAPSHOT(name) \
    session->final_stats.name = atomic_load_explicit( \
        &counters->name, memory_order_acquire)
    WIFI_CSI_SNAPSHOT(callbacks);
    WIFI_CSI_SNAPSHOT(accepted);
    WIFI_CSI_SNAPSHOT(delivered_frames);
    WIFI_CSI_SNAPSHOT(delivered_batches);
    WIFI_CSI_SNAPSHOT(filtered_mac);
    WIFI_CSI_SNAPSHOT(filtered_rssi);
    WIFI_CSI_SNAPSHOT(filtered_decimation);
    WIFI_CSI_SNAPSHOT(filtered_rate_limit);
    WIFI_CSI_SNAPSHOT(invalid_channel_estimate);
    WIFI_CSI_SNAPSHOT(dropped_pool_full);
    WIFI_CSI_SNAPSHOT(dropped_queue_full);
    WIFI_CSI_SNAPSHOT(dropped_frame_too_large);
    WIFI_CSI_SNAPSHOT(dropped_closing);
    WIFI_CSI_SNAPSHOT(received_bytes);
    WIFI_CSI_SNAPSHOT(leased_frames);
#undef WIFI_CSI_SNAPSHOT
}

static void wifi_csi_maybe_destroy_resources(wifi_csi_session_t *session)
{
    bool destroy = false;

    if (session == NULL) return;
    taskENTER_CRITICAL(&session->lock);
    if (!session->resources_destroying && session->resources.slots != NULL &&
        atomic_load_explicit(&session->lifecycle, memory_order_acquire) ==
            WIFI_CSI_CLOSED &&
        atomic_load_explicit(&session->resources.callbacks_active,
                             memory_order_acquire) == 0U &&
        atomic_load_explicit(&session->resources.counters.leased_frames,
                             memory_order_acquire) == 0U) {
        session->resources_destroying = true;
        wifi_csi_snapshot_stats(session);
        destroy = true;
    }
    taskEXIT_CRITICAL(&session->lock);
    if (destroy) {
        if (!esp32_mquickjs_wifi_csi_resources_deinit(&session->resources)) {
            taskENTER_CRITICAL(&session->lock);
            session->resources_destroying = false;
            taskEXIT_CRITICAL(&session->lock);
        }
    }
}

static esp32_mquickjs_wifi_csi_slot_t *wifi_csi_resolve_event(
    const esp32_mquickjs_wifi_csi_event_t *event)
{
    return esp32_mquickjs_wifi_csi_slot_from_event(
        &s_wifi_csi.resources, event);
}

static void wifi_csi_lease_release(wifi_csi_lease_ref_t *lease)
{
    esp32_mquickjs_wifi_csi_slot_t *slot;

    if (lease == NULL || lease->released) return;
    slot = wifi_csi_resolve_event(&lease->event);
    if (slot != NULL) {
        (void)esp32_mquickjs_wifi_csi_slot_release(
            &s_wifi_csi.resources, slot);
    }
    lease->released = true;
    wifi_csi_maybe_destroy_resources(&s_wifi_csi);
}

static void wifi_csi_lease_request_close(wifi_csi_lease_ref_t *lease)
{
    esp32_mquickjs_wifi_csi_slot_t *slot;

    if (lease == NULL || lease->released) return;
    slot = wifi_csi_resolve_event(&lease->event);
    if (slot != NULL) {
        (void)esp32_mquickjs_wifi_csi_slot_request_close(
            &s_wifi_csi.resources, slot);
    }
    lease->released = true;
    wifi_csi_maybe_destroy_resources(&s_wifi_csi);
}

static wifi_csi_lease_ref_t *wifi_csi_retain_event(
    const esp32_mquickjs_wifi_csi_event_t *event)
{
    esp32_mquickjs_wifi_csi_slot_t *slot = wifi_csi_resolve_event(event);
    wifi_csi_lease_ref_t *lease;

    if (slot == NULL || !esp32_mquickjs_wifi_csi_slot_retain(
            &s_wifi_csi.resources, slot)) return NULL;
    lease = heap_caps_calloc(1, sizeof(*lease), MALLOC_CAP_8BIT);
    if (lease == NULL) {
        (void)esp32_mquickjs_wifi_csi_slot_release(
            &s_wifi_csi.resources, slot);
        return NULL;
    }
    lease->event = *event;
    return lease;
}

static void wifi_csi_byte_view_release(void *opaque)
{
    wifi_csi_lease_ref_t *lease = opaque;

    wifi_csi_lease_release(lease);
    heap_caps_free(lease);
}

static bool wifi_csi_publish_event(
    const esp32_mquickjs_wifi_csi_event_t *event, void *opaque)
{
    wifi_csi_session_t *session = opaque;

    return session != NULL &&
        esp32_mquickjs_event_queue_try_send_from_callback(
            session->event_queue, event);
}

static void wifi_csi_rx_callback(void *opaque, wifi_csi_info_t *info)
{
    wifi_csi_session_t *session = opaque;
    esp32_mquickjs_wifi_csi_metadata_t metadata;

    if (!esp32_mquickjs_wifi_csi_callback_enter(&session->resources)) {
        esp32_mquickjs_wifi_csi_callback_leave(&session->resources);
        return;
    }
    if (info == NULL || info->buf == NULL) {
        atomic_fetch_add_explicit(
            &session->resources.counters.invalid_channel_estimate, 1U,
            memory_order_relaxed);
    } else {
        esp32_mquickjs_wifi_csi_target_normalize_metadata(
            info, &session->options.capture, &metadata);
        (void)esp32_mquickjs_wifi_csi_callback_publish(
            &session->resources, &metadata, (const uint8_t *)info->buf,
            info->len, wifi_csi_publish_event, session);
    }
    esp32_mquickjs_wifi_csi_callback_leave(&session->resources);
}

static void wifi_csi_event_drop(void *event, void *opaque)
{
    esp32_mquickjs_wifi_csi_event_t *receive_event = event;
    wifi_csi_session_t *session = opaque;
    esp32_mquickjs_wifi_csi_slot_t *slot;

    if (receive_event == NULL || session == NULL) return;
    slot = esp32_mquickjs_wifi_csi_slot_from_event(
        &session->resources, receive_event);
    if (slot != NULL) {
        (void)esp32_mquickjs_wifi_csi_slot_request_close(
            &session->resources, slot);
    }
    wifi_csi_maybe_destroy_resources(session);
}

static void wifi_csi_request_reap(wifi_csi_session_t *session);

static void wifi_csi_event_queue_close(void *opaque)
{
    wifi_csi_session_t *session = opaque;

    if (session != NULL && atomic_load_explicit(
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
        ctx, code, "wifiCsi", "Wi-Fi CSI operation failed", *details);
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
            esp32_mquickjs_wifi_radio_release_channel(&session->radio_lease);
            return err;
        }
    }
    err = esp32_mquickjs_wifi_radio_get_channel(
        &session->effective_channel, &session->effective_secondary,
        &session->radio_generation);
    if (err != ESP_OK) {
        session->last_error_stage = "wifi_radio_get_channel";
        goto fail_radio_policy;
    }
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
    esp32_mquickjs_wifi_csi_resources_set_accepting(
        &session->resources, true);
    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        session->last_error_stage = "esp_wifi_set_csi";
        esp32_mquickjs_wifi_csi_resources_set_accepting(
            &session->resources, false);
        goto fail_callback;
    }
    session->csi_enabled = true;
    session->last_error = ESP_OK;
    session->last_error_stage = NULL;
    atomic_store_explicit(&session->lifecycle, WIFI_CSI_RUNNING,
                          memory_order_release);
    return ESP_OK;

fail_callback:
    if (esp_wifi_set_csi_rx_cb(NULL, NULL) == ESP_OK) {
        session->callback_registered = false;
    }
fail_radio_policy:
    if (!session->callback_registered) {
        esp32_mquickjs_wifi_radio_release_promiscuous(
            &session->promiscuous_lease);
        esp32_mquickjs_wifi_radio_release_channel(&session->radio_lease);
    }
    return err;
}

static void wifi_csi_finish_stop(wifi_csi_session_t *session)
{
    if (session == NULL ||
        atomic_load_explicit(&session->resources.callbacks_active,
                             memory_order_acquire) != 0U ||
        session->csi_enabled || session->callback_registered) return;
    if (session->event_queue != NULL) {
        (void)esp32_mquickjs_event_queue_discard_all(session->event_queue);
    }
    esp32_mquickjs_wifi_radio_release_promiscuous(
        &session->promiscuous_lease);
    esp32_mquickjs_wifi_radio_release_channel(&session->radio_lease);
    atomic_store_explicit(&session->lifecycle, WIFI_CSI_STOPPED,
                          memory_order_release);
}

static void wifi_csi_finish_close(wifi_csi_session_t *session)
{
    esp32_mquickjs_event_queue_t *queue;

    if (session == NULL || session->csi_enabled ||
        session->callback_registered ||
        atomic_load_explicit(&session->resources.callbacks_active,
                             memory_order_acquire) != 0U) return;
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
    esp32_mquickjs_wifi_radio_release(&session->radio_lease);
    wifi_csi_maybe_destroy_resources(session);
}

static void wifi_csi_cleanup_worker(void *opaque)
{
    wifi_csi_session_t *session = opaque;
    esp32_mquickjs_runtime_t *runtime;

    if (session == NULL) return;
    runtime = session->runtime;
    while (atomic_load_explicit(&session->resources.callbacks_active,
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
    if (lifecycle == WIFI_CSI_CLOSED || lifecycle == WIFI_CSI_STOPPED ||
        lifecycle == WIFI_CSI_STOPPING) return ESP_OK;
    atomic_store_explicit(&session->lifecycle, WIFI_CSI_STOPPING,
                          memory_order_release);
    esp32_mquickjs_wifi_csi_resources_set_accepting(
        &session->resources, false);
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
    if (atomic_load_explicit(&session->resources.callbacks_active,
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
    wifi_csi_session_t *session = opaque;
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
            session->runtime, wifi_csi_reap, session);
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

static JSValue wifi_csi_requested_to_js(JSContext *ctx,
                                        const wifi_csi_options_t *options)
{
    JSGCRef result_ref, capture_ref, filter_ref, queue_ref;
    JSGCRef source_macs_ref, destination_macs_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *capture = JS_PushGCRef(ctx, &capture_ref);
    JSValue *filter = JS_PushGCRef(ctx, &filter_ref);
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    JSValue *source_macs = JS_PushGCRef(ctx, &source_macs_ref);
    JSValue *destination_macs = JS_PushGCRef(ctx, &destination_macs_ref);

    *result = JS_NewObject(ctx);
    *capture = wifi_csi_capture_to_js(ctx, options);
    *filter = JS_NewObject(ctx);
    *queue = JS_NewObject(ctx);
    *source_macs = wifi_csi_mac_array(
        ctx, options->filter.source_macs, options->filter.source_mac_count);
    *destination_macs = wifi_csi_mac_array(
        ctx, options->filter.destination_macs,
        options->filter.destination_mac_count);
    if (JS_IsException(*result) || JS_IsException(*capture) ||
        JS_IsException(*filter) || JS_IsException(*queue) ||
        JS_IsException(*source_macs) || JS_IsException(*destination_macs) ||
        !esp32_mquickjs_set_property_ref(
            ctx, filter, "sourceMac", *source_macs) ||
        !esp32_mquickjs_set_property_ref(
            ctx, filter, "destinationMac", *destination_macs) ||
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
            ctx, queue, "capacity",
            JS_NewUint32(ctx, options->queue_capacity)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, queue, "overflow", JS_NewString(ctx, "drop-newest")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "source",
            JS_NewString(ctx, wifi_csi_source_name(options->source))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "channel", options->fixed_channel
                ? JS_NewUint32(ctx, options->channel)
                : JS_NewString(ctx, "current")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "conflict", JS_NewString(ctx, "fail")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "capture", *capture) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "filter", *filter) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "queue", *queue) ||
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
    JS_PopGCRef(ctx, &destination_macs_ref);
    JS_PopGCRef(ctx, &source_macs_ref);
    JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &filter_ref);
    JS_PopGCRef(ctx, &capture_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    JS_PopGCRef(ctx, &destination_macs_ref);
    JS_PopGCRef(ctx, &source_macs_ref);
    JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &filter_ref);
    JS_PopGCRef(ctx, &capture_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_csi_frame_info_to_js(
    JSContext *ctx, const esp32_mquickjs_wifi_csi_slot_t *slot)
{
    const esp32_mquickjs_wifi_csi_metadata_t *metadata = &slot->metadata;
    JSGCRef result_ref, phy_ref, validity_ref, layout_ref;
    JSGCRef segments_ref, segment_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *phy = JS_PushGCRef(ctx, &phy_ref);
    JSValue *validity = JS_PushGCRef(ctx, &validity_ref);
    JSValue *layout = JS_PushGCRef(ctx, &layout_ref);
    JSValue *segments = JS_PushGCRef(ctx, &segments_ref);
    JSValue *segment = JS_PushGCRef(ctx, &segment_ref);
    char source_mac[18];
    char destination_mac[18];

    wifi_csi_format_mac(metadata->source_mac, source_mac);
    wifi_csi_format_mac(metadata->destination_mac, destination_mac);
    *result = JS_NewObject(ctx);
    *phy = JS_NewObject(ctx);
    *validity = JS_NewObject(ctx);
    *layout = JS_NewObject(ctx);
    *segments = JS_NewArray(ctx, 0);
    *segment = JS_NewObject(ctx);
    if (JS_IsException(*result) || JS_IsException(*phy) ||
        JS_IsException(*validity) || JS_IsException(*layout) ||
        JS_IsException(*segments) || JS_IsException(*segment) ||
        !esp32_mquickjs_set_property_ref(
            ctx, phy, "format",
            JS_NewString(ctx, wifi_csi_phy_name(metadata->phy))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, phy, "bandwidthMHz", metadata->bandwidth_available
                ? JS_NewUint32(ctx, metadata->bandwidth_mhz) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, phy, "mcs", metadata->mcs_available
                ? JS_NewUint32(ctx, metadata->mcs) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, phy, "stbc", metadata->stbc_available
                ? JS_NewBool(metadata->stbc) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, validity, "firstWordInvalid",
            JS_NewBool(metadata->first_word_invalid)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, validity, "channelEstimateValid",
            metadata->channel_estimate_valid_available
                ? JS_NewBool(metadata->channel_estimate_valid) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, validity, "truncated", JS_FALSE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, segment, "type", JS_NewString(ctx, "unknown")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, segment, "offsetBytes", JS_NewUint32(ctx, 0U)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, segment, "lengthBytes",
            JS_NewUint32(ctx, (uint32_t)slot->length)) ||
        JS_IsException(JS_SetPropertyUint32(ctx, *segments, 0U, *segment)))
        goto fail;
    *segment = JS_UNDEFINED;
    if (!esp32_mquickjs_set_property_ref(
            ctx, layout, "schema", JS_NewString(ctx,
                esp32_mquickjs_wifi_csi_target_is_he()
                    ? "wifi-csi-he-layout/1"
                    : "wifi-csi-legacy-layout/1")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, layout, "componentOrder",
            JS_NewString(ctx, "imaginary-real")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, layout, "sampleBits", metadata->sample_bits > 0U
                ? JS_NewUint32(ctx, metadata->sample_bits) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, layout, "byteLength",
            JS_NewUint32(ctx, (uint32_t)slot->length)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, layout, "segments", *segments) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sequence", JS_NewUint32(ctx, slot->sequence)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "timestampUs",
            JS_NewUint32(ctx, metadata->timestamp_us)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "rxSequence",
            JS_NewUint32(ctx, metadata->rx_sequence)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "generation",
            JS_NewUint32(ctx, slot->session_generation)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sourceMac", JS_NewString(ctx, source_mac)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "destinationMac",
            JS_NewString(ctx, destination_mac)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "rssi", JS_NewInt32(ctx, metadata->rssi)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "noiseFloor", metadata->noise_floor_available
                ? JS_NewInt32(ctx, metadata->noise_floor) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "channel", JS_NewUint32(ctx, metadata->channel)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "secondaryChannel",
            JS_NewString(ctx, wifi_csi_secondary_name(metadata->secondary))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "antenna", metadata->antenna_available
                ? JS_NewUint32(ctx, metadata->antenna) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "phy", *phy) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "validity", *validity) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "layout", *layout))
        goto fail;
    *phy = JS_UNDEFINED;
    *validity = JS_UNDEFINED;
    *layout = JS_UNDEFINED;
    *segments = JS_UNDEFINED;
    JS_PopGCRef(ctx, &segment_ref);
    JS_PopGCRef(ctx, &segments_ref);
    JS_PopGCRef(ctx, &layout_ref);
    JS_PopGCRef(ctx, &validity_ref);
    JS_PopGCRef(ctx, &phy_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    JS_PopGCRef(ctx, &segment_ref);
    JS_PopGCRef(ctx, &segments_ref);
    JS_PopGCRef(ctx, &layout_ref);
    JS_PopGCRef(ctx, &validity_ref);
    JS_PopGCRef(ctx, &phy_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
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
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_CSI_FRAME);
    if (JS_IsException(*object)) goto fail_close;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
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
        heap_caps_free(ref);
        goto fail_close;
    }
    *info = JS_UNDEFINED;
    atomic_fetch_add_explicit(
        &s_wifi_csi.resources.counters.delivered_frames, 1U,
        memory_order_relaxed);
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

static bool wifi_csi_frame_source_next(
    JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out)
{
    wifi_csi_frame_source_t *source = opaque;
    esp32_mquickjs_wifi_csi_slot_t *slot;

    (void)ctx;
    if (source == NULL || source->lease.released ||
        (slot = wifi_csi_resolve_event(&source->lease.event)) == NULL ||
        source->offset >= slot->length) return false;
    out->data = slot->payload + source->offset;
    out->length = slot->length - source->offset;
    out->owner = JS_UNDEFINED;
    out->dma_capable = false;
    source->offset = slot->length;
    return true;
}

static void wifi_csi_frame_source_iterator_close(JSContext *ctx, void *opaque)
{
    wifi_csi_frame_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) return;
    source->iterator_active = false;
    wifi_csi_lease_release(&source->lease);
    if (source->destroy_requested) heap_caps_free(source);
}

static bool wifi_csi_frame_source_open(
    JSContext *ctx, JSValue source_value, void *opaque,
    esp32_mquickjs_byte_span_source_t *out, JSValue *out_error)
{
    wifi_csi_frame_source_t *source = opaque;

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
    out->next = wifi_csi_frame_source_next;
    out->close = wifi_csi_frame_source_iterator_close;
    return true;
}

static size_t wifi_csi_frame_source_known_length(void *opaque)
{
    wifi_csi_frame_source_t *source = opaque;
    esp32_mquickjs_wifi_csi_slot_t *slot = source != NULL
        ? wifi_csi_resolve_event(&source->lease.event) : NULL;

    return slot != NULL && !source->lease.released ? slot->length : 0U;
}

static void wifi_csi_frame_source_destroy(JSContext *ctx, void *opaque)
{
    wifi_csi_frame_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) return;
    if (source->iterator_active) {
        source->destroy_requested = true;
        return;
    }
    wifi_csi_lease_release(&source->lease);
    heap_caps_free(source);
}

static const esp32_mquickjs_byte_span_source_object_ops_t
    s_wifi_csi_frame_source_ops = {
        .class_id = JS_CLASS_BYTE_SPAN_SOURCE,
        .open = wifi_csi_frame_source_open,
        .known_length = wifi_csi_frame_source_known_length,
        .destroy = wifi_csi_frame_source_destroy,
    };

static void wifi_csi_write_u16_le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static void wifi_csi_write_u32_le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static uint16_t wifi_csi_metadata_flags(
    const esp32_mquickjs_wifi_csi_metadata_t *metadata)
{
    uint16_t flags = 0U;

    if (metadata->destination_mac_available) flags |= 1U << 0U;
    if (metadata->noise_floor_available) flags |= 1U << 1U;
    if (metadata->antenna_available) flags |= 1U << 2U;
    if (metadata->mcs_available) flags |= 1U << 3U;
    if (metadata->bandwidth_available) flags |= 1U << 4U;
    if (metadata->stbc_available) flags |= 1U << 5U;
    if (metadata->stbc) flags |= 1U << 6U;
    if (metadata->first_word_invalid) flags |= 1U << 7U;
    if (metadata->channel_estimate_valid_available) flags |= 1U << 8U;
    if (metadata->channel_estimate_valid) flags |= 1U << 9U;
    return flags;
}

static void wifi_csi_encode_metadata(
    uint8_t output[WIFI_CSI_BATCH_METADATA_BYTES],
    const esp32_mquickjs_wifi_csi_slot_t *slot)
{
    const esp32_mquickjs_wifi_csi_metadata_t *metadata = &slot->metadata;

    memset(output, 0, WIFI_CSI_BATCH_METADATA_BYTES);
    wifi_csi_write_u32_le(output + 0U, slot->sequence);
    wifi_csi_write_u32_le(output + 4U, metadata->timestamp_us);
    wifi_csi_write_u32_le(output + 8U, metadata->rx_sequence);
    wifi_csi_write_u32_le(output + 12U, slot->session_generation);
    memcpy(output + 16U, metadata->source_mac, 6U);
    memcpy(output + 22U, metadata->destination_mac, 6U);
    output[28] = (uint8_t)metadata->rssi;
    output[29] = (uint8_t)metadata->noise_floor;
    output[30] = metadata->channel;
    output[31] = (uint8_t)metadata->secondary;
    output[32] = metadata->antenna_available
        ? metadata->antenna : UINT8_MAX;
    output[33] = (uint8_t)metadata->phy;
    output[34] = metadata->bandwidth_available
        ? metadata->bandwidth_mhz : 0U;
    output[35] = metadata->mcs_available ? metadata->mcs : UINT8_MAX;
    wifi_csi_write_u16_le(output + 36U,
                           wifi_csi_metadata_flags(metadata));
    output[38] = metadata->sample_bits;
    output[39] = 0xffU;
    wifi_csi_write_u32_le(output + 40U, (uint32_t)slot->length);
    wifi_csi_write_u32_le(output + 44U, 0U);
    wifi_csi_write_u32_le(output + 48U, (uint32_t)slot->length);
}

static bool wifi_csi_batch_source_build_control(
    wifi_csi_batch_source_t *source)
{
    size_t control_length = WIFI_CSI_BATCH_HEADER_BYTES +
        ((size_t)source->frame_count * WIFI_CSI_BATCH_DIRECTORY_BYTES) +
        ((size_t)source->frame_count * WIFI_CSI_BATCH_METADATA_BYTES);
    size_t metadata_base = WIFI_CSI_BATCH_HEADER_BYTES +
        ((size_t)source->frame_count * WIFI_CSI_BATCH_DIRECTORY_BYTES);
    size_t payload_base = control_length;
    size_t payload_bytes = 0U;
    uint16_t index;

    for (index = 0; index < source->frame_count; ++index) {
        esp32_mquickjs_wifi_csi_slot_t *slot =
            wifi_csi_resolve_event(&source->leases[index].event);

        if (slot == NULL || SIZE_MAX - payload_bytes < slot->length) {
            return false;
        }
        payload_bytes += slot->length;
    }
    if (control_length > UINT32_MAX || payload_bytes > UINT32_MAX ||
        payload_base > UINT32_MAX - payload_bytes) return false;
    source->control = heap_caps_calloc(
        1, control_length, MALLOC_CAP_8BIT);
    if (source->control == NULL) return false;
    source->control_length = control_length;
    memcpy(source->control, "E32QCSI1", 8U);
    wifi_csi_write_u16_le(source->control + 8U, WIFI_CSI_BINARY_VERSION);
    wifi_csi_write_u16_le(source->control + 10U,
                           WIFI_CSI_BATCH_HEADER_BYTES);
    wifi_csi_write_u32_le(source->control + 12U, source->frame_count);
    wifi_csi_write_u32_le(
        source->control + 16U,
        (uint32_t)source->frame_count * WIFI_CSI_BATCH_METADATA_BYTES);
    wifi_csi_write_u32_le(source->control + 20U, (uint32_t)payload_bytes);
    payload_bytes = 0U;
    for (index = 0; index < source->frame_count; ++index) {
        esp32_mquickjs_wifi_csi_slot_t *slot =
            wifi_csi_resolve_event(&source->leases[index].event);
        uint8_t *directory = source->control + WIFI_CSI_BATCH_HEADER_BYTES +
            ((size_t)index * WIFI_CSI_BATCH_DIRECTORY_BYTES);
        uint8_t *metadata = source->control + metadata_base +
            ((size_t)index * WIFI_CSI_BATCH_METADATA_BYTES);

        wifi_csi_write_u32_le(
            directory + 0U,
            (uint32_t)(metadata_base +
                ((size_t)index * WIFI_CSI_BATCH_METADATA_BYTES)));
        wifi_csi_write_u32_le(
            directory + 4U,
            (uint32_t)(payload_base + payload_bytes));
        wifi_csi_write_u32_le(directory + 8U, (uint32_t)slot->length);
        wifi_csi_write_u16_le(directory + 12U,
                              WIFI_CSI_BATCH_METADATA_BYTES);
        wifi_csi_encode_metadata(metadata, slot);
        payload_bytes += slot->length;
    }
    return true;
}

static void wifi_csi_batch_source_release(wifi_csi_batch_source_t *source)
{
    uint16_t index;

    if (source == NULL) return;
    for (index = 0; index < source->frame_count; ++index) {
        wifi_csi_lease_release(&source->leases[index]);
    }
}

static bool wifi_csi_batch_source_next(
    JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out)
{
    wifi_csi_batch_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) return false;
    if (!source->control_emitted) {
        source->control_emitted = true;
        out->data = source->control;
        out->length = source->control_length;
        out->owner = JS_UNDEFINED;
        out->dma_capable = false;
        return true;
    }
    while (source->payload_index < source->frame_count) {
        wifi_csi_lease_ref_t *lease =
            &source->leases[source->payload_index++];
        esp32_mquickjs_wifi_csi_slot_t *slot =
            wifi_csi_resolve_event(&lease->event);

        if (slot == NULL || lease->released) continue;
        out->data = slot->payload;
        out->length = slot->length;
        out->owner = JS_UNDEFINED;
        out->dma_capable = false;
        return true;
    }
    return false;
}

static void wifi_csi_batch_source_iterator_close(JSContext *ctx, void *opaque)
{
    wifi_csi_batch_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) return;
    source->iterator_active = false;
    wifi_csi_batch_source_release(source);
    if (source->destroy_requested) {
        heap_caps_free(source->control);
        heap_caps_free(source);
    }
}

static bool wifi_csi_batch_source_open(
    JSContext *ctx, JSValue source_value, void *opaque,
    esp32_mquickjs_byte_span_source_t *out, JSValue *out_error)
{
    wifi_csi_batch_source_t *source = opaque;

    (void)source_value;
    if (source == NULL || source->opened) {
        *out_error = JS_ThrowReferenceError(
            ctx, "WiFiCsiBatch source is closed or consumed");
        return false;
    }
    source->opened = true;
    source->iterator_active = true;
    out->opaque = source;
    out->next = wifi_csi_batch_source_next;
    out->close = wifi_csi_batch_source_iterator_close;
    return true;
}

static size_t wifi_csi_batch_source_known_length(void *opaque)
{
    wifi_csi_batch_source_t *source = opaque;
    size_t length;
    uint16_t index;

    if (source == NULL) return 0U;
    length = source->control_length;
    for (index = 0; index < source->frame_count; ++index) {
        esp32_mquickjs_wifi_csi_slot_t *slot =
            wifi_csi_resolve_event(&source->leases[index].event);
        if (slot != NULL && !source->leases[index].released) {
            length += slot->length;
        }
    }
    return length;
}

static void wifi_csi_batch_source_destroy(JSContext *ctx, void *opaque)
{
    wifi_csi_batch_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) return;
    if (source->iterator_active) {
        source->destroy_requested = true;
        return;
    }
    wifi_csi_batch_source_release(source);
    heap_caps_free(source->control);
    heap_caps_free(source);
}

static const esp32_mquickjs_byte_span_source_object_ops_t
    s_wifi_csi_batch_source_ops = {
        .class_id = JS_CLASS_BYTE_SPAN_SOURCE,
        .open = wifi_csi_batch_source_open,
        .known_length = wifi_csi_batch_source_known_length,
        .destroy = wifi_csi_batch_source_destroy,
    };

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

JSValue js_wifi_csi_capabilities(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
#ifdef CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS
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
    JSGCRef result_ref, sources_ref, formats_ref, limits_ref, supports_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *sources = JS_PushGCRef(ctx, &sources_ref);
    JSValue *formats = JS_PushGCRef(ctx, &formats_ref);
    JSValue *limits = JS_PushGCRef(ctx, &limits_ref);
    JSValue *supports = JS_PushGCRef(ctx, &supports_ref);
    bool he = esp32_mquickjs_wifi_csi_target_is_he();

    (void)this_val;
    (void)argc;
    (void)argv;
    *result = JS_NewObject(ctx);
#ifdef CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS
    *sources = wifi_csi_string_array(
        ctx, sources_promiscuous, 2U);
#else
    *sources = wifi_csi_string_array(ctx, sources_associated, 1U);
#endif
    *formats = he
        ? wifi_csi_string_array(ctx, phy_he,
                                sizeof(phy_he) / sizeof(phy_he[0]))
        : wifi_csi_string_array(ctx, phy_legacy,
                                sizeof(phy_legacy) / sizeof(phy_legacy[0]));
    *limits = JS_NewObject(ctx);
    *supports = JS_NewObject(ctx);
    if (JS_IsException(*result) || JS_IsException(*sources) ||
        JS_IsException(*formats) || JS_IsException(*limits) ||
        JS_IsException(*supports) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "maxFrameBytes",
            JS_NewUint32(ctx,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_FRAME_BYTES)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, limits, "maxPoolCapacity",
            JS_NewUint32(ctx,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY)) ||
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
            JS_NewBool(WIFI_CSI_FIXED_CHANNEL_SUPPORTED)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "promiscuous",
            JS_NewBool(WIFI_CSI_PROMISCUOUS_SUPPORTED)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "sourceMacFilter", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "destinationMacFilter", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "rssiFilter", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "nativeDecimation", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "nativeRateLimit", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "vht",
            JS_NewBool(esp32_mquickjs_wifi_csi_target_supports_vht())) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "he", JS_NewBool(he)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "heStbcSelection", JS_NewBool(he)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "manualScaling", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, supports, "lltfBitMode",
            JS_NewBool(
                esp32_mquickjs_wifi_csi_target_supports_lltf_bit_mode())) ||
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
        !esp32_mquickjs_set_property_ref(ctx, result, "limits", *limits) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "supports", *supports))
        goto fail;
    *sources = JS_UNDEFINED;
    *formats = JS_UNDEFINED;
    *limits = JS_UNDEFINED;
    *supports = JS_UNDEFINED;
    JS_PopGCRef(ctx, &supports_ref);
    JS_PopGCRef(ctx, &limits_ref);
    JS_PopGCRef(ctx, &formats_ref);
    JS_PopGCRef(ctx, &sources_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
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
            "wifiCsi.open(options) expects one options object");
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
    if (s_wifi_csi.resources.slots != NULL) {
        JS_ThrowReferenceError(ctx,
            "WIFI_CSI_CLEANUP_PENDING: retained frames still own the previous pool");
        goto fail;
    }
    generation = atomic_fetch_add_explicit(
        &s_wifi_csi_next_generation, 1U, memory_order_relaxed);
    if (generation == 0U) {
        generation = atomic_fetch_add_explicit(
            &s_wifi_csi_next_generation, 1U, memory_order_relaxed);
    }
    memset(&s_wifi_csi, 0, sizeof(s_wifi_csi));
    s_wifi_csi.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    atomic_init(&s_wifi_csi.lifecycle, WIFI_CSI_OPENING);
    atomic_init(&s_wifi_csi.cleanup_scheduled, false);
    atomic_init(&s_wifi_csi.close_requested, false);
    s_wifi_csi.generation = generation;
    s_wifi_csi.runtime = esp32_mquickjs_get_active_runtime();
    s_wifi_csi.options = options;
    allocator = wifi_csi_resource_allocator();
    if (!esp32_mquickjs_wifi_csi_resources_init(
            &s_wifi_csi.resources, generation,
            CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY,
            CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_FRAME_BYTES,
            &allocator)) {
        atomic_store(&s_wifi_csi.lifecycle, WIFI_CSI_CLOSED);
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    s_wifi_csi.resources.filter = options.filter;
    *queue = esp32_mquickjs_event_queue_new(
        ctx, s_wifi_csi.runtime,
        sizeof(esp32_mquickjs_wifi_csi_event_t), options.queue_capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        wifi_csi_event_to_js, wifi_csi_event_drop,
        wifi_csi_event_queue_close, &s_wifi_csi);
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
        const char *code = strcmp(
                s_wifi_csi.last_error_stage != NULL
                    ? s_wifi_csi.last_error_stage : "",
                "power_save_policy") == 0
            ? "WIFI_CSI_POWER_SAVE_CONFLICT"
            : s_wifi_csi.options.source == WIFI_CSI_SOURCE_PROMISCUOUS &&
                    s_wifi_csi.last_error_stage != NULL &&
                    strcmp(s_wifi_csi.last_error_stage,
                           "wifi_radio_acquire_promiscuous") == 0
                ? "WIFI_CSI_PROMISCUOUS_CONFLICT"
                : s_wifi_csi.options.fixed_channel &&
                        s_wifi_csi.last_error_stage != NULL &&
                        strcmp(s_wifi_csi.last_error_stage,
                               "wifi_radio_set_channel") == 0
                    ? "WIFI_CSI_RADIO_CONFLICT"
                    : "WIFI_CSI_DRIVER_ERROR";
        wifi_csi_throw_driver_error(
            ctx, code,
            s_wifi_csi.last_error_stage != NULL
                ? s_wifi_csi.last_error_stage : "start",
            err);
        goto fail_open;
    }
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_CSI_SESSION);
    if (JS_IsException(*object)) goto fail_open;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail_open;
    }
    ref->generation = generation;
    JS_SetOpaque(ctx, *object, ref);
    if (!esp32_mquickjs_set_property_ref(
            ctx, object, WIFI_CSI_EVENT_QUEUE_KEY, *queue)) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
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
                JS_NewString(ctx, "WIFI_CSI_DRIVER_ERROR")) ||
            !esp32_mquickjs_set_property_ref(
                ctx, error, "operation", JS_NewString(ctx, "wifiCsi")) ||
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
            ctx, effective, "maxFrameBytes",
            JS_NewUint32(ctx,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_FRAME_BYTES)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "queueCapacity",
            JS_NewUint32(ctx, session->options.queue_capacity)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "poolCapacity",
            JS_NewUint32(ctx,
                CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "powerSave",
            JS_NewString(ctx,
                wifi_csi_power_save_name(session->effective_power_save))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, effective, "timestampAccuracy",
            JS_NewString(ctx, session->effective_power_save == WIFI_PS_NONE
                ? "normal" : "power-save-dependent")) ||
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

static uint32_t wifi_csi_counter_value(
    wifi_csi_session_t *session,
    _Atomic uint32_t *live,
    uint32_t snapshot)
{
    return session->resources.slots != NULL
        ? atomic_load_explicit(live, memory_order_acquire) : snapshot;
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

JSValue js_wifi_csi_session_stats(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    wifi_csi_session_t *session;
    esp32_mquickjs_wifi_csi_counters_t *counters;
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
    counters = &session->resources.counters;
    if (session->resources.slots != NULL) {
        free_slots = esp32_mquickjs_native_pool_available(
            &session->resources.pool);
    }
    if (session->event_queue != NULL) {
        (void)esp32_mquickjs_event_queue_get_stats(
            session->event_queue, &queue_stats);
    }
    *result = JS_NewObject(ctx);
    *queue = JS_NewObject(ctx);
#define WIFI_CSI_COUNTER(name) wifi_csi_counter_value( \
    session, &counters->name, session->final_stats.name)
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
            ctx, result, "invalidChannelEstimate",
            JS_NewUint32(ctx,
                WIFI_CSI_COUNTER(invalid_channel_estimate))) ||
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
    if (atomic_load_explicit(&session->lifecycle,
                             memory_order_acquire) != WIFI_CSI_STOPPED) {
        return JS_ThrowReferenceError(ctx,
            "WIFI_CSI_NOT_RUNNING: configure requires stopped state");
    }
    if (!wifi_csi_parse_open_options(ctx, argv[0], &options)) {
        return JS_EXCEPTION;
    }
    if (options.queue_capacity != session->options.queue_capacity) {
        return JS_ThrowTypeError(ctx,
            "WIFI_CSI_CONFIG_UNSUPPORTED: queue capacity cannot change within a session");
    }
    session->options = options;
    session->resources.filter = options.filter;
    session->resources.filter_qualified = 0U;
    session->resources.last_accepted_timestamp_set = false;
    session->last_error = ESP_OK;
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
            ctx, "WIFI_CSI_DRIVER_ERROR",
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
    heap_caps_free(ref);
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
    heap_caps_free(ref);
}

JSValue js_wifi_csi_frame_samples(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    esp32_mquickjs_wifi_csi_event_t event;
    esp32_mquickjs_wifi_csi_slot_t *slot;
    wifi_csi_lease_ref_t *lease;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        (slot = wifi_csi_frame_from_value(
            ctx, *this_val, "WiFiCsiFrame.samples()", NULL, &event)) == NULL) {
        if (!JS_HasException(ctx)) {
            return JS_ThrowTypeError(
                ctx, "WiFiCsiFrame.samples() expects no arguments");
        }
        return JS_EXCEPTION;
    }
    lease = wifi_csi_retain_event(&event);
    if (lease == NULL) return JS_ThrowOutOfMemory(ctx);
    return esp32_mquickjs_new_retained_byte_view(
        ctx, slot->payload, slot->length,
        wifi_csi_byte_view_release, lease);
}

JSValue js_wifi_csi_frame_copy_samples(JSContext *ctx, JSValue *this_val,
                                       int argc, JSValue *argv)
{
    esp32_mquickjs_wifi_csi_slot_t *slot;
    uint8_t *copy = NULL;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        (slot = wifi_csi_frame_from_value(
            ctx, *this_val, "WiFiCsiFrame.copySamples()",
            NULL, NULL)) == NULL) {
        if (!JS_HasException(ctx)) {
            return JS_ThrowTypeError(
                ctx, "WiFiCsiFrame.copySamples() expects no arguments");
        }
        return JS_EXCEPTION;
    }
    if (slot->length > 0U) {
        copy = heap_caps_malloc(slot->length, MALLOC_CAP_8BIT);
        if (copy == NULL) return JS_ThrowOutOfMemory(ctx);
        memcpy(copy, slot->payload, slot->length);
    }
    return esp32_mquickjs_new_owned_byte_view(ctx, copy, slot->length);
}

JSValue js_wifi_csi_frame_source(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    esp32_mquickjs_wifi_csi_event_t event;
    esp32_mquickjs_wifi_csi_slot_t *slot;
    wifi_csi_frame_source_t *source;

    (void)argv;
    if (argc != 0 || this_val == NULL ||
        (slot = wifi_csi_frame_from_value(
            ctx, *this_val, "WiFiCsiFrame.source()", NULL, &event)) == NULL) {
        if (!JS_HasException(ctx)) {
            return JS_ThrowTypeError(
                ctx, "WiFiCsiFrame.source() expects no arguments");
        }
        return JS_EXCEPTION;
    }
    if (!esp32_mquickjs_wifi_csi_slot_retain(
            &s_wifi_csi.resources, slot)) {
        return JS_ThrowReferenceError(
            ctx, "WIFI_CSI_STALE_FRAME: could not retain frame source");
    }
    source = heap_caps_calloc(1, sizeof(*source), MALLOC_CAP_8BIT);
    if (source == NULL) {
        (void)esp32_mquickjs_wifi_csi_slot_release(
            &s_wifi_csi.resources, slot);
        return JS_ThrowOutOfMemory(ctx);
    }
    source->lease.event = event;
    return esp32_mquickjs_new_byte_span_source(
        ctx, *this_val, &s_wifi_csi_frame_source_ops, source);
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
    heap_caps_free(ref);
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
    heap_caps_free(batch);
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

JSValue js_wifi_csi_batch_samples(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
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
    lease = wifi_csi_retain_event(&batch->events[index]);
    if (lease == NULL) return JS_ThrowOutOfMemory(ctx);
    return esp32_mquickjs_new_retained_byte_view(
        ctx, slot->payload, slot->length,
        wifi_csi_byte_view_release, lease);
}

JSValue js_wifi_csi_batch_source(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    static const char *const allowed[] = {"format"};
    wifi_csi_batch_ref_t *batch;
    wifi_csi_batch_source_t *source;
    size_t allocation_size;
    uint16_t index;

    if (this_val == NULL ||
        (batch = wifi_csi_batch_from_value(
            ctx, *this_val, "WiFiCsiBatch.source()")) == NULL) {
        return JS_EXCEPTION;
    }
    if (argc > 1) {
        return JS_ThrowTypeError(
            ctx, "WiFiCsiBatch.source(options?) expects at most one argument");
    }
    if (argc == 1 && !JS_IsUndefined(argv[0])) {
        JSValue format;

        if (!esp32_mquickjs_validate_plain_options(
                ctx, argv[0], "WiFiCsiBatch.source()", allowed, 1U)) {
            return JS_EXCEPTION;
        }
        format = JS_GetPropertyStr(ctx, argv[0], "format");
        if (JS_IsException(format) ||
            (!JS_IsUndefined(format) &&
             !wifi_csi_string_equals(ctx, format, "esp32qjs-csi/1"))) {
            return JS_ThrowTypeError(
                ctx, "batch source format only supports esp32qjs-csi/1");
        }
    }
    allocation_size = sizeof(*source) +
        ((size_t)batch->frame_count * sizeof(source->leases[0]));
    source = heap_caps_calloc(1, allocation_size, MALLOC_CAP_8BIT);
    if (source == NULL) return JS_ThrowOutOfMemory(ctx);
    source->frame_count = batch->frame_count;
    for (index = 0; index < batch->frame_count; ++index) {
        esp32_mquickjs_wifi_csi_slot_t *slot =
            wifi_csi_resolve_event(&batch->events[index]);

        if (slot == NULL || !esp32_mquickjs_wifi_csi_slot_retain(
                &s_wifi_csi.resources, slot)) goto fail;
        source->leases[index].event = batch->events[index];
    }
    if (!wifi_csi_batch_source_build_control(source)) goto fail;
    return esp32_mquickjs_new_byte_span_source(
        ctx, *this_val, &s_wifi_csi_batch_source_ops, source);

fail:
    wifi_csi_batch_source_release(source);
    heap_caps_free(source->control);
    heap_caps_free(source);
    return JS_ThrowOutOfMemory(ctx);
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
    heap_caps_free(batch);
    return JS_TRUE;
}

JSValue js_wifi_csi_session_receive_batch(JSContext *ctx,
                                          JSValue *this_val,
                                          int argc,
                                          JSValue *argv)
{
    wifi_csi_session_t *session;
    wifi_csi_batch_ref_t *batch = NULL;
    wifi_csi_frame_ref_t *frame_ref;
    JSGCRef first_ref, object_ref;
    JSValue *first = JS_PushGCRef(ctx, &first_ref);
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    uint32_t maximum_frames = CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES;
    JSValue receive_argument;
    int receive_argc = 0;

    *first = JS_UNDEFINED;
    *object = JS_UNDEFINED;
    if (this_val == NULL ||
        (session = wifi_csi_session_from_value(
            ctx, *this_val, false)) == NULL || argc > 2) goto fail;
    if (argc >= 1 && !JS_IsUndefined(argv[0]) &&
        !esp32_mquickjs_value_to_bounded_u32(
            ctx, argv[0], 1U,
            CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES,
            &maximum_frames)) {
        JS_ThrowRangeError(ctx,
            "maximumFrames exceeds this Build Context");
        goto fail;
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        uint32_t timeout_ms;

        if (!esp32_mquickjs_value_to_bounded_u32(
                ctx, argv[1], 0U, UINT32_MAX, &timeout_ms)) {
            JS_ThrowRangeError(ctx,
                "timeoutMs must be a non-negative integer");
            goto fail;
        }
        receive_argument = argv[1];
        receive_argc = 1;
    }
    *first = js_wifi_csi_session_receive(
        ctx, this_val, receive_argc,
        receive_argc > 0 ? &receive_argument : NULL);
    if (JS_IsException(*first)) goto fail;
    if (JS_IsNull(*first)) {
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
    batch = heap_caps_calloc(
        1, sizeof(*batch) +
            ((size_t)maximum_frames * sizeof(batch->events[0])),
        MALLOC_CAP_8BIT);
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
    heap_caps_free(frame_ref);
    atomic_fetch_sub_explicit(
        &session->resources.counters.delivered_frames, 1U,
        memory_order_relaxed);
    while (batch->frame_count < maximum_frames) {
        esp32_mquickjs_wifi_csi_event_t event;

        if (!esp32_mquickjs_event_queue_try_receive(
                session->event_queue, &event)) break;
        if (wifi_csi_resolve_event(&event) != NULL) {
            batch->events[batch->frame_count++] = event;
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
    atomic_fetch_add_explicit(
        &session->resources.counters.delivered_batches, 1U,
        memory_order_relaxed);
    JSValue return_value = JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &first_ref);
    return return_value;

fail_batch:
    wifi_csi_batch_release_owner(batch);
    heap_caps_free(batch);
    batch = NULL;
fail:
    if (batch != NULL) {
        wifi_csi_batch_release_owner(batch);
        heap_caps_free(batch);
    }
    if (!JS_IsUndefined(*first) &&
        JS_GetClassID(ctx, *first) == JS_CLASS_WIFI_CSI_FRAME) {
        (void)js_wifi_csi_frame_close(ctx, first, 0, NULL);
    }
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
        s_wifi_csi.resources.slots != NULL) {
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
