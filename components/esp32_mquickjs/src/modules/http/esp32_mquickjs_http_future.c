#include "esp32_mquickjs_http.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#if defined(CONFIG_ESP32_MQUICKJS_HTTP_TASK_STACK_IN_PSRAM) && \
    CONFIG_ESP32_MQUICKJS_HTTP_TASK_STACK_IN_PSRAM
#include "freertos/idf_additions.h"
#endif
#include "freertos/semphr.h"
#include "freertos/task.h"

#define ESP32_MQUICKJS_HTTP_MAX_FUTURES 4U

struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_http_request_t request;
    esp32_mquickjs_http_response_t *response;
    esp32_mquickjs_http_operation_t *operation;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp_err_t err;
    esp32_mquickjs_tls_error_t tls_error;
    char error_text[ESP32_MQUICKJS_HTTP_ERROR_TEXT_LEN];
    _Atomic bool completed;
    bool started;
    bool cancel_requested;
    bool worker_uses_caps;
    bool cleanup_queued;
    struct esp32_mquickjs_future_driver_state *next_cleanup;
};

typedef struct {
    bool initialized;
    SemaphoreHandle_t lock;
    uint32_t active_count;
    esp32_mquickjs_future_driver_state_t *cleanup_pending;
} esp32_mquickjs_http_future_runtime_t;

static esp32_mquickjs_http_future_runtime_t s_http_future_runtime;

static JSValue http_throw_operation_error(JSContext *ctx,
                                          const char *code,
                                          esp_err_t err)
{
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    JSValue result;

    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "espCode", JS_NewInt32(ctx, err)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "espName",
            JS_NewString(ctx, esp_err_to_name(err)))) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    result = esp32_mquickjs_throw_native_error(
        ctx, code, "http.fetch", "HTTP request failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

static void http_future_lock(void)
{
    if (s_http_future_runtime.lock != NULL) {
        xSemaphoreTake(s_http_future_runtime.lock, portMAX_DELAY);
    }
}

static void http_future_unlock(void)
{
    if (s_http_future_runtime.lock != NULL) {
        xSemaphoreGive(s_http_future_runtime.lock);
    }
}

static void http_future_retry_pending_cleanup(void)
{
    esp32_mquickjs_future_driver_state_t **link;

    if (!s_http_future_runtime.initialized) {
        return;
    }
    http_future_lock();
    link = &s_http_future_runtime.cleanup_pending;
    while (*link != NULL) {
        esp32_mquickjs_future_driver_state_t *state = *link;

        if (!esp32_mquickjs_http_operation_destroy(state->operation)) {
            link = &state->next_cleanup;
            continue;
        }
        state->operation = NULL;
        *link = state->next_cleanup;
        state->next_cleanup = NULL;
        state->cleanup_queued = false;
        if (state->started && s_http_future_runtime.active_count > 0) {
            s_http_future_runtime.active_count--;
        }
        heap_caps_free(state);
    }
    http_future_unlock();
}

static void http_future_track_pending_cleanup(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->cleanup_queued) {
        return;
    }
    http_future_lock();
    if (!state->cleanup_queued) {
        state->next_cleanup = s_http_future_runtime.cleanup_pending;
        s_http_future_runtime.cleanup_pending = state;
        state->cleanup_queued = true;
    }
    http_future_unlock();
}

static bool http_future_init_state(void)
{
    if (s_http_future_runtime.initialized) {
        http_future_retry_pending_cleanup();
        return true;
    }
    memset(&s_http_future_runtime, 0, sizeof(s_http_future_runtime));
    s_http_future_runtime.lock = xSemaphoreCreateMutex();
    if (s_http_future_runtime.lock == NULL) {
        return false;
    }
    s_http_future_runtime.initialized = true;
    return true;
}

static bool http_future_reserve_worker(void)
{
    bool reserved = false;

    http_future_retry_pending_cleanup();
    http_future_lock();
    if (s_http_future_runtime.active_count < ESP32_MQUICKJS_HTTP_MAX_FUTURES) {
        s_http_future_runtime.active_count++;
        reserved = true;
    }
    http_future_unlock();
    return reserved;
}

static void http_future_release_worker(void)
{
    http_future_lock();
    if (s_http_future_runtime.active_count > 0) {
        s_http_future_runtime.active_count--;
    }
    http_future_unlock();
}

static void http_future_delete_worker(bool worker_uses_caps)
{
#if defined(CONFIG_ESP32_MQUICKJS_HTTP_TASK_STACK_IN_PSRAM) && \
    CONFIG_ESP32_MQUICKJS_HTTP_TASK_STACK_IN_PSRAM
    if (worker_uses_caps) {
        vTaskDeleteWithCaps(NULL);
        return;
    }
#else
    (void)worker_uses_caps;
#endif
    vTaskDelete(NULL);
}

static void http_future_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    bool worker_uses_caps;

    if (state == NULL) {
        vTaskDelete(NULL);
        return;
    }
    worker_uses_caps = state->worker_uses_caps;
    state->response = esp32_mquickjs_http_perform_request(&state->request,
                                                          state->operation,
                                                          &state->err,
                                                          &state->tls_error,
                                                          state->error_text,
                                                          sizeof(state->error_text));
    if (state->err == ESP_OK && state->response == NULL) {
        state->err = ESP_FAIL;
        snprintf(state->error_text,
                 sizeof(state->error_text),
                 "fetch worker returned no response");
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    http_future_delete_worker(worker_uses_caps);
}

static BaseType_t http_future_create_worker(
    esp32_mquickjs_future_driver_state_t *state)
{
#if defined(CONFIG_ESP32_MQUICKJS_HTTP_TASK_STACK_IN_PSRAM) && \
    CONFIG_ESP32_MQUICKJS_HTTP_TASK_STACK_IN_PSRAM
    state->worker_uses_caps = true;
    if (xTaskCreateWithCaps(http_future_worker,
                            "http_future",
                            ESP32_MQUICKJS_HTTP_TASK_STACK_SIZE,
                            state,
                            tskIDLE_PRIORITY + 4,
                            NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        return pdPASS;
    }
#endif
    state->worker_uses_caps = false;
    return xTaskCreate(http_future_worker,
                       "http_future",
                       ESP32_MQUICKJS_HTTP_TASK_STACK_SIZE,
                       state,
                       tskIDLE_PRIORITY + 4,
                       NULL);
}

static bool http_future_prepare(JSContext *ctx,
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
    *out_state = NULL;
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    atomic_init(&state->completed, false);
    if (esp32_mquickjs_http_build_request_from_args(ctx,
                                                    argc,
                                                    argv,
                                                    &state->request) != 0) {
        esp32_mquickjs_http_free_request(&state->request);
        heap_caps_free(state);
        return false;
    }
    state->operation = esp32_mquickjs_http_operation_create();
    if (state->operation == NULL) {
        esp32_mquickjs_http_free_request(&state->request);
        heap_caps_free(state);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    *out_state = state;
    return true;
}

static bool http_future_start(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              esp32_mquickjs_future_token_t token,
                              esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || !http_future_reserve_worker()) {
        http_throw_operation_error(ctx, "HTTP_QUEUE_FULL", ESP_ERR_NO_MEM);
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (http_future_create_worker(state) != pdPASS) {
        state->started = false;
        http_future_release_worker();
        http_throw_operation_error(
            ctx, "HTTP_WORKER_START_FAILED", ESP_ERR_NO_MEM);
        return false;
    }
    return true;
}

static esp32_mquickjs_future_poll_t http_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL &&
        atomic_load_explicit(&state->completed, memory_order_acquire)) {
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    return ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue http_future_finish(JSContext *ctx,
                                  esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return http_throw_operation_error(
            ctx, "HTTP_INTERNAL", ESP_ERR_INVALID_STATE);
    }
    if (state->cancel_requested ||
        esp32_mquickjs_http_operation_is_cancelled(state->operation)) {
        return http_throw_operation_error(
            ctx, "HTTP_CANCELLED", ESP_ERR_INVALID_STATE);
    }
    if (state->err != ESP_OK || state->response == NULL) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
        if (state->tls_error.present) {
            return esp32_mquickjs_throw_tls_error(
                ctx, "fetch()", &state->tls_error);
        }
#endif
        return http_throw_operation_error(
            ctx,
            state->err == ESP_ERR_TIMEOUT ? "HTTP_TIMEOUT"
                                          : "HTTP_REQUEST_FAILED",
            state->err);
    }
    return esp32_mquickjs_http_make_response_object(ctx, state->response);
}

static esp32_mquickjs_cancel_result_t http_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL ||
        atomic_load_explicit(&state->completed, memory_order_acquire) ||
        state->cancel_requested) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    if (!esp32_mquickjs_http_operation_cancel(state->operation)) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancel_requested = true;
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}

static void http_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    esp32_mquickjs_http_free_response(state->response);
    state->response = NULL;
    esp32_mquickjs_http_free_request(&state->request);
    if (!esp32_mquickjs_http_operation_destroy(state->operation)) {
        http_future_track_pending_cleanup(state);
        return;
    }
    state->operation = NULL;
    if (state->started) {
        http_future_release_worker();
    }
    heap_caps_free(state);
}

static uint32_t http_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    /* esp_http_client owns the request deadline so it can publish TLS detail
     * before the Future settles. Cancellation still interrupts the worker. */
    (void)state;
    return 0;
}

static const esp32_mquickjs_future_driver_t s_http_future_driver = {
    .capture = http_future_prepare,
    .start = http_future_start,
    .poll = http_future_poll,
    .finish = http_future_finish,
    .cancel = http_future_cancel,
    .destroy = http_future_destroy,
    .timeout_ms = http_future_timeout_ms,
};

bool esp32_mquickjs_init_http_future_runtime(JSContext *ctx,
                                             esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref;
    JSGCRef http_ref;
    JSGCRef fetch_ref;
    JSGCRef module_fetch_ref;
    JSValue *global;
    JSValue *http;
    JSValue *fetch;
    JSValue *module_fetch;
    bool result = false;

    if (!http_future_init_state()) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    global = JS_PushGCRef(ctx, &global_ref);
    http = JS_PushGCRef(ctx, &http_ref);
    fetch = JS_PushGCRef(ctx, &fetch_ref);
    module_fetch = JS_PushGCRef(ctx, &module_fetch_ref);
    *global = JS_GetGlobalObject(ctx);
    *http = JS_GetPropertyStr(ctx, *global, "http");
    *fetch = JS_GetPropertyStr(ctx, *global, "fetch");
    *module_fetch = JS_IsException(*http)
                        ? JS_EXCEPTION
                        : JS_GetPropertyStr(ctx, *http, "fetch");
    if (!JS_IsException(*global) && !JS_IsException(*http) &&
        !JS_IsException(*fetch) && !JS_IsException(*module_fetch) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *fetch,
                                              &s_http_future_driver) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *module_fetch,
                                              &s_http_future_driver)) {
        result = true;
    } else if (!JS_IsException(*fetch) && !JS_IsException(*module_fetch)) {
        JS_ThrowInternalError(ctx, "failed to register HTTP Future drivers");
    }
    JS_PopGCRef(ctx, &module_fetch_ref);
    JS_PopGCRef(ctx, &fetch_ref);
    JS_PopGCRef(ctx, &http_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

bool esp32_mquickjs_deinit_http_runtime(JSContext *ctx)
{
    bool active;

    (void)ctx;
    if (!s_http_future_runtime.initialized) {
        return true;
    }
    http_future_retry_pending_cleanup();
    http_future_lock();
    active = s_http_future_runtime.active_count > 0;
    http_future_unlock();
    if (active) {
        return false;
    }
    vSemaphoreDelete(s_http_future_runtime.lock);
    memset(&s_http_future_runtime, 0, sizeof(s_http_future_runtime));
    return true;
}

#endif
