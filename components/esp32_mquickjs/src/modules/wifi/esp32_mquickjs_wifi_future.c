#include "esp32_mquickjs_wifi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"

#include <inttypes.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_wifi.h"

typedef enum {
    WIFI_FUTURE_SCAN,
    WIFI_FUTURE_CONNECT,
} wifi_future_kind_t;

struct esp32_mquickjs_future_driver_state {
    wifi_future_kind_t kind;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint32_t generation;
    uint32_t timeout_ms;
    uint32_t scan_status;
    uint32_t connect_kind;
    int32_t connect_reason;
    char ssid[ESP32_MQUICKJS_WIFI_SSID_MAX_LEN + 1];
    char password[ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN + 1];
    bool started;
    bool completed;
    bool cancel_requested;
};

static bool wifi_future_parse_connect(JSContext *ctx,
                                      int argc,
                                      JSGCRef *argv,
                                      esp32_mquickjs_future_driver_state_t *state)
{
    JSCStringBuf ssid_buf;
    JSCStringBuf password_buf;
    const char *ssid;
    const char *password;
    size_t ssid_len = 0;
    size_t password_len = 0;

    if (argc < 2 || argc > 3 || !JS_IsString(ctx, argv[0].val) || !JS_IsString(ctx, argv[1].val)) {
        JS_ThrowTypeError(ctx,
                          "wifi.connect(ssid, password, timeoutMs?) expects two strings and an optional timeout");
        return false;
    }
    state->timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    if (argc == 3 &&
        (!JS_IsNumber(ctx, argv[2].val) ||
         esp32_mquickjs_wifi_value_to_timeout_ms(ctx,
                                                 argv[2].val,
                                                 ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS,
                                                 &state->timeout_ms) != 0)) {
        JS_ThrowTypeError(ctx, "wifi.connect(..., timeoutMs) expects a non-negative integer");
        return false;
    }
    ssid = JS_ToCStringLen(ctx, &ssid_len, argv[0].val, &ssid_buf);
    if (ssid == NULL || ssid_len == 0 || ssid_len > ESP32_MQUICKJS_WIFI_SSID_MAX_LEN) {
        JS_ThrowTypeError(ctx,
                          "wifi.connect(ssid, ...) expects an SSID of 1..%d bytes",
                          ESP32_MQUICKJS_WIFI_SSID_MAX_LEN);
        return false;
    }
    password = JS_ToCStringLen(ctx, &password_len, argv[1].val, &password_buf);
    if (password == NULL || password_len > ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN) {
        JS_ThrowTypeError(ctx,
                          "wifi.connect(..., password, ...) expects at most %d bytes",
                          ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN);
        return false;
    }
    memcpy(state->ssid, ssid, ssid_len);
    state->ssid[ssid_len] = '\0';
    memcpy(state->password, password, password_len);
    state->password[password_len] = '\0';
    return true;
}

static bool wifi_scan_future_prepare(JSContext *ctx,
                                     JSGCRef *this_ref,
                                     int argc,
                                     JSGCRef *argv,
                                     esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    (void)this_ref;
    (void)argv;
    if (argc != 0 || out_state == NULL) {
        JS_ThrowTypeError(ctx, "wifi.scan() expects no arguments");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->kind = WIFI_FUTURE_SCAN;
    state->timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    *out_state = state;
    return true;
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

static bool wifi_future_start(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              esp32_mquickjs_future_token_t token,
                              esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_wifi_state_t *wifi = esp32_mquickjs_wifi_state();
    wifi_scan_config_t scan_config = {
        .show_hidden = true,
    };
    esp_err_t err;

    if (state == NULL) {
        JS_ThrowInternalError(ctx, "Wi-Fi Future lost its driver state");
        return false;
    }
    err = esp32_mquickjs_wifi_ensure_started();
    if (err != ESP_OK) {
        if (state->kind == WIFI_FUTURE_SCAN) {
            esp32_mquickjs_wifi_throw_scan_error(ctx, err);
        } else if (state->kind == WIFI_FUTURE_CONNECT) {
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
            JS_ThrowInternalError(ctx, "wifi.scan() is already in progress");
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
            JS_ThrowInternalError(ctx, "wifi.connect() is already in progress");
            return false;
        }
        wifi->connect_generation++;
        state->generation = wifi->connect_generation;
        wifi->connect_future_registered = true;
        wifi->connect_future_token = token;
    }
    esp32_mquickjs_wifi_unlock();

    if (state->kind == WIFI_FUTURE_SCAN) {
        if (wifi->scan_queue != NULL) {
            xQueueReset(wifi->scan_queue);
        }
        err = esp_wifi_scan_start(&scan_config, false);
        if (err != ESP_OK) {
            esp32_mquickjs_wifi_clear_scan_future();
            esp32_mquickjs_wifi_throw_scan_error(ctx, err);
            return false;
        }
    } else {
        err = esp32_mquickjs_wifi_start_connect(state->ssid,
                                                state->password,
                                                state->timeout_ms);
        if (err != ESP_OK) {
            esp32_mquickjs_wifi_clear_connect_future();
            esp32_mquickjs_wifi_throw_connect_error(ctx, err);
            return false;
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
        return JS_ThrowInternalError(ctx, "Wi-Fi operation cancelled");
    }
    if (state->kind == WIFI_FUTURE_SCAN) {
        esp32_mquickjs_wifi_clear_scan_future();
        if (state->scan_status != 0) {
            return JS_ThrowInternalError(ctx,
                                         "wifi.scan() failed with status=%" PRIu32,
                                         state->scan_status);
        }
        return esp32_mquickjs_wifi_make_scan_results_array(ctx);
    }
    esp32_mquickjs_wifi_clear_connect_future();
    if (state->connect_kind == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS) {
        return esp32_mquickjs_wifi_make_status_object(ctx);
    }
    if (state->connect_kind == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT) {
        return JS_ThrowInternalError(ctx, "wifi.connect() timed out");
    }
    return JS_ThrowInternalError(ctx,
                                 "wifi.connect() failed (reason=%d:%s)",
                                 (int)state->connect_reason,
                                 esp32_mquickjs_wifi_reason_to_string(state->connect_reason));
}

static bool wifi_future_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_wifi_state_t *wifi = esp32_mquickjs_wifi_state();

    if (state == NULL || state->completed || state->cancel_requested) {
        return false;
    }
    state->cancel_requested = true;
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
    return true;
}

static void wifi_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->started && !state->completed) {
        (void)wifi_future_cancel(state);
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
    .prepare = wifi_scan_future_prepare,
    .start = wifi_future_start,
    .poll = wifi_future_poll,
    .finish = wifi_future_finish,
    .cancel = wifi_future_cancel,
    .destroy = wifi_future_destroy,
    .timeout_ms = wifi_future_timeout_ms,
};

static const esp32_mquickjs_future_driver_t s_wifi_connect_future_driver = {
    .prepare = wifi_connect_future_prepare,
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
    JSValue *global;
    JSValue *wifi;
    JSValue *scan;
    JSValue *connect;
    bool result = false;

    global = JS_PushGCRef(ctx, &global_ref);
    wifi = JS_PushGCRef(ctx, &wifi_ref);
    scan = JS_PushGCRef(ctx, &scan_ref);
    connect = JS_PushGCRef(ctx, &connect_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_GetPropertyStr(ctx, *global, "wifi");
    *scan = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "scan");
    *connect = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "connect");
    if (!JS_IsException(*global) && !JS_IsException(*wifi) &&
        !JS_IsException(*scan) && !JS_IsException(*connect) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *scan,
                                              &s_wifi_scan_future_driver) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *connect,
                                              &s_wifi_connect_future_driver)) {
        result = true;
    } else if (!JS_IsException(*scan) && !JS_IsException(*connect)) {
        JS_ThrowInternalError(ctx, "failed to register Wi-Fi Future drivers");
    }
    JS_PopGCRef(ctx, &connect_ref);
    JS_PopGCRef(ctx, &scan_ref);
    JS_PopGCRef(ctx, &wifi_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

#endif
