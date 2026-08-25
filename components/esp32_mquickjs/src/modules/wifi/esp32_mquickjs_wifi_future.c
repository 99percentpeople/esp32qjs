#include "esp32_mquickjs_wifi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"

#define WIFI_TIME_SERVER_MAX_BYTES 253U
#define WIFI_TIME_VALID_AFTER_UNIX 1577836800LL
#define WIFI_TIME_MAX_WAITERS \
    (CONFIG_ESP32_MQUICKJS_MAX_FUTURES + \
     CONFIG_ESP32_MQUICKJS_INTERNAL_FUTURE_RESERVE)

typedef enum {
    WIFI_FUTURE_SCAN,
    WIFI_FUTURE_CONNECT,
    WIFI_FUTURE_SYNC_TIME,
} wifi_future_kind_t;

typedef struct {
    bool active;
    uint32_t generation;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
} wifi_time_waiter_t;

typedef struct {
    bool in_progress;
    bool sntp_initialized;
    bool synchronized;
    uint32_t generation;
    uint32_t completed_generation;
    esp_err_t completed_error;
    int64_t unix_time_ms;
    size_t waiter_count;
    wifi_time_waiter_t waiters[WIFI_TIME_MAX_WAITERS];
    char servers[CONFIG_LWIP_SNTP_MAX_SERVERS]
                [WIFI_TIME_SERVER_MAX_BYTES + 1U];
    size_t server_count;
} wifi_time_sync_state_t;

static wifi_time_sync_state_t s_wifi_time;

struct esp32_mquickjs_future_driver_state {
    wifi_future_kind_t kind;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint32_t generation;
    uint32_t timeout_ms;
    uint32_t scan_status;
    uint32_t connect_kind;
    int32_t connect_reason;
    size_t time_server_count;
    int time_waiter_index;
    int64_t unix_time_ms;
    esp_err_t time_error;
    char ssid[ESP32_MQUICKJS_WIFI_SSID_MAX_LEN + 1];
    char password[ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN + 1];
    char time_servers[CONFIG_LWIP_SNTP_MAX_SERVERS]
                     [WIFI_TIME_SERVER_MAX_BYTES + 1U];
    bool started;
    bool completed;
    bool cancel_requested;
};

static int64_t wifi_time_now_ms(void)
{
    struct timeval now = {0};

    gettimeofday(&now, NULL);
    return (int64_t)now.tv_sec * 1000LL + (int64_t)now.tv_usec / 1000LL;
}

static bool wifi_time_is_valid(void)
{
    return time(NULL) >= (time_t)WIFI_TIME_VALID_AFTER_UNIX;
}

static void wifi_time_wake_generation(uint32_t generation)
{
    size_t i;

    for (i = 0; i < WIFI_TIME_MAX_WAITERS; ++i) {
        wifi_time_waiter_t *waiter = &s_wifi_time.waiters[i];

        if (waiter->active && waiter->generation == generation &&
            waiter->runtime != NULL) {
            (void)esp32_mquickjs_future_wake(waiter->runtime, waiter->token);
        }
    }
}

static void wifi_time_sync_callback(struct timeval *tv)
{
    uint32_t generation;

    esp32_mquickjs_wifi_lock();
    if (!s_wifi_time.in_progress) {
        esp32_mquickjs_wifi_unlock();
        return;
    }
    generation = s_wifi_time.generation;
    s_wifi_time.in_progress = false;
    s_wifi_time.synchronized = true;
    s_wifi_time.completed_generation = generation;
    s_wifi_time.completed_error = ESP_OK;
    s_wifi_time.unix_time_ms = tv != NULL
        ? (int64_t)tv->tv_sec * 1000LL + (int64_t)tv->tv_usec / 1000LL
        : wifi_time_now_ms();
    wifi_time_wake_generation(generation);
    esp32_mquickjs_wifi_unlock();
}

static int wifi_time_add_waiter(esp32_mquickjs_runtime_t *runtime,
                                esp32_mquickjs_future_token_t token,
                                uint32_t generation)
{
    size_t i;

    for (i = 0; i < WIFI_TIME_MAX_WAITERS; ++i) {
        wifi_time_waiter_t *waiter = &s_wifi_time.waiters[i];

        if (waiter->active) {
            continue;
        }
        waiter->active = true;
        waiter->generation = generation;
        waiter->runtime = runtime;
        waiter->token = token;
        s_wifi_time.waiter_count++;
        return (int)i;
    }
    return -1;
}

static void wifi_time_remove_waiter(
    esp32_mquickjs_future_driver_state_t *state)
{
    bool deinit_sntp = false;

    if (state == NULL || state->time_waiter_index < 0 ||
        state->time_waiter_index >= WIFI_TIME_MAX_WAITERS) {
        return;
    }
    esp32_mquickjs_wifi_lock();
    if (s_wifi_time.waiters[state->time_waiter_index].active) {
        memset(&s_wifi_time.waiters[state->time_waiter_index], 0,
               sizeof(s_wifi_time.waiters[state->time_waiter_index]));
        if (s_wifi_time.waiter_count > 0) {
            s_wifi_time.waiter_count--;
        }
    }
    state->time_waiter_index = -1;
    if (s_wifi_time.waiter_count == 0 && s_wifi_time.sntp_initialized) {
        s_wifi_time.in_progress = false;
        s_wifi_time.sntp_initialized = false;
        deinit_sntp = true;
    }
    esp32_mquickjs_wifi_unlock();
    if (deinit_sntp) {
        esp_netif_sntp_deinit();
    }
}

void esp32_mquickjs_deinit_wifi_time_sync(void)
{
    bool deinit_sntp;
    bool synchronized;

    esp32_mquickjs_wifi_lock();
    deinit_sntp = s_wifi_time.sntp_initialized;
    synchronized = s_wifi_time.synchronized;
    memset(&s_wifi_time, 0, sizeof(s_wifi_time));
    s_wifi_time.synchronized = synchronized && wifi_time_is_valid();
    esp32_mquickjs_wifi_unlock();
    if (deinit_sntp) {
        esp_netif_sntp_deinit();
    }
}

static bool wifi_future_parse_time_sync(
    JSContext *ctx, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef servers_ref;
    JSGCRef timeout_ref;
    JSGCRef item_ref;
    JSValue *servers;
    JSValue *timeout;
    JSValue *item;
    int server_count = 0;
    bool valid = false;
    size_t i;

    if (argc != 1 || JS_GetClassID(ctx, argv[0].val) < 0 ||
        JS_IsArray(ctx, argv[0].val)) {
        JS_ThrowTypeError(
            ctx, "wifi.syncTime(options) expects an options object");
        return false;
    }
    servers = JS_PushGCRef(ctx, &servers_ref);
    timeout = JS_PushGCRef(ctx, &timeout_ref);
    item = JS_PushGCRef(ctx, &item_ref);
    *servers = JS_GetPropertyStr(ctx, argv[0].val, "servers");
    *timeout = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
    if (JS_IsException(*servers) || JS_IsException(*timeout) ||
        !JS_IsArray(ctx, *servers)) {
        JS_ThrowTypeError(
            ctx, "wifi.syncTime({ servers }) expects an array of server names");
        goto done;
    }
    *item = JS_GetPropertyStr(ctx, *servers, "length");
    if (JS_IsException(*item) || JS_ToInt32(ctx, &server_count, *item) != 0 ||
        server_count < 1 || server_count > CONFIG_LWIP_SNTP_MAX_SERVERS) {
        JS_ThrowRangeError(
            ctx, "wifi.syncTime() expects 1..%d time servers",
            CONFIG_LWIP_SNTP_MAX_SERVERS);
        goto done;
    }
    state->timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    if (!JS_IsUndefined(*timeout) &&
        (!JS_IsNumber(ctx, *timeout) ||
         esp32_mquickjs_wifi_value_to_timeout_ms(
             ctx, *timeout, ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS,
             &state->timeout_ms) != 0)) {
        JS_ThrowTypeError(
            ctx, "wifi.syncTime({ timeoutMs }) expects a non-negative integer");
        goto done;
    }
    for (i = 0; i < (size_t)server_count; ++i) {
        JSCStringBuf server_buf;
        const char *server;
        size_t server_len = 0;

        *item = JS_GetPropertyUint32(ctx, *servers, (uint32_t)i);
        if (JS_IsException(*item) || !JS_IsString(ctx, *item)) {
            JS_ThrowTypeError(
                ctx, "wifi.syncTime() server names must be strings");
            goto done;
        }
        server = JS_ToCStringLen(ctx, &server_len, *item, &server_buf);
        if (server == NULL || server_len == 0 ||
            server_len > WIFI_TIME_SERVER_MAX_BYTES ||
            memchr(server, '\0', server_len) != NULL) {
            JS_ThrowRangeError(
                ctx, "wifi.syncTime() received an invalid time server name");
            goto done;
        }
        memcpy(state->time_servers[i], server, server_len);
        state->time_servers[i][server_len] = '\0';
    }
    state->time_server_count = (size_t)server_count;
    valid = true;

done:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &timeout_ref);
    JS_PopGCRef(ctx, &servers_ref);
    return valid;
}

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

static bool wifi_time_future_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
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
    state->kind = WIFI_FUTURE_SYNC_TIME;
    state->time_waiter_index = -1;
    if (!wifi_future_parse_time_sync(ctx, argc, argv, state)) {
        heap_caps_free(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool wifi_time_future_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_wifi_status_t status;
    bool start_round = false;
    bool immediate = false;
    uint32_t generation;
    size_t i;

    if (esp32_mquickjs_wifi_get_status(&status) != ESP_OK ||
        !status.connected) {
        JS_ThrowInternalError(
            ctx, "wifi.syncTime() requires an active Wi-Fi connection");
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    state->started = true;

    esp32_mquickjs_wifi_lock();
    if ((s_wifi_time.synchronized || wifi_time_is_valid()) &&
        !s_wifi_time.in_progress) {
        s_wifi_time.synchronized = true;
        state->unix_time_ms = wifi_time_now_ms();
        state->time_error = ESP_OK;
        state->completed = true;
        immediate = true;
        generation = s_wifi_time.generation;
    } else {
        if (!s_wifi_time.in_progress) {
            s_wifi_time.generation++;
            if (s_wifi_time.generation == 0) {
                s_wifi_time.generation++;
            }
            s_wifi_time.in_progress = true;
            s_wifi_time.completed_error = ESP_OK;
            s_wifi_time.server_count = state->time_server_count;
            for (i = 0; i < state->time_server_count; ++i) {
                snprintf(s_wifi_time.servers[i],
                         sizeof(s_wifi_time.servers[i]), "%s",
                         state->time_servers[i]);
            }
            start_round = true;
        }
        generation = s_wifi_time.generation;
        state->generation = generation;
        state->time_waiter_index = wifi_time_add_waiter(
            runtime, token, generation);
        if (state->time_waiter_index < 0) {
            if (start_round) {
                s_wifi_time.in_progress = false;
            }
            esp32_mquickjs_wifi_unlock();
            JS_ThrowInternalError(
                ctx, "wifi.syncTime() has too many concurrent waiters");
            return false;
        }
    }
    esp32_mquickjs_wifi_unlock();

    if (immediate) {
        (void)esp32_mquickjs_future_wake(runtime, token);
        return true;
    }
    if (start_round) {
        esp_sntp_config_t config = {
            .smooth_sync = false,
            .server_from_dhcp = false,
            .wait_for_sync = false,
            .start = true,
            .sync_cb = wifi_time_sync_callback,
            .renew_servers_after_new_IP = false,
            .ip_event_to_renew = IP_EVENT_STA_GOT_IP,
            .index_of_first_server = 0,
            .num_of_servers = state->time_server_count,
        };
        esp_err_t err;

        for (i = 0; i < state->time_server_count; ++i) {
            config.servers[i] = s_wifi_time.servers[i];
        }
        err = esp_netif_sntp_init(&config);
        esp32_mquickjs_wifi_lock();
        if (err == ESP_OK) {
            s_wifi_time.sntp_initialized = true;
        } else if (s_wifi_time.generation == generation) {
            s_wifi_time.in_progress = false;
            s_wifi_time.completed_generation = generation;
            s_wifi_time.completed_error = err;
            wifi_time_wake_generation(generation);
        }
        esp32_mquickjs_wifi_unlock();
    }
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
        } else {
            JS_ThrowInternalError(ctx, "wifi.syncTime() failed to start Wi-Fi: %s",
                                  esp_err_to_name(err));
        }
        return false;
    }
    if (state->kind == WIFI_FUTURE_SYNC_TIME) {
        return wifi_time_future_start(ctx, runtime, token, state);
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
    if (state->kind == WIFI_FUTURE_SYNC_TIME) {
        esp32_mquickjs_wifi_lock();
        if (s_wifi_time.completed_generation == state->generation) {
            state->time_error = s_wifi_time.completed_error;
            state->unix_time_ms = s_wifi_time.unix_time_ms;
            state->completed = true;
        }
        esp32_mquickjs_wifi_unlock();
    } else if (state->kind == WIFI_FUTURE_SCAN) {
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
    if (state->kind == WIFI_FUTURE_SYNC_TIME) {
        JSGCRef result_ref;
        JSValue *result;

        if (state->time_error != ESP_OK) {
            return JS_ThrowInternalError(
                ctx, "wifi.syncTime() failed: %s",
                esp_err_to_name(state->time_error));
        }
        result = JS_PushGCRef(ctx, &result_ref);
        *result = JS_NewObject(ctx);
        if (JS_IsException(*result) ||
            !esp32_mquickjs_set_property_ref(
                ctx, result, "synchronized", JS_TRUE) ||
            !esp32_mquickjs_set_property_ref(
                ctx, result, "unixTimeMs",
                JS_NewInt64(ctx, state->unix_time_ms))) {
            JS_PopGCRef(ctx, &result_ref);
            return JS_EXCEPTION;
        }
        return JS_PopGCRef(ctx, &result_ref);
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
    if (state->kind == WIFI_FUTURE_SYNC_TIME) {
        wifi_time_remove_waiter(state);
    } else if (state->kind == WIFI_FUTURE_SCAN) {
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
    if (state->kind == WIFI_FUTURE_SYNC_TIME) {
        wifi_time_remove_waiter(state);
    }
    heap_caps_free(state);
}

static uint32_t wifi_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return 0;
    }
    if (state->kind == WIFI_FUTURE_SYNC_TIME && state->timeout_ms == 0) {
        return 1;
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

static const esp32_mquickjs_future_driver_t s_wifi_time_future_driver = {
    .prepare = wifi_time_future_prepare,
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
    JSGCRef sync_time_ref;
    JSValue *global;
    JSValue *wifi;
    JSValue *scan;
    JSValue *connect;
    JSValue *sync_time;
    bool result = false;

    global = JS_PushGCRef(ctx, &global_ref);
    wifi = JS_PushGCRef(ctx, &wifi_ref);
    scan = JS_PushGCRef(ctx, &scan_ref);
    connect = JS_PushGCRef(ctx, &connect_ref);
    sync_time = JS_PushGCRef(ctx, &sync_time_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_GetPropertyStr(ctx, *global, "wifi");
    *scan = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "scan");
    *connect = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "connect");
    *sync_time = JS_IsException(*wifi)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *wifi, "syncTime");
    if (!JS_IsException(*global) && !JS_IsException(*wifi) &&
        !JS_IsException(*scan) && !JS_IsException(*connect) &&
        !JS_IsException(*sync_time) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *scan,
                                              &s_wifi_scan_future_driver) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *connect,
                                              &s_wifi_connect_future_driver) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *sync_time,
                                              &s_wifi_time_future_driver)) {
        result = true;
    } else if (!JS_IsException(*scan) && !JS_IsException(*connect) &&
               !JS_IsException(*sync_time)) {
        JS_ThrowInternalError(ctx, "failed to register Wi-Fi Future drivers");
    }
    JS_PopGCRef(ctx, &sync_time_ref);
    JS_PopGCRef(ctx, &connect_ref);
    JS_PopGCRef(ctx, &scan_ref);
    JS_PopGCRef(ctx, &wifi_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

#endif
