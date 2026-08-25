#include "esp32_mquickjs_time.h"

#include "esp32_mquickjs_core.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#define ESP32_MQUICKJS_TIME_VALID_AFTER_UNIX 1577836800LL

static int64_t time_now_ms(void)
{
    struct timeval now = {0};

    gettimeofday(&now, NULL);
    return (int64_t)now.tv_sec * 1000LL + (int64_t)now.tv_usec / 1000LL;
}

static bool time_is_valid(void)
{
    return time(NULL) >= (time_t)ESP32_MQUICKJS_TIME_VALID_AFTER_UNIX;
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include "esp32_mquickjs_future.h"

#include <stdint.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

#define TIME_SERVER_MAX_BYTES 253U
#define TIME_DEFAULT_TIMEOUT_MS 15000U
#define TIME_MAX_WAITERS \
    (CONFIG_ESP32_MQUICKJS_MAX_FUTURES + \
     CONFIG_ESP32_MQUICKJS_INTERNAL_FUTURE_RESERVE)

typedef struct {
    bool active;
    uint32_t generation;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
} time_waiter_t;

typedef struct {
    bool in_progress;
    bool sntp_initialized;
    bool synchronized;
    uint32_t generation;
    uint32_t completed_generation;
    esp_err_t completed_error;
    int64_t unix_time_ms;
    size_t waiter_count;
    time_waiter_t waiters[TIME_MAX_WAITERS];
    char servers[CONFIG_LWIP_SNTP_MAX_SERVERS][TIME_SERVER_MAX_BYTES + 1U];
    size_t server_count;
} time_sync_state_t;

struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint32_t generation;
    uint32_t timeout_ms;
    size_t server_count;
    int waiter_index;
    int64_t unix_time_ms;
    esp_err_t error;
    char servers[CONFIG_LWIP_SNTP_MAX_SERVERS][TIME_SERVER_MAX_BYTES + 1U];
    bool started;
    bool completed;
    bool cancel_requested;
};

static time_sync_state_t s_time;
static portMUX_TYPE s_time_lock = portMUX_INITIALIZER_UNLOCKED;

static void time_lock(void)
{
    portENTER_CRITICAL(&s_time_lock);
}

static void time_unlock(void)
{
    portEXIT_CRITICAL(&s_time_lock);
}

static void time_wake_generation(uint32_t generation)
{
    esp32_mquickjs_runtime_t *runtimes[TIME_MAX_WAITERS];
    esp32_mquickjs_future_token_t tokens[TIME_MAX_WAITERS];
    size_t count = 0;
    size_t i;

    time_lock();
    for (i = 0; i < TIME_MAX_WAITERS; ++i) {
        time_waiter_t *waiter = &s_time.waiters[i];

        if (waiter->active && waiter->generation == generation &&
            waiter->runtime != NULL) {
            runtimes[count] = waiter->runtime;
            tokens[count] = waiter->token;
            count++;
        }
    }
    time_unlock();

    for (i = 0; i < count; ++i) {
        (void)esp32_mquickjs_future_wake(runtimes[i], tokens[i]);
    }
}

static void time_sync_callback(struct timeval *tv)
{
    uint32_t generation;

    time_lock();
    if (!s_time.in_progress) {
        time_unlock();
        return;
    }
    generation = s_time.generation;
    s_time.in_progress = false;
    s_time.synchronized = true;
    s_time.completed_generation = generation;
    s_time.completed_error = ESP_OK;
    s_time.unix_time_ms = tv != NULL
        ? (int64_t)tv->tv_sec * 1000LL + (int64_t)tv->tv_usec / 1000LL
        : time_now_ms();
    time_unlock();

    time_wake_generation(generation);
}

static int time_add_waiter_locked(esp32_mquickjs_runtime_t *runtime,
                                  esp32_mquickjs_future_token_t token,
                                  uint32_t generation)
{
    size_t i;

    for (i = 0; i < TIME_MAX_WAITERS; ++i) {
        time_waiter_t *waiter = &s_time.waiters[i];

        if (waiter->active) {
            continue;
        }
        waiter->active = true;
        waiter->generation = generation;
        waiter->runtime = runtime;
        waiter->token = token;
        s_time.waiter_count++;
        return (int)i;
    }
    return -1;
}

static void time_remove_waiter(
    esp32_mquickjs_future_driver_state_t *state)
{
    bool deinit_sntp = false;

    if (state == NULL || state->waiter_index < 0 ||
        state->waiter_index >= TIME_MAX_WAITERS) {
        return;
    }
    time_lock();
    if (s_time.waiters[state->waiter_index].active) {
        memset(&s_time.waiters[state->waiter_index], 0,
               sizeof(s_time.waiters[state->waiter_index]));
        if (s_time.waiter_count > 0) {
            s_time.waiter_count--;
        }
    }
    state->waiter_index = -1;
    if (s_time.waiter_count == 0 && s_time.sntp_initialized) {
        s_time.in_progress = false;
        s_time.sntp_initialized = false;
        deinit_sntp = true;
    }
    time_unlock();
    if (deinit_sntp) {
        esp_netif_sntp_deinit();
    }
}

static bool time_parse_sync_options(
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
    int timeout_ms = 0;
    bool valid = false;
    size_t i;

    if (argc != 1 || JS_GetClassID(ctx, argv[0].val) < 0 ||
        JS_IsArray(ctx, argv[0].val)) {
        JS_ThrowTypeError(
            ctx, "sys.time.sync(options) expects an options object");
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
            ctx,
            "sys.time.sync({ servers }) expects an array of server names");
        goto done;
    }
    *item = JS_GetPropertyStr(ctx, *servers, "length");
    if (JS_IsException(*item) || JS_ToInt32(ctx, &server_count, *item) != 0 ||
        server_count < 1 || server_count > CONFIG_LWIP_SNTP_MAX_SERVERS) {
        JS_ThrowRangeError(
            ctx, "sys.time.sync() expects 1..%d time servers",
            CONFIG_LWIP_SNTP_MAX_SERVERS);
        goto done;
    }
    state->timeout_ms = TIME_DEFAULT_TIMEOUT_MS;
    if (!JS_IsUndefined(*timeout)) {
        if (!JS_IsNumber(ctx, *timeout) ||
            JS_ToInt32(ctx, &timeout_ms, *timeout) != 0 || timeout_ms < 0) {
            JS_ThrowTypeError(
                ctx,
                "sys.time.sync({ timeoutMs }) expects a non-negative integer");
            goto done;
        }
        state->timeout_ms = (uint32_t)timeout_ms;
    }
    for (i = 0; i < (size_t)server_count; ++i) {
        JSCStringBuf server_buf;
        const char *server;
        size_t server_len = 0;

        *item = JS_GetPropertyUint32(ctx, *servers, (uint32_t)i);
        if (JS_IsException(*item) || !JS_IsString(ctx, *item)) {
            JS_ThrowTypeError(
                ctx, "sys.time.sync() server names must be strings");
            goto done;
        }
        server = JS_ToCStringLen(ctx, &server_len, *item, &server_buf);
        if (server == NULL || server_len == 0 ||
            server_len > TIME_SERVER_MAX_BYTES ||
            memchr(server, '\0', server_len) != NULL) {
            JS_ThrowRangeError(
                ctx,
                "sys.time.sync() received an invalid time server name");
            goto done;
        }
        memcpy(state->servers[i], server, server_len);
        state->servers[i][server_len] = '\0';
    }
    state->server_count = (size_t)server_count;
    valid = true;

done:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &timeout_ref);
    JS_PopGCRef(ctx, &servers_ref);
    return valid;
}

static bool time_future_prepare(
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
    state->waiter_index = -1;
    if (!time_parse_sync_options(ctx, argc, argv, state)) {
        heap_caps_free(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool time_netif_has_address(esp_netif_t *netif, void *ctx)
{
    esp_netif_ip_info_t ip_info = {0};

    (void)ctx;
    return esp_netif_is_netif_up(netif) &&
           esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
           ip_info.ip.addr != 0;
}

static bool time_future_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    bool start_round = false;
    bool immediate = false;
    uint32_t generation = 0;
    size_t i;

    if (state == NULL) {
        JS_ThrowInternalError(ctx, "time Future lost its driver state");
        return false;
    }
    if (esp_netif_find_if(time_netif_has_address, NULL) == NULL) {
        JS_ThrowInternalError(
            ctx,
            "sys.time.sync() requires an active network connection");
        return false;
    }

    state->runtime = runtime;
    state->token = token;
    state->started = true;

    time_lock();
    if ((s_time.synchronized || time_is_valid()) && !s_time.in_progress) {
        s_time.synchronized = true;
        state->unix_time_ms = time_now_ms();
        state->error = ESP_OK;
        state->completed = true;
        immediate = true;
    } else {
        if (!s_time.in_progress) {
            s_time.generation++;
            if (s_time.generation == 0) {
                s_time.generation++;
            }
            s_time.in_progress = true;
            s_time.completed_error = ESP_OK;
            s_time.server_count = state->server_count;
            for (i = 0; i < state->server_count; ++i) {
                snprintf(s_time.servers[i], sizeof(s_time.servers[i]), "%s",
                         state->servers[i]);
            }
            start_round = true;
        }
        generation = s_time.generation;
        state->generation = generation;
        state->waiter_index = time_add_waiter_locked(runtime, token,
                                                     generation);
        if (state->waiter_index < 0) {
            if (start_round) {
                s_time.in_progress = false;
            }
            time_unlock();
            JS_ThrowInternalError(
                ctx, "sys.time.sync() has too many concurrent waiters");
            return false;
        }
    }
    time_unlock();

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
            .sync_cb = time_sync_callback,
            .renew_servers_after_new_IP = false,
            .ip_event_to_renew = IP_EVENT_STA_GOT_IP,
            .index_of_first_server = 0,
            .num_of_servers = state->server_count,
        };
        esp_err_t err;

        for (i = 0; i < state->server_count; ++i) {
            config.servers[i] = s_time.servers[i];
        }
        err = esp_netif_sntp_init(&config);
        time_lock();
        if (err == ESP_OK) {
            s_time.sntp_initialized = true;
        } else if (s_time.generation == generation) {
            s_time.in_progress = false;
            s_time.completed_generation = generation;
            s_time.completed_error = err;
        }
        time_unlock();
        if (err != ESP_OK) {
            time_wake_generation(generation);
        }
    }
    return true;
}

static esp32_mquickjs_future_poll_t time_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed) {
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    time_lock();
    if (s_time.completed_generation == state->generation) {
        state->error = s_time.completed_error;
        state->unix_time_ms = s_time.unix_time_ms;
        state->completed = true;
    }
    time_unlock();
    return state->completed
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue time_future_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef result_ref;
    JSValue *result;

    if (state == NULL || state->cancel_requested) {
        return JS_ThrowInternalError(ctx, "time synchronization cancelled");
    }
    if (state->error != ESP_OK) {
        return JS_ThrowInternalError(
            ctx, "sys.time.sync() failed: %s",
            esp_err_to_name(state->error));
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

static bool time_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->cancel_requested) {
        return false;
    }
    state->cancel_requested = true;
    time_remove_waiter(state);
    state->completed = true;
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    return true;
}

static void time_future_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->started && !state->completed) {
        (void)time_future_cancel(state);
    }
    time_remove_waiter(state);
    heap_caps_free(state);
}

static uint32_t time_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return 0;
    }
    return state->timeout_ms == 0 ? 1 : state->timeout_ms;
}

static const esp32_mquickjs_future_driver_t s_time_future_driver = {
    .prepare = time_future_prepare,
    .start = time_future_start,
    .poll = time_future_poll,
    .finish = time_future_finish,
    .cancel = time_future_cancel,
    .destroy = time_future_destroy,
    .timeout_ms = time_future_timeout_ms,
};

JSValue js_sys_time_sync(JSContext *ctx, JSValue *this_val, int argc,
                         JSValue *argv)
{
    JSGCRef global_ref;
    JSGCRef sys_ref;
    JSGCRef time_ref;
    JSGCRef sync_ref;
    JSValue *global;
    JSValue *sys;
    JSValue *time;
    JSValue *sync;
    JSValue result;

    (void)this_val;
    global = JS_PushGCRef(ctx, &global_ref);
    sys = JS_PushGCRef(ctx, &sys_ref);
    time = JS_PushGCRef(ctx, &time_ref);
    sync = JS_PushGCRef(ctx, &sync_ref);
    *global = JS_GetGlobalObject(ctx);
    *sys = JS_GetPropertyStr(ctx, *global, "sys");
    *time = JS_IsException(*sys)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *sys, "time");
    *sync = JS_IsException(*time)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *time, "sync");
    if (JS_IsException(*global) || JS_IsException(*sys) ||
        JS_IsException(*time) || JS_IsException(*sync)) {
        result = JS_EXCEPTION;
    } else {
        result = esp32_mquickjs_future_call_and_wait(
            ctx, esp32_mquickjs_get_active_runtime(), *sync, *time, argc,
            argv);
    }
    JS_PopGCRef(ctx, &sync_ref);
    JS_PopGCRef(ctx, &time_ref);
    JS_PopGCRef(ctx, &sys_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

#endif

JSValue js_sys_time_status(JSContext *ctx, JSValue *this_val, int argc,
                           JSValue *argv)
{
    JSGCRef result_ref;
    JSValue *result;
    bool valid = time_is_valid();
    bool synchronized = valid;
    bool synchronizing = false;

    (void)this_val;
    (void)argv;
    if (argc != 0) {
        return JS_ThrowTypeError(ctx, "sys.time.status() expects no arguments");
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    time_lock();
    if (valid) {
        s_time.synchronized = true;
    }
    synchronized = s_time.synchronized && valid;
    synchronizing = s_time.in_progress;
    time_unlock();
#endif

    result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "synchronized", JS_NewBool(synchronized)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "synchronizing", JS_NewBool(synchronizing)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "unixTimeMs",
            valid ? JS_NewInt64(ctx, time_now_ms()) : JS_NULL)) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

bool esp32_mquickjs_init_time_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime)
{
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    JSGCRef global_ref;
    JSGCRef sys_ref;
    JSGCRef time_ref;
    JSGCRef sync_ref;
    JSValue *global;
    JSValue *sys;
    JSValue *time;
    JSValue *sync;
    bool result = false;

    global = JS_PushGCRef(ctx, &global_ref);
    sys = JS_PushGCRef(ctx, &sys_ref);
    time = JS_PushGCRef(ctx, &time_ref);
    sync = JS_PushGCRef(ctx, &sync_ref);
    *global = JS_GetGlobalObject(ctx);
    *sys = JS_GetPropertyStr(ctx, *global, "sys");
    *time = JS_IsException(*sys)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *sys, "time");
    *sync = JS_IsException(*time)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *time, "sync");
    if (!JS_IsException(*global) && !JS_IsException(*sys) &&
        !JS_IsException(*time) && !JS_IsException(*sync) &&
        esp32_mquickjs_future_register_driver(
            ctx, runtime, *sync, &s_time_future_driver)) {
        result = true;
    } else if (!JS_IsException(*sync)) {
        JS_ThrowInternalError(ctx, "failed to register time Future driver");
    }
    JS_PopGCRef(ctx, &sync_ref);
    JS_PopGCRef(ctx, &time_ref);
    JS_PopGCRef(ctx, &sys_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
#else
    (void)ctx;
    (void)runtime;
    return true;
#endif
}

void esp32_mquickjs_deinit_time_runtime(void)
{
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    bool deinit_sntp;
    bool synchronized;

    time_lock();
    deinit_sntp = s_time.sntp_initialized;
    synchronized = s_time.synchronized;
    memset(&s_time, 0, sizeof(s_time));
    s_time.synchronized = synchronized && time_is_valid();
    time_unlock();
    if (deinit_sntp) {
        esp_netif_sntp_deinit();
    }
#endif
}
