#include "esp32_mquickjs_wifi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include "esp32_mquickjs_core.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "esp32qjs_wifi";

static bool wifi_async_poller(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              void *opaque);

static JSValue wifi_scan_async(JSContext *ctx, JSValue callback)
{
    esp32_mquickjs_wifi_state_t *state = esp32_mquickjs_wifi_state();
    JSValue *callback_value;
    esp_err_t err;

    if (!JS_IsFunction(ctx, callback)) {
        return JS_ThrowTypeError(ctx, "wifi.async.scan(callback) expects a function");
    }

    err = esp32_mquickjs_wifi_ensure_started();
    if (err != ESP_OK) {
        return esp32_mquickjs_wifi_throw_scan_error(ctx, err);
    }

    esp32_mquickjs_wifi_lock();
    if (state->scan_in_progress || state->scan_callback_registered) {
        esp32_mquickjs_wifi_unlock();
        return JS_ThrowInternalError(ctx, "wifi.scan() is already in progress");
    }
    state->scan_generation++;
    esp32_mquickjs_wifi_set_scanning_locked(true);
    state->scan_callback_registered = true;
    esp32_mquickjs_wifi_unlock();

    if (state->scan_queue != NULL) {
        xQueueReset(state->scan_queue);
    }

    callback_value = JS_AddGCRef(ctx, &state->scan_callback);
    *callback_value = callback;

    err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        esp32_mquickjs_wifi_lock();
        esp32_mquickjs_wifi_set_scanning_locked(false);
        esp32_mquickjs_wifi_unlock();
        esp32_mquickjs_wifi_clear_scan_callback(ctx);
        return esp32_mquickjs_wifi_throw_scan_error(ctx, err);
    }

    return JS_UNDEFINED;
}

static JSValue wifi_connect_async_js(JSContext *ctx,
                                     const char *ssid,
                                     const char *password,
                                     uint32_t timeout_ms,
                                     JSValue callback)
{
    esp32_mquickjs_wifi_state_t *state = esp32_mquickjs_wifi_state();
    JSValue *callback_value;
    esp_err_t err;

    if (!JS_IsFunction(ctx, callback)) {
        return JS_ThrowTypeError(ctx, "wifi.async.connect(..., callback) expects a function");
    }

    err = esp32_mquickjs_wifi_ensure_started();
    if (err != ESP_OK) {
        return esp32_mquickjs_wifi_throw_connect_error(ctx, err);
    }

    esp32_mquickjs_wifi_lock();
    if (state->connect_callback_registered) {
        esp32_mquickjs_wifi_unlock();
        return JS_ThrowInternalError(ctx, "wifi.connect() is already in progress");
    }
    state->connect_generation++;
    state->connect_callback_registered = true;
    esp32_mquickjs_wifi_unlock();

    callback_value = JS_AddGCRef(ctx, &state->connect_callback);
    *callback_value = callback;

    err = esp32_mquickjs_wifi_connect_async(ssid, password, timeout_ms);
    if (err != ESP_OK) {
        esp32_mquickjs_wifi_clear_connect_callback(ctx);
        return esp32_mquickjs_wifi_throw_connect_error(ctx, err);
    }

    return JS_UNDEFINED;
}

bool esp32_mquickjs_init_wifi_async_runtime(JSContext *ctx,
                                            esp32_mquickjs_runtime_t *runtime)
{
    if (!esp32_mquickjs_register_async_poller(runtime, wifi_async_poller, NULL)) {
        JS_ThrowInternalError(ctx, "failed to register wifi async poller");
        return false;
    }
    return true;
}

JSValue js_wifi_async_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;

    if (argc != 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "wifi.async.scan(callback) expects a callback function");
    }

    return wifi_scan_async(ctx, argv[0]);
}

JSValue js_wifi_async_connect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSCStringBuf ssid_buf;
    JSCStringBuf password_buf;
    const char *ssid;
    const char *password;
    uint32_t timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    JSValue callback = JS_UNDEFINED;

    (void)this_val;

    if (argc < 3 || argc > 4 || !JS_IsString(ctx, argv[0]) || !JS_IsString(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx,
                                 "wifi.async.connect(ssid, password, callback) or wifi.async.connect(ssid, password, timeoutMs, callback) expects two strings, an optional timeout, and a callback");
    }

    if (argc == 3) {
        callback = argv[2];
    } else {
        if (esp32_mquickjs_wifi_value_to_timeout_ms(ctx,
                                                    argv[2],
                                                    ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS,
                                                    &timeout_ms) != 0) {
            return JS_ThrowTypeError(ctx, "wifi.async.connect(..., timeoutMs) expects a non-negative integer");
        }
        callback = argv[3];
    }

    if (!JS_IsFunction(ctx, callback)) {
        return JS_ThrowTypeError(ctx, "wifi.async.connect(..., callback) expects a callback function");
    }

    ssid = JS_ToCString(ctx, argv[0], &ssid_buf);
    password = JS_ToCString(ctx, argv[1], &password_buf);
    return wifi_connect_async_js(ctx, ssid, password, timeout_ms, callback);
}

static bool wifi_async_poller(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              void *opaque)
{
    esp32_mquickjs_wifi_state_t *state = esp32_mquickjs_wifi_state();
    esp32_mquickjs_wifi_scan_event_t scan_event;
    esp32_mquickjs_wifi_connect_event_t connect_event;
    bool needs_redraw = false;

    (void)opaque;
    (void)runtime;
    if (ctx == NULL || state->scan_queue == NULL) {
        return false;
    }

    while (xQueueReceive(state->scan_queue, &scan_event, 0) == pdTRUE) {
        JSGCRef callback_ref;
        JSValue *callback_fn;
        JSValue callback_ret;
        JSValue argv[2];
        JSValue results;
        bool callback_matches;

        esp32_mquickjs_wifi_lock();
        callback_matches = state->scan_callback_registered &&
                           scan_event.generation == state->scan_generation;
        esp32_mquickjs_wifi_unlock();
        if (!callback_matches) {
            continue;
        }

        if (JS_StackCheck(ctx, 4)) {
            esp32_mquickjs_wifi_lock();
            state->scan_callback_registered = false;
            esp32_mquickjs_wifi_unlock();
            JS_DeleteGCRef(ctx, &state->scan_callback);
            ESP_LOGW(TAG, "Skipping Wi-Fi scan callback due to JS stack pressure");
            needs_redraw = true;
            continue;
        }

        callback_fn = JS_PushGCRef(ctx, &callback_ref);
        *callback_fn = state->scan_callback.val;

        esp32_mquickjs_wifi_lock();
        state->scan_callback_registered = false;
        esp32_mquickjs_wifi_unlock();
        JS_DeleteGCRef(ctx, &state->scan_callback);

        if (scan_event.status != 0) {
            char message[96];

            ESP_LOGW(TAG, "Wi-Fi scan completed with failure status=%" PRIu32, scan_event.status);
            needs_redraw = true;
            snprintf(message, sizeof(message), "wifi.scan() failed with status=%" PRIu32, scan_event.status);
            argv[0] = JS_UNDEFINED;
            argv[1] = JS_NewString(ctx, message);
        } else {
            results = esp32_mquickjs_wifi_make_scan_results_array(ctx);
            if (JS_IsException(results)) {
                esp32_mquickjs_print_exception(ctx);
                JS_PopGCRef(ctx, &callback_ref);
                continue;
            }
            argv[0] = results;
            argv[1] = JS_UNDEFINED;
        }
        if (JS_IsException(argv[0]) || JS_IsException(argv[1])) {
            esp32_mquickjs_print_exception(ctx);
            JS_PopGCRef(ctx, &callback_ref);
            continue;
        }

        callback_ret = esp32_mquickjs_call(ctx,
                                            runtime,
                                            *callback_fn,
                                            JS_NULL,
                                            2,
                                            argv);
        if (JS_IsException(callback_ret)) {
            esp32_mquickjs_print_exception(ctx);
        }

        JS_PopGCRef(ctx, &callback_ref);
    }

    while (xQueueReceive(state->connect_queue, &connect_event, 0) == pdTRUE) {
        JSGCRef callback_ref;
        JSValue *callback_fn;
        JSValue argv[2];
        JSValue callback_ret;
        bool callback_matches;

        esp32_mquickjs_wifi_lock();
        callback_matches = state->connect_callback_registered &&
                           connect_event.generation == state->connect_generation;
        esp32_mquickjs_wifi_unlock();
        if (!callback_matches) {
            continue;
        }

        if (JS_StackCheck(ctx, 4)) {
            esp32_mquickjs_wifi_lock();
            state->connect_callback_registered = false;
            esp32_mquickjs_wifi_unlock();
            JS_DeleteGCRef(ctx, &state->connect_callback);
            ESP_LOGW(TAG, "Skipping Wi-Fi connect callback due to JS stack pressure");
            needs_redraw = true;
            continue;
        }

        callback_fn = JS_PushGCRef(ctx, &callback_ref);
        *callback_fn = state->connect_callback.val;

        esp32_mquickjs_wifi_lock();
        state->connect_callback_registered = false;
        esp32_mquickjs_wifi_unlock();
        JS_DeleteGCRef(ctx, &state->connect_callback);

        if (connect_event.kind == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS) {
            argv[0] = esp32_mquickjs_wifi_make_status_object(ctx);
            argv[1] = JS_UNDEFINED;
            if (JS_IsException(argv[0])) {
                JS_PopGCRef(ctx, &callback_ref);
                return true;
            }
        } else if (connect_event.kind == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT) {
            argv[0] = JS_UNDEFINED;
            argv[1] = JS_NewString(ctx, "wifi.connect() timed out");
        } else {
            esp32_mquickjs_wifi_status_t status = {0};
            char message[160];

            esp32_mquickjs_wifi_get_status(&status);
            snprintf(message,
                     sizeof(message),
                     "wifi.connect() failed for %s (reason=%d:%s, err=%s)",
                     status.ssid[0] != '\0' ? status.ssid : "<unknown>",
                     (int)connect_event.reason,
                     esp32_mquickjs_wifi_reason_to_string(connect_event.reason),
                     esp_err_to_name(ESP_FAIL));
            argv[0] = JS_UNDEFINED;
            argv[1] = JS_NewString(ctx, message);
        }

        callback_ret = esp32_mquickjs_call(ctx,
                                            runtime,
                                            *callback_fn,
                                            JS_NULL,
                                            2,
                                            argv);
        if (JS_IsException(callback_ret)) {
            esp32_mquickjs_print_exception(ctx);
            needs_redraw = true;
        }

        JS_PopGCRef(ctx, &callback_ref);
    }

    return needs_redraw;
}

#endif
