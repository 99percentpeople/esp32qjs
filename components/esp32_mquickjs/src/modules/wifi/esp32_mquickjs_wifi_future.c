#include "esp32_mquickjs_wifi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_wifi.h"

typedef enum {
    WIFI_FUTURE_SCAN,
    WIFI_FUTURE_CONNECT,
    WIFI_FUTURE_DISCONNECT,
} wifi_future_kind_t;

static const char *wifi_future_operation_name(wifi_future_kind_t kind)
{
    return kind == WIFI_FUTURE_SCAN
               ? "wifi.scan"
               : kind == WIFI_FUTURE_CONNECT ? "wifi.connect"
                                             : "wifi.disconnect";
}

struct esp32_mquickjs_future_driver_state {
    wifi_future_kind_t kind;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint32_t generation;
    uint32_t timeout_ms;
    uint32_t scan_status;
    uint32_t connect_kind;
    int32_t connect_reason;
    wifi_scan_config_t scan_config;
    wifi_config_t connect_config;
    bool started;
    bool completed;
    bool cancel_requested;
};

static bool wifi_is_object(JSContext *ctx, JSValue value)
{
    return JS_GetClassID(ctx, value) >= 0 && !JS_IsArray(ctx, value);
}

static bool wifi_string_equals(JSContext *ctx, JSValue value,
                               const char *expected)
{
    JSCStringBuf buffer;
    const char *text;

    return JS_IsString(ctx, value) &&
           (text = JS_ToCString(ctx, value, &buffer)) != NULL &&
           strcmp(text, expected) == 0;
}

static bool wifi_validate_option_keys(JSContext *ctx, JSValue options,
                                      const char *api_name,
                                      const char *const *allowed,
                                      size_t allowed_count)
{
    return esp32_mquickjs_validate_plain_options(
        ctx, options, api_name, allowed, allowed_count);
}

static bool wifi_to_integer(JSContext *ctx, JSValue value,
                            int32_t minimum, int32_t maximum,
                            int32_t *out)
{
    double number;

    if (out == NULL || !JS_IsNumber(ctx, value) ||
        JS_ToNumber(ctx, &number, value) != 0 || !isfinite(number) ||
        floor(number) != number || number < minimum || number > maximum) {
        return false;
    }
    *out = (int32_t)number;
    return true;
}

static bool wifi_parse_bssid(JSContext *ctx, JSValue value,
                             uint8_t output[6])
{
    JSCStringBuf buffer;
    const char *text;
    unsigned int bytes[6];
    int consumed = 0;
    size_t index;

    if (!JS_IsString(ctx, value) ||
        (text = JS_ToCString(ctx, value, &buffer)) == NULL ||
        sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%n",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4],
               &bytes[5], &consumed) != 6 || text[consumed] != '\0') {
        return false;
    }
    for (index = 0; index < 6; ++index) {
        output[index] = (uint8_t)bytes[index];
    }
    return true;
}

static bool wifi_parse_auth_mode(JSContext *ctx, JSValue value,
                                 wifi_auth_mode_t *out)
{
    if (wifi_string_equals(ctx, value, "open")) {
        *out = WIFI_AUTH_OPEN;
    } else if (wifi_string_equals(ctx, value, "wep")) {
        *out = WIFI_AUTH_WEP;
    } else if (wifi_string_equals(ctx, value, "wpa")) {
        *out = WIFI_AUTH_WPA_PSK;
    } else if (wifi_string_equals(ctx, value, "wpa2")) {
        *out = WIFI_AUTH_WPA2_PSK;
    } else if (wifi_string_equals(ctx, value, "wpa/wpa2")) {
        *out = WIFI_AUTH_WPA_WPA2_PSK;
    } else if (wifi_string_equals(ctx, value, "wpa3")) {
        *out = WIFI_AUTH_WPA3_PSK;
    } else if (wifi_string_equals(ctx, value, "wpa2/wpa3")) {
        *out = WIFI_AUTH_WPA2_WPA3_PSK;
    } else if (wifi_string_equals(ctx, value, "wapi")) {
        *out = WIFI_AUTH_WAPI_PSK;
    } else if (wifi_string_equals(ctx, value, "owe")) {
        *out = WIFI_AUTH_OWE;
    } else {
        return false;
    }
    return true;
}

static bool wifi_future_parse_connect(JSContext *ctx,
                                      int argc,
                                      JSGCRef *argv,
                                      esp32_mquickjs_future_driver_state_t *state)
{
    static const char *const allowed[] = {
        "password", "timeoutMs", "bssid", "channel", "scanMethod",
        "sortMethod", "minimumRssi", "minimumAuthMode", "pmf",
    };
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    JSCStringBuf ssid_buf;
    JSCStringBuf password_buf;
    const char *ssid;
    const char *password = "";
    size_t ssid_len = 0;
    size_t password_len = 0;
    int32_t integer;
    bool result = false;

    if (argc < 1 || argc > 2 || !JS_IsString(ctx, argv[0].val) ||
        (argc == 2 && !wifi_is_object(ctx, argv[1].val))) {
        JS_ThrowTypeError(
            ctx,
            "wifi.connect(ssid, options?) expects a string SSID and optional options object");
        goto done;
    }
    if (argc == 2 &&
        !wifi_validate_option_keys(ctx, argv[1].val, "wifi.connect()",
                                   allowed, 9)) {
        goto done;
    }
    memset(&state->connect_config, 0, sizeof(state->connect_config));
    state->timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    state->connect_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    state->connect_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    state->connect_config.sta.threshold.rssi = -127;
    state->connect_config.sta.pmf_cfg.capable = true;

    ssid = JS_ToCStringLen(ctx, &ssid_len, argv[0].val, &ssid_buf);
    if (ssid == NULL || ssid_len == 0 || ssid_len > ESP32_MQUICKJS_WIFI_SSID_MAX_LEN) {
        JS_ThrowTypeError(ctx,
                          "wifi.connect(ssid, ...) expects 1..%d bytes",
                          ESP32_MQUICKJS_WIFI_SSID_MAX_LEN);
        goto done;
    }
    memcpy(state->connect_config.sta.ssid, ssid, ssid_len);
    state->connect_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    if (argc == 1) {
        result = true;
        goto done;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "password");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        password = JS_IsString(ctx, *property)
                       ? JS_ToCStringLen(ctx, &password_len, *property,
                                         &password_buf)
                       : NULL;
        if (password == NULL ||
            password_len > ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN) {
            JS_ThrowTypeError(
                ctx, "wifi.connect({ password }) expects at most %d bytes",
                ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN);
            goto done;
        }
        memcpy(state->connect_config.sta.password, password, password_len);
    }
    state->connect_config.sta.threshold.authmode =
        password_len > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    *property = JS_GetPropertyStr(ctx, argv[1].val, "timeoutMs");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (!wifi_to_integer(ctx, *property, 1, 60000, &integer)) {
            JS_ThrowRangeError(
                ctx, "wifi.connect({ timeoutMs }) expects 1..60000");
            goto done;
        }
        state->timeout_ms = (uint32_t)integer;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "bssid");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (!wifi_parse_bssid(ctx, *property,
                              state->connect_config.sta.bssid)) {
            JS_ThrowTypeError(
                ctx, "wifi.connect({ bssid }) expects xx:xx:xx:xx:xx:xx");
            goto done;
        }
        state->connect_config.sta.bssid_set = true;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "channel");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (!wifi_to_integer(ctx, *property, 1, 255, &integer)) {
            JS_ThrowRangeError(
                ctx, "wifi.connect({ channel }) expects 1..255");
            goto done;
        }
        state->connect_config.sta.channel = (uint8_t)integer;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "scanMethod");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (wifi_string_equals(ctx, *property, "fast")) {
            state->connect_config.sta.scan_method = WIFI_FAST_SCAN;
        } else if (wifi_string_equals(ctx, *property, "all")) {
            state->connect_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        } else {
            JS_ThrowRangeError(
                ctx, "wifi.connect({ scanMethod }) expects fast or all");
            goto done;
        }
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "sortMethod");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (wifi_string_equals(ctx, *property, "signal")) {
            state->connect_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        } else if (wifi_string_equals(ctx, *property, "security")) {
            state->connect_config.sta.sort_method = WIFI_CONNECT_AP_BY_SECURITY;
        } else {
            JS_ThrowRangeError(
                ctx, "wifi.connect({ sortMethod }) expects signal or security");
            goto done;
        }
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "minimumRssi");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (!wifi_to_integer(ctx, *property, -127, 0, &integer)) {
            JS_ThrowRangeError(
                ctx, "wifi.connect({ minimumRssi }) expects -127..0");
            goto done;
        }
        state->connect_config.sta.threshold.rssi = (int8_t)integer;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "minimumAuthMode");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property) &&
        !wifi_parse_auth_mode(
            ctx, *property, &state->connect_config.sta.threshold.authmode)) {
        JS_ThrowRangeError(
            ctx, "wifi.connect({ minimumAuthMode }) is not a supported personal auth mode");
        goto done;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "pmf");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (wifi_string_equals(ctx, *property, "disabled")) {
            state->connect_config.sta.pmf_cfg.capable = false;
            state->connect_config.sta.pmf_cfg.required = false;
        } else if (wifi_string_equals(ctx, *property, "capable")) {
            state->connect_config.sta.pmf_cfg.capable = true;
            state->connect_config.sta.pmf_cfg.required = false;
        } else if (wifi_string_equals(ctx, *property, "required")) {
            state->connect_config.sta.pmf_cfg.capable = true;
            state->connect_config.sta.pmf_cfg.required = true;
        } else {
            JS_ThrowRangeError(
                ctx, "wifi.connect({ pmf }) expects disabled, capable, or required");
            goto done;
        }
    }
    result = true;

done:
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool wifi_scan_future_prepare(JSContext *ctx,
                                     JSGCRef *this_ref,
                                     int argc,
                                     JSGCRef *argv,
                                     esp32_mquickjs_future_driver_state_t **out_state)
{
    static const char *const allowed[] = {
        "channel", "showHidden", "passive", "dwellMs", "timeoutMs",
    };
    esp32_mquickjs_future_driver_state_t *state;
    wifi_scan_config_t scan_config = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    int32_t integer;
    bool passive = false;
    bool result = false;

    (void)this_ref;
    if (out_state == NULL || argc < 0 || argc > 1 ||
        (argc == 1 && !wifi_is_object(ctx, argv[0].val))) {
        JS_ThrowTypeError(ctx, "wifi.scan(options?) expects an options object");
        goto done;
    }
    if (argc == 1 &&
        !wifi_validate_option_keys(ctx, argv[0].val, "wifi.scan()",
                                   allowed, 5)) {
        goto done;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    state->kind = WIFI_FUTURE_SCAN;
    state->timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    if (argc == 1) {
        *property = JS_GetPropertyStr(ctx, argv[0].val, "channel");
        if (JS_IsException(*property)) {
            goto fail;
        }
        if (!JS_IsUndefined(*property)) {
            if (!wifi_to_integer(ctx, *property, 1, 255, &integer)) {
                JS_ThrowRangeError(
                    ctx, "wifi.scan({ channel }) expects 1..255");
                goto fail;
            }
            scan_config.channel = (uint8_t)integer;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "showHidden");
        if (JS_IsException(*property)) {
            goto fail;
        }
        if (!JS_IsUndefined(*property)) {
            if (!JS_IsBool(*property)) {
                JS_ThrowTypeError(
                    ctx, "wifi.scan({ showHidden }) expects a boolean");
                goto fail;
            }
            scan_config.show_hidden = *property == JS_TRUE;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "passive");
        if (JS_IsException(*property)) {
            goto fail;
        }
        if (!JS_IsUndefined(*property)) {
            if (!JS_IsBool(*property)) {
                JS_ThrowTypeError(
                    ctx, "wifi.scan({ passive }) expects a boolean");
                goto fail;
            }
            passive = *property == JS_TRUE;
            scan_config.scan_type = passive
                                        ? WIFI_SCAN_TYPE_PASSIVE
                                        : WIFI_SCAN_TYPE_ACTIVE;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "dwellMs");
        if (JS_IsException(*property)) {
            goto fail;
        }
        if (!JS_IsUndefined(*property)) {
            if (!wifi_to_integer(ctx, *property, 1, 1500, &integer)) {
                JS_ThrowRangeError(
                    ctx, "wifi.scan({ dwellMs }) expects 1..1500");
                goto fail;
            }
            if (passive) {
                scan_config.scan_time.passive = (uint32_t)integer;
            } else {
                scan_config.scan_time.active.min = (uint32_t)integer;
                scan_config.scan_time.active.max = (uint32_t)integer;
            }
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
        if (JS_IsException(*property)) {
            goto fail;
        }
        if (!JS_IsUndefined(*property)) {
            if (!wifi_to_integer(ctx, *property, 1, 60000, &integer)) {
                JS_ThrowRangeError(
                    ctx, "wifi.scan({ timeoutMs }) expects 1..60000");
                goto fail;
            }
            state->timeout_ms = (uint32_t)integer;
        }
    }
    state->scan_config = scan_config;
    *out_state = state;
    result = true;
    goto done;

fail:
    heap_caps_free(state);
done:
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool wifi_connect_future_prepare(JSContext *ctx,
                                        JSGCRef *this_ref,
                                        int argc,
                                        JSGCRef *argv,
                                        esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    (void)this_ref;
    if (out_state == NULL) {
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->kind = WIFI_FUTURE_CONNECT;
    if (!wifi_future_parse_connect(ctx, argc, argv, state)) {
        heap_caps_free(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool wifi_disconnect_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    (void)this_ref;
    if (out_state == NULL || argc < 0 || argc > 1) {
        JS_ThrowTypeError(
            ctx, "wifi.disconnect(timeoutMs?) expects at most one timeout");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->kind = WIFI_FUTURE_DISCONNECT;
    state->timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    if (argc == 1 &&
        (!JS_IsNumber(ctx, argv[0].val) ||
         esp32_mquickjs_wifi_value_to_timeout_ms(
             ctx, argv[0].val, ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS,
             &state->timeout_ms) != 0)) {
        heap_caps_free(state);
        JS_ThrowTypeError(
            ctx, "wifi.disconnect(timeoutMs) expects a non-negative integer");
        return false;
    }
    *out_state = state;
    return true;
}

static bool wifi_future_start(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              esp32_mquickjs_future_token_t token,
                              esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_wifi_state_t *wifi = esp32_mquickjs_wifi_state();
    esp_err_t err;
    bool disconnect_pending = false;

    if (state == NULL) {
        JS_ThrowInternalError(ctx, "Wi-Fi Future lost its driver state");
        return false;
    }
    if (state->kind == WIFI_FUTURE_DISCONNECT &&
        (!wifi->initialized || !wifi->started)) {
        state->connect_kind =
            ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED;
        state->completed = true;
        state->started = true;
        return true;
    }
    err = esp32_mquickjs_wifi_ensure_started();
    if (err != ESP_OK) {
        if (state->kind == WIFI_FUTURE_SCAN) {
            esp32_mquickjs_wifi_throw_scan_error(ctx, err);
        } else {
            esp32_mquickjs_wifi_throw_connect_error(ctx, err);
        }
        return false;
    }
    state->runtime = runtime;
    state->token = token;

    esp32_mquickjs_wifi_lock();
    if (state->kind == WIFI_FUTURE_SCAN) {
        if (wifi->scan_in_progress || wifi->scan_future_registered) {
            esp32_mquickjs_wifi_unlock();
            esp32_mquickjs_wifi_throw_operation_error(
                ctx, "WIFI_SCAN_BUSY", "wifi.scan", ESP_ERR_INVALID_STATE,
                -1, UINT32_MAX);
            return false;
        }
        wifi->scan_generation++;
        state->generation = wifi->scan_generation;
        wifi->scan_future_registered = true;
        wifi->scan_future_token = token;
        esp32_mquickjs_wifi_set_scanning_locked(true);
    } else {
        if (wifi->connect_future_registered) {
            esp32_mquickjs_wifi_unlock();
            esp32_mquickjs_wifi_throw_operation_error(
                ctx, "WIFI_OPERATION_BUSY",
                wifi_future_operation_name(state->kind),
                ESP_ERR_INVALID_STATE, -1, UINT32_MAX);
            return false;
        }
        wifi->connect_generation++;
        state->generation = wifi->connect_generation;
        wifi->connect_future_registered = true;
        wifi->connect_future_token = token;
        wifi->connection_future_operation =
            state->kind == WIFI_FUTURE_CONNECT
                ? ESP32_MQUICKJS_WIFI_OPERATION_CONNECT
                : ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
    }
    esp32_mquickjs_wifi_unlock();

    if (state->kind == WIFI_FUTURE_SCAN) {
        if (wifi->scan_queue != NULL) {
            xQueueReset(wifi->scan_queue);
        }
        err = esp_wifi_scan_start(&state->scan_config, false);
        if (err != ESP_OK) {
            esp32_mquickjs_wifi_clear_scan_future();
            esp32_mquickjs_wifi_throw_scan_error(ctx, err);
            return false;
        }
    } else if (state->kind == WIFI_FUTURE_CONNECT) {
        err = esp32_mquickjs_wifi_start_connect(&state->connect_config,
                                                state->timeout_ms);
        if (err != ESP_OK) {
            esp32_mquickjs_wifi_clear_connect_future();
            esp32_mquickjs_wifi_throw_connect_error(ctx, err);
            return false;
        }
    } else {
        if (wifi->connect_queue != NULL) {
            xQueueReset(wifi->connect_queue);
        }
        err = esp32_mquickjs_wifi_start_disconnect(&disconnect_pending);
        if (err != ESP_OK) {
            esp32_mquickjs_wifi_clear_connect_future();
            esp32_mquickjs_wifi_throw_operation_error(
                ctx, "WIFI_DISCONNECT_FAILED", "wifi.disconnect", err,
                -1, UINT32_MAX);
            return false;
        }
        if (!disconnect_pending) {
            state->connect_kind =
                ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED;
            state->completed = true;
        }
    }
    state->started = true;
    return true;
}

static esp32_mquickjs_future_poll_t wifi_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_wifi_state_t *wifi = esp32_mquickjs_wifi_state();

    if (state == NULL) {
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->completed) {
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->kind == WIFI_FUTURE_SCAN) {
        esp32_mquickjs_wifi_scan_event_t event;

        while (wifi->scan_queue != NULL && xQueueReceive(wifi->scan_queue, &event, 0) == pdTRUE) {
            if (event.generation == state->generation) {
                state->scan_status = event.status;
                state->completed = true;
                break;
            }
        }
    } else {
        esp32_mquickjs_wifi_connect_event_t event;

        while (wifi->connect_queue != NULL &&
               xQueueReceive(wifi->connect_queue, &event, 0) == pdTRUE) {
            if (event.generation == state->generation) {
                state->connect_kind = event.kind;
                state->connect_reason = event.reason;
                state->completed = true;
                break;
            }
        }
    }
    return state->completed
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue wifi_future_finish(JSContext *ctx,
                                  esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->cancel_requested) {
        return esp32_mquickjs_wifi_throw_operation_error(
            ctx, "WIFI_CANCELLED",
            state != NULL ? wifi_future_operation_name(state->kind)
                          : "wifi",
            ESP_ERR_INVALID_STATE, -1, UINT32_MAX);
    }
    if (state->kind == WIFI_FUTURE_SCAN) {
        esp32_mquickjs_wifi_clear_scan_future();
        if (state->scan_status != 0) {
            return esp32_mquickjs_wifi_throw_operation_error(
                ctx, "WIFI_SCAN_FAILED", "wifi.scan", ESP_FAIL, -1,
                state->scan_status);
        }
        return esp32_mquickjs_wifi_make_scan_results_array(ctx);
    }
    esp32_mquickjs_wifi_clear_connect_future();
    if (state->kind == WIFI_FUTURE_DISCONNECT) {
        if (state->connect_kind ==
            ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED) {
            return esp32_mquickjs_wifi_make_status_object(ctx);
        }
        return esp32_mquickjs_wifi_throw_operation_error(
            ctx, "WIFI_DISCONNECT_FAILED", "wifi.disconnect",
            ESP_ERR_INVALID_STATE, state->connect_reason, UINT32_MAX);
    }
    if (state->connect_kind == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS) {
        return esp32_mquickjs_wifi_make_status_object(ctx);
    }
    if (state->connect_kind == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT) {
        return esp32_mquickjs_wifi_throw_operation_error(
            ctx, "WIFI_CONNECT_TIMEOUT", "wifi.connect", ESP_ERR_TIMEOUT,
            state->connect_reason, UINT32_MAX);
    }
    return esp32_mquickjs_wifi_throw_operation_error(
        ctx, "WIFI_CONNECT_FAILED", "wifi.connect", ESP_FAIL,
        state->connect_reason, UINT32_MAX);
}

static esp32_mquickjs_cancel_result_t wifi_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_wifi_state_t *wifi = esp32_mquickjs_wifi_state();

    if (state == NULL || state->completed || state->cancel_requested) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancel_requested = true;
    if (state->kind == WIFI_FUTURE_DISCONNECT && state->started) {
        state->cancel_requested = false;
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    if (state->kind == WIFI_FUTURE_SCAN) {
        (void)esp_wifi_scan_stop();
        esp32_mquickjs_wifi_lock();
        esp32_mquickjs_wifi_set_scanning_locked(false);
        esp32_mquickjs_wifi_unlock();
        esp32_mquickjs_wifi_clear_scan_future();
    } else {
        esp32_mquickjs_wifi_lock();
        wifi->connect_in_progress = false;
        wifi->ignore_disconnect_once = true;
        esp32_mquickjs_wifi_unlock();
        esp32_mquickjs_wifi_clear_connect_future();
        (void)esp_wifi_disconnect();
    }
    state->completed = true;
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    return ESP32_MQUICKJS_CANCELLED;
}

static void wifi_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->started && !state->completed) {
        if (state->kind == WIFI_FUTURE_DISCONNECT) {
            /*
             * esp_wifi_disconnect() cannot be rolled back once submitted.  The
             * Future may still be destroyed during runtime teardown, though,
             * so detach its generation/token before releasing the driver
             * state.  A later STA_DISCONNECTED event will then update Wi-Fi
             * status without waking a stale Future token.
             */
            esp32_mquickjs_wifi_clear_connect_future();
        } else {
            (void)wifi_future_cancel(state);
        }
    }
    heap_caps_free(state);
}

static uint32_t wifi_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return 0;
    }
    return state->timeout_ms;
}

static const esp32_mquickjs_future_driver_t s_wifi_scan_future_driver = {
    .capture = wifi_scan_future_prepare,
    .start = wifi_future_start,
    .poll = wifi_future_poll,
    .finish = wifi_future_finish,
    .cancel = wifi_future_cancel,
    .destroy = wifi_future_destroy,
    .timeout_ms = wifi_future_timeout_ms,
};

static const esp32_mquickjs_future_driver_t s_wifi_connect_future_driver = {
    .capture = wifi_connect_future_prepare,
    .start = wifi_future_start,
    .poll = wifi_future_poll,
    .finish = wifi_future_finish,
    .cancel = wifi_future_cancel,
    .destroy = wifi_future_destroy,
    .timeout_ms = wifi_future_timeout_ms,
};

static const esp32_mquickjs_future_driver_t s_wifi_disconnect_future_driver = {
    .capture = wifi_disconnect_future_prepare,
    .start = wifi_future_start,
    .poll = wifi_future_poll,
    .finish = wifi_future_finish,
    .cancel = wifi_future_cancel,
    .destroy = wifi_future_destroy,
    .timeout_ms = wifi_future_timeout_ms,
};

bool esp32_mquickjs_init_wifi_future_runtime(JSContext *ctx,
                                             esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref;
    JSGCRef wifi_ref;
    JSGCRef scan_ref;
    JSGCRef connect_ref;
    JSGCRef disconnect_ref;
    JSValue *global;
    JSValue *wifi;
    JSValue *scan;
    JSValue *connect;
    JSValue *disconnect;
    bool result = false;

    global = JS_PushGCRef(ctx, &global_ref);
    wifi = JS_PushGCRef(ctx, &wifi_ref);
    scan = JS_PushGCRef(ctx, &scan_ref);
    connect = JS_PushGCRef(ctx, &connect_ref);
    disconnect = JS_PushGCRef(ctx, &disconnect_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_GetPropertyStr(ctx, *global, "wifi");
    *scan = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "scan");
    *connect = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "connect");
    *disconnect = JS_IsException(*wifi)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "disconnect");
    if (!JS_IsException(*global) && !JS_IsException(*wifi) &&
        !JS_IsException(*scan) && !JS_IsException(*connect) &&
        !JS_IsException(*disconnect) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *scan,
                                              &s_wifi_scan_future_driver) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *connect,
                                              &s_wifi_connect_future_driver) &&
        esp32_mquickjs_future_register_driver(
            ctx, runtime, *disconnect,
            &s_wifi_disconnect_future_driver)) {
        result = true;
    } else if (!JS_IsException(*scan) && !JS_IsException(*connect) &&
               !JS_IsException(*disconnect)) {
        JS_ThrowInternalError(ctx, "failed to register Wi-Fi Future drivers");
    }
    JS_PopGCRef(ctx, &disconnect_ref);
    JS_PopGCRef(ctx, &connect_ref);
    JS_PopGCRef(ctx, &scan_ref);
    JS_PopGCRef(ctx, &wifi_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

#endif
