#include "esp32_mquickjs_wifi_twt_options.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT
#include "esp32_mquickjs_options.h"
#include <string.h>

uint64_t esp32_mquickjs_wifi_itwt_interval_us(const wifi_itwt_setup_config_t *config)
{
    /* A 16-bit mantissa shifted by a 5-bit exponent fits uint64, but not uint32. */
    return config ? (uint64_t)config->wake_invl_mant << config->wake_invl_expn : 0;
}

uint32_t esp32_mquickjs_wifi_itwt_duration_us(const wifi_itwt_setup_config_t *config)
{
    return config ? (uint32_t)config->min_wake_dura * (config->wake_duration_unit ? 1024U : 256U) : 0;
}

bool esp32_mquickjs_wifi_itwt_options_valid(const esp32_mquickjs_wifi_itwt_options_t *options)
{
    if (!options || !options->timeout_ms || options->timeout_ms > ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS)
        return false;
    const wifi_itwt_setup_config_t *c = &options->config;
    if ((unsigned)c->setup_cmd > TWT_DEMAND || c->reserved || !c->min_wake_dura ||
        !c->wake_invl_mant || c->twt_id > 32767U || c->timeout_time_ms < 100U)
        return false;
    /* Fixed C5 SDK API validates at least 10 ms of sleep, even for REQUEST.
     * Apply the public header's maximum to sleep, not directly to interval. */
    uint64_t interval = esp32_mquickjs_wifi_itwt_interval_us(c);
    uint32_t duration = esp32_mquickjs_wifi_itwt_duration_us(c);
    return interval >= (uint64_t)duration + 10000U &&
        interval - duration <= ESP32_MQUICKJS_WIFI_TWT_MAX_SLEEP_US;
}

bool esp32_mquickjs_wifi_btwt_options_valid(const esp32_mquickjs_wifi_btwt_options_t *options)
{
    return options && options->timeout_ms && options->timeout_ms <= ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS &&
        (unsigned)options->config.setup_cmd <= TWT_DEMAND && options->config.btwt_id >= 1U &&
        options->config.btwt_id <= 31U && options->config.timeout_time_ms != 0;
}

#define READ(name) do { stage = name; *value = JS_GetPropertyStr(ctx, *options, name); if (JS_IsException(*value)) goto done; } while (0)
#define NUMBER(name, low, high, target) do { READ(name); if (!JS_IsUndefined(*value)) { uint32_t n; \
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, low, high, &n)) { goto invalid; } target = n; } } while (0)
#define BOOLEAN(name, target) do { READ(name); if (!JS_IsUndefined(*value)) { \
    if (!JS_IsBool(*value)) { goto invalid; } target = *value == JS_TRUE; } } while (0)

bool esp32_mquickjs_wifi_capture_itwt(JSContext *ctx, JSValue input,
    esp32_mquickjs_wifi_itwt_options_t *output)
{
    if (!output) { JS_ThrowInternalError(ctx, "invalid iTWT capture output"); return false; }
    memset(output, 0, sizeof(*output));
    static const char *const keys[] = {"command", "flowId", "connectionId", "trigger", "announced",
        "wakeDurationUnit", "minimumWakeDuration", "wakeIntervalMantissa", "wakeIntervalExponent",
        "responseTimeoutMs", "timeoutMs"};
    static const char *const commands[] = {"request", "suggest", "demand"};
    static const char *const units[] = {"256us", "1024us"};
    JSGCRef options_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *value = JS_PushGCRef(ctx, &value_ref);
    *options = input;
    bool ok = false, announced = true;
    const char *stage = "options";
    esp32_mquickjs_wifi_itwt_options_t captured = {
        .config = {.setup_cmd = TWT_REQUEST, .trigger = 1, .min_wake_dura = 255,
            .wake_invl_mant = 512, .wake_invl_expn = 12,
            .timeout_time_ms = ESP32_MQUICKJS_WIFI_TWT_DEFAULT_RESPONSE_MS},
        .timeout_ms = ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS,
    };
    if (!esp32_mquickjs_validate_plain_options(ctx, *options, "iTWT setup", keys, sizeof(keys)/sizeof(keys[0]))) goto done;
    READ("command");
    if (!JS_IsUndefined(*value)) {
        size_t index;
        if (!esp32_mquickjs_value_to_enum(ctx, *value, commands, 3, &index)) goto invalid;
        captured.config.setup_cmd = (wifi_twt_setup_cmds_t)index;
    }
    NUMBER("flowId", 0, 7, captured.config.flow_id);
    READ("connectionId");
    if (!JS_IsUndefined(*value)) {
        uint32_t id;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, 32767, &id)) goto invalid;
        captured.config.twt_id = id;
        captured.connection_id_set = true;
    }
    BOOLEAN("trigger", captured.config.trigger);
    BOOLEAN("announced", announced);
    captured.config.flow_type = !announced;
    READ("wakeDurationUnit");
    if (!JS_IsUndefined(*value)) {
        size_t index;
        if (!esp32_mquickjs_value_to_enum(ctx, *value, units, 2, &index)) goto invalid;
        captured.config.wake_duration_unit = index;
    }
    NUMBER("minimumWakeDuration", 1, 255, captured.config.min_wake_dura);
    NUMBER("wakeIntervalMantissa", 1, 65535, captured.config.wake_invl_mant);
    NUMBER("wakeIntervalExponent", 0, 31, captured.config.wake_invl_expn);
    NUMBER("responseTimeoutMs", 100, 65535, captured.config.timeout_time_ms);
    NUMBER("timeoutMs", 1, ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS, captured.timeout_ms);
    stage = "timing";
    if (!esp32_mquickjs_wifi_itwt_options_valid(&captured)) goto invalid;
    *output = captured;
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid iTWT setup field: %s", stage);
done:
    JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &options_ref);
    return ok;
}

bool esp32_mquickjs_wifi_capture_btwt(JSContext *ctx, JSValue input,
    esp32_mquickjs_wifi_btwt_options_t *output)
{
    if (!output) { JS_ThrowInternalError(ctx, "invalid bTWT capture output"); return false; }
    memset(output, 0, sizeof(*output));
    static const char *const keys[] = {"command", "broadcastId", "responseTimeoutMs", "timeoutMs"};
    static const char *const commands[] = {"request", "suggest", "demand"};
    JSGCRef options_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *value = JS_PushGCRef(ctx, &value_ref);
    *options = input;
    bool ok = false;
    const char *stage = "options";
    esp32_mquickjs_wifi_btwt_options_t captured = {
        .config = {.setup_cmd = TWT_REQUEST, .timeout_time_ms = ESP32_MQUICKJS_WIFI_TWT_DEFAULT_RESPONSE_MS},
        .timeout_ms = ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS,
    };
    if (!esp32_mquickjs_validate_plain_options(ctx, *options, "bTWT setup", keys, sizeof(keys)/sizeof(keys[0]))) goto done;
    READ("command");
    if (!JS_IsUndefined(*value)) {
        size_t index;
        if (!esp32_mquickjs_value_to_enum(ctx, *value, commands, 3, &index)) goto invalid;
        captured.config.setup_cmd = (wifi_twt_setup_cmds_t)index;
    }
    READ("broadcastId");
    uint32_t id;
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 31, &id)) goto invalid;
    captured.config.btwt_id = id;
    NUMBER("responseTimeoutMs", 1, 65535, captured.config.timeout_time_ms);
    NUMBER("timeoutMs", 1, ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS, captured.timeout_ms);
    if (!esp32_mquickjs_wifi_btwt_options_valid(&captured)) goto invalid;
    *output = captured;
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid bTWT setup field: %s", stage);
done:
    JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &options_ref);
    return ok;
}
#undef READ
#undef NUMBER
#undef BOOLEAN
#endif
