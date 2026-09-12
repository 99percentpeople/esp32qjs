#include "esp32_mquickjs_wifi_monitor_options.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_options.h"
#include <string.h>

static bool monitor_options_error(JSContext *ctx, const char *field)
{
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "wifi.monitor invalid option: %s", field);
    return false;
}

static bool monitor_options_u32(JSContext *ctx, JSValue *object, const char *key,
    uint32_t minimum, uint32_t maximum, uint32_t *output)
{
    JSValue value = JS_GetPropertyStr(ctx, *object, key);
    if (JS_IsException(value)) return false;
    return JS_IsUndefined(value) || esp32_mquickjs_value_to_bounded_u32(ctx, value, minimum, maximum, output) ||
        monitor_options_error(ctx, key);
}

static bool monitor_options_bool(JSContext *ctx, JSValue *object, const char *key, bool *output)
{
    JSValue value = JS_GetPropertyStr(ctx, *object, key);
    if (JS_IsException(value)) return false;
    if (JS_IsUndefined(value)) return true;
    if (!JS_IsBool(value)) return monitor_options_error(ctx, key);
    *output = value == JS_TRUE;
    return true;
}

static int monitor_options_hex(unsigned char byte)
{
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

static bool monitor_options_mac(JSContext *ctx, JSValue value, uint8_t output[6])
{
    if (!JS_IsString(ctx, value)) return false;
    JSCStringBuf buffer;
    size_t length;
    const char *text = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (text == NULL || length != 17) return false;
    for (unsigned i = 0; i < 6; ++i) {
        int high = monitor_options_hex((unsigned char)text[i * 3]);
        int low = monitor_options_hex((unsigned char)text[i * 3 + 1]);
        if (high < 0 || low < 0 || (i < 5 && text[i * 3 + 2] != ':')) return false;
        output[i] = (uint8_t)((unsigned)high * 16U + (unsigned)low);
    }
    return true;
}

static bool monitor_options_mac_list(JSContext *ctx, JSValue value, const char *field,
    esp32_mquickjs_wifi_rx_mac_filter_t *output)
{
    JSGCRef list_ref, item_ref;
    JSValue *list = JS_PushGCRef(ctx, &list_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *list = value; *item = JS_UNDEFINED;
    bool valid = false, single = JS_IsString(ctx, *list);
    uint32_t count = 1;
    if (!single) {
        if (!JS_IsArray(ctx, *list)) goto done;
        *item = JS_GetPropertyStr(ctx, *list, "length");
        if (JS_IsException(*item) || !esp32_mquickjs_value_to_bounded_u32(ctx, *item, 1,
                ESP32_MQUICKJS_WIFI_RX_FILTER_MAC_CAPACITY, &count)) goto done;
    }
    for (uint32_t i = 0; i < count; ++i) {
        *item = single ? *list : JS_GetPropertyUint32(ctx, *list, i);
        if (JS_IsException(*item) || !monitor_options_mac(ctx, *item, output->values[i])) goto done;
        for (uint32_t j = 0; j < i; ++j)
            if (memcmp(output->values[i], output->values[j], 6) == 0) goto done;
    }
    output->count = (uint8_t)count;
    valid = true;
done:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &list_ref);
    return valid || monitor_options_error(ctx, field);
}

static bool monitor_options_mask(JSContext *ctx, JSValue value, bool types, uint16_t *output)
{
    static const char *const names[] = {"management", "control", "data", "misc"};
    JSGCRef list_ref, item_ref;
    JSValue *list = JS_PushGCRef(ctx, &list_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *list = value; *item = JS_UNDEFINED;
    uint32_t count;
    uint16_t mask = 0;
    bool valid = false;
    if (!JS_IsArray(ctx, *list)) goto done;
    *item = JS_GetPropertyStr(ctx, *list, "length");
    if (JS_IsException(*item) || !esp32_mquickjs_value_to_bounded_u32(ctx, *item, 0, types ? 4 : 16, &count)) goto done;
    for (uint32_t i = 0; i < count; ++i) {
        *item = JS_GetPropertyUint32(ctx, *list, i);
        if (JS_IsException(*item)) goto done;
        uint32_t index;
        if (types) {
            size_t choice;
            if (!esp32_mquickjs_value_to_enum(ctx, *item, names, 4, &choice)) goto done;
            index = (uint32_t)choice;
        } else if (!esp32_mquickjs_value_to_bounded_u32(ctx, *item, 0, 15, &index)) goto done;
        uint16_t bit = (uint16_t)(1U << index);
        if ((mask & bit) != 0) goto done;
        mask |= bit;
    }
    *output = mask;
    valid = true;
done:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &list_ref);
    return valid || monitor_options_error(ctx, types ? "filter.types" : "filter.subtypes");
}

static bool monitor_options_filter(JSContext *ctx, JSValue value, esp32_mquickjs_wifi_rx_filter_t *filter)
{
    static const char *const allowed[] = {
        "types", "subtypes", "sourceMac", "destinationMac", "bssid", "minimumRssi",
        "sampleEvery", "maximumRateHz", "validOnly",
    };
    JSGCRef object_ref, value_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *property = JS_PushGCRef(ctx, &value_ref);
    *object = value; *property = JS_UNDEFINED;
    bool valid = false;
    uint16_t mask;
    if (!esp32_mquickjs_validate_plain_options(ctx, *object, "wifi.monitor filter", allowed, 9)) goto done;
    *property = JS_GetPropertyStr(ctx, *object, "types");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!monitor_options_mask(ctx, *property, true, &mask)) goto done;
        filter->type_mask = (uint8_t)mask;
    }
    *property = JS_GetPropertyStr(ctx, *object, "subtypes");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!monitor_options_mask(ctx, *property, false, &mask)) goto done;
        filter->subtype_filter = true; filter->subtype_mask = mask;
    }
    static const char *const mac_keys[] = {"sourceMac", "destinationMac", "bssid"};
    esp32_mquickjs_wifi_rx_mac_filter_t *mac_lists[] = {&filter->source, &filter->destination, &filter->bssid};
    for (unsigned i = 0; i < 3; ++i) {
        *property = JS_GetPropertyStr(ctx, *object, mac_keys[i]);
        if (JS_IsException(*property) || (!JS_IsUndefined(*property) &&
                !monitor_options_mac_list(ctx, *property, mac_keys[i], mac_lists[i]))) goto done;
    }
    *property = JS_GetPropertyStr(ctx, *object, "minimumRssi");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        int32_t rssi;
        if (!esp32_mquickjs_value_to_bounded_i32(ctx, *property, INT8_MIN, INT8_MAX, &rssi)) {
            monitor_options_error(ctx, "filter.minimumRssi"); goto done;
        }
        filter->minimum_rssi = (int8_t)rssi; filter->minimum_rssi_set = true;
    }
    if (!monitor_options_u32(ctx, object, "sampleEvery", 1, UINT32_MAX, &filter->sample_every) ||
        !monitor_options_u32(ctx, object, "maximumRateHz", 0, 1000000, &filter->maximum_rate_hz) ||
        !monitor_options_bool(ctx, object, "validOnly", &filter->valid_only)) goto done;
    valid = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &object_ref);
    return valid;
}

bool esp32_mquickjs_wifi_monitor_capture_options(JSContext *ctx, JSValue value,
    esp32_mquickjs_wifi_monitor_options_t *output)
{
    static const char *const allowed[] = {"channel", "filter", "capture", "buffering", "powerSavePolicy"};
    static const char *const capture_keys[] = {"snapLength", "requireComplete"};
    static const char *const buffering_keys[] = {"poolCapacity", "queueCapacity", "overflow"};
    static const char *const channel_names[] = {"current"};
    static const char *const ps_names[] = {"preserve", "require-none"};
    static const char *const overflow_names[] = {"drop-newest"};
    if (ctx == NULL) return false;
    if (output == NULL) return monitor_options_error(ctx, "native output");
    esp32_mquickjs_wifi_monitor_options_t parsed = {
        .capture = {.filter = {.type_mask = ESP32_MQUICKJS_WIFI_RX_FILTER_TYPE_MASK, .sample_every = 1, .valid_only = true}},
        .pool_capacity = ESP32_MQUICKJS_WIFI_MONITOR_DEFAULT_POOL_CAPACITY,
        .queue_capacity = ESP32_MQUICKJS_WIFI_MONITOR_DEFAULT_QUEUE_CAPACITY,
        .snap_length = ESP32_MQUICKJS_WIFI_MONITOR_DEFAULT_SNAP_LENGTH,
    };
    JSGCRef object_ref, section_ref, property_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *section = JS_PushGCRef(ctx, &section_ref);
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    *object = value; *section = JS_UNDEFINED; *property = JS_UNDEFINED;
    bool valid = false;
    size_t choice;
    if (JS_IsUndefined(*object)) { valid = true; goto done; }
    if (!esp32_mquickjs_validate_plain_options(ctx, *object, "wifi.monitor.open", allowed, 5)) goto done;
    *property = JS_GetPropertyStr(ctx, *object, "channel");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (JS_IsString(ctx, *property)) {
            if (!esp32_mquickjs_value_to_enum(ctx, *property, channel_names, 1, &choice)) {
                monitor_options_error(ctx, "channel"); goto done;
            }
        } else {
            uint32_t channel;
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *property, 1, UINT8_MAX, &channel)) {
                monitor_options_error(ctx, "channel"); goto done;
            }
            if (channel > 14) {
#if CONFIG_SOC_WIFI_SUPPORT_5G
                if (esp32_mquickjs_wifi_radio_5ghz_channel_bit((uint8_t)channel) == 0) {
                    monitor_options_error(ctx, "channel"); goto done;
                }
#else
                monitor_options_error(ctx, "channel"); goto done;
#endif
            }
            parsed.capture.channel = (uint8_t)channel;
        }
    }
    *property = JS_GetPropertyStr(ctx, *object, "powerSavePolicy");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *property, ps_names, 2, &choice)) {
            monitor_options_error(ctx, "powerSavePolicy"); goto done;
        }
        parsed.capture.require_power_save_none = choice == 1;
    }
    *section = JS_GetPropertyStr(ctx, *object, "filter");
    if (JS_IsException(*section) || (!JS_IsUndefined(*section) && !monitor_options_filter(ctx, *section, &parsed.capture.filter))) goto done;
    *section = JS_GetPropertyStr(ctx, *object, "capture");
    if (JS_IsException(*section)) goto done;
    if (!JS_IsUndefined(*section) &&
        (!esp32_mquickjs_validate_plain_options(ctx, *section, "wifi.monitor capture", capture_keys, 2) ||
         !monitor_options_u32(ctx, section, "snapLength", 1, ESP32_MQUICKJS_WIFI_MONITOR_MAX_SNAP_LENGTH, &parsed.snap_length) ||
         !monitor_options_bool(ctx, section, "requireComplete", &parsed.require_complete))) goto done;
    *section = JS_GetPropertyStr(ctx, *object, "buffering");
    if (JS_IsException(*section)) goto done;
    if (!JS_IsUndefined(*section)) {
        if (!esp32_mquickjs_validate_plain_options(ctx, *section, "wifi.monitor buffering", buffering_keys, 3) ||
            !monitor_options_u32(ctx, section, "poolCapacity", 1, ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY, &parsed.pool_capacity) ||
            !monitor_options_u32(ctx, section, "queueCapacity", 1, ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY, &parsed.queue_capacity)) goto done;
        *property = JS_GetPropertyStr(ctx, *section, "overflow");
        if (JS_IsException(*property)) goto done;
        if (!JS_IsUndefined(*property) && !esp32_mquickjs_value_to_enum(ctx, *property, overflow_names, 1, &choice)) {
            monitor_options_error(ctx, "buffering.overflow"); goto done;
        }
    }
    valid = true;
done:
    if (valid) *output = parsed;
    JS_PopGCRef(ctx, &property_ref);
    JS_PopGCRef(ctx, &section_ref);
    JS_PopGCRef(ctx, &object_ref);
    return valid;
}
bool esp32_mquickjs_wifi_monitor_capture_batch_options(JSContext *ctx, JSValue value,
    uint32_t pool_capacity, esp32_mquickjs_wifi_monitor_batch_options_t *output)
{
    if (ctx == NULL) return false;
    if (output == NULL || pool_capacity == 0 || pool_capacity > ESP32_MQUICKJS_WIFI_MONITOR_MAX_BATCH_FRAMES)
        return monitor_options_error(ctx, "batch pool capacity");
    static const char *const allowed[] = {"maximumFrames", "minimumFrames", "timeoutMs", "maximumLatencyMs"};
    esp32_mquickjs_wifi_monitor_batch_options_t parsed = {
        .maximum_frames = pool_capacity < ESP32_MQUICKJS_WIFI_MONITOR_DEFAULT_BATCH_FRAMES ?
            pool_capacity : ESP32_MQUICKJS_WIFI_MONITOR_DEFAULT_BATCH_FRAMES,
        .minimum_frames = 1,
    };
    JSGCRef object_ref, timeout_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *timeout = JS_PushGCRef(ctx, &timeout_ref);
    *object = value; *timeout = JS_UNDEFINED;
    bool valid = false;
    if (!JS_IsUndefined(*object)) {
        if (!esp32_mquickjs_validate_plain_options(ctx, *object, "WiFiMonitorSession.receiveBatch", allowed, 4) ||
            !monitor_options_u32(ctx, object, "maximumFrames", 1, pool_capacity, &parsed.maximum_frames) ||
            !monitor_options_u32(ctx, object, "minimumFrames", 1, pool_capacity, &parsed.minimum_frames) ||
            !monitor_options_u32(ctx, object, "maximumLatencyMs", 0, INT32_MAX, &parsed.maximum_latency_ms)) goto done;
        *timeout = JS_GetPropertyStr(ctx, *object, "timeoutMs");
        if (JS_IsException(*timeout)) goto done;
        if (!JS_IsUndefined(*timeout)) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *timeout, 0, INT32_MAX, &parsed.timeout_ms)) {
                monitor_options_error(ctx, "batch timeoutMs"); goto done;
            }
            parsed.timeout_set = true;
        }
    }
    if (parsed.minimum_frames > parsed.maximum_frames) { monitor_options_error(ctx, "batch minimumFrames"); goto done; }
    *output = parsed;
    valid = true;
done:
    JS_PopGCRef(ctx, &timeout_ref);
    JS_PopGCRef(ctx, &object_ref);
    return valid;
}
#endif
