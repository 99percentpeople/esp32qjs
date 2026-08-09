#include "esp32_mquickjs_http.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP

#include "esp32_mquickjs_core.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS 4
#define ESP32_MQUICKJS_HTTP_ASYNC_QUEUE_LEN 4
#define ESP32_MQUICKJS_HTTP_HANDLE_ID_BITS 2U
#define ESP32_MQUICKJS_HTTP_HANDLE_ID_MASK ((1U << ESP32_MQUICKJS_HTTP_HANDLE_ID_BITS) - 1U)
#define ESP32_MQUICKJS_HTTP_HANDLE_MAX \
    (((uint64_t)UINT32_MAX << ESP32_MQUICKJS_HTTP_HANDLE_ID_BITS) | \
     ESP32_MQUICKJS_HTTP_HANDLE_ID_MASK)

typedef struct esp32_mquickjs_http_async_slot esp32_mquickjs_http_async_slot_t;

typedef struct {
    uint8_t slot_id;
    uint32_t generation;
    esp_err_t err;
    esp32_mquickjs_http_response_t *response;
    char error_text[ESP32_MQUICKJS_HTTP_ERROR_TEXT_LEN];
} esp32_mquickjs_http_async_event_t;

struct esp32_mquickjs_http_async_slot {
    uint8_t slot_id;
    bool allocated;
    uint32_t generation;
    JSGCRef callback;
    esp32_mquickjs_http_request_t request;
    esp32_mquickjs_http_operation_t *operation;
};

typedef struct {
    bool initialized;
    QueueHandle_t queue;
    SemaphoreHandle_t lock;
    esp32_mquickjs_http_async_slot_t slots[ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS];
} esp32_mquickjs_http_state_t;

typedef struct {
    esp32_mquickjs_http_async_slot_t *slot;
    uint32_t generation;
} esp32_mquickjs_http_worker_args_t;

static esp32_mquickjs_http_state_t s_http_state;

static bool http_async_poller(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              void *opaque);

static void http_lock(void)
{
    if (s_http_state.lock != NULL) {
        xSemaphoreTake(s_http_state.lock, portMAX_DELAY);
    }
}

static void http_unlock(void)
{
    if (s_http_state.lock != NULL) {
        xSemaphoreGive(s_http_state.lock);
    }
}

static uint64_t http_async_handle(const esp32_mquickjs_http_async_slot_t *slot)
{
    return ((uint64_t)slot->generation << ESP32_MQUICKJS_HTTP_HANDLE_ID_BITS) |
           (uint64_t)slot->slot_id;
}

static bool http_decode_async_handle(JSContext *ctx,
                                     JSValue value,
                                     uint8_t *out_slot_id,
                                     uint32_t *out_generation)
{
    double raw_handle;
    uint64_t handle;

    if (JS_ToNumber(ctx, &raw_handle, value) != 0 ||
        !(raw_handle > 0) ||
        raw_handle > (double)ESP32_MQUICKJS_HTTP_HANDLE_MAX) {
        return false;
    }
    handle = (uint64_t)raw_handle;
    if ((double)handle != raw_handle) {
        return false;
    }
    *out_slot_id = (uint8_t)(handle & ESP32_MQUICKJS_HTTP_HANDLE_ID_MASK);
    *out_generation = (uint32_t)(handle >> ESP32_MQUICKJS_HTTP_HANDLE_ID_BITS);
    return *out_generation != 0 &&
           *out_slot_id < ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS &&
           handle == (((uint64_t)*out_generation << ESP32_MQUICKJS_HTTP_HANDLE_ID_BITS) |
                      (uint64_t)*out_slot_id);
}

static bool http_init_state(void)
{
    int i;

    if (s_http_state.initialized) {
        return true;
    }

    memset(&s_http_state, 0, sizeof(s_http_state));
    s_http_state.queue = xQueueCreate(ESP32_MQUICKJS_HTTP_ASYNC_QUEUE_LEN,
                                      sizeof(esp32_mquickjs_http_async_event_t));
    s_http_state.lock = xSemaphoreCreateMutex();
    if (s_http_state.queue == NULL || s_http_state.lock == NULL) {
        if (s_http_state.queue != NULL) {
            vQueueDelete(s_http_state.queue);
        }
        if (s_http_state.lock != NULL) {
            vSemaphoreDelete(s_http_state.lock);
        }
        memset(&s_http_state, 0, sizeof(s_http_state));
        return false;
    }

    for (i = 0; i < ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS; ++i) {
        s_http_state.slots[i].slot_id = (uint8_t)i;
    }
    s_http_state.initialized = true;
    return true;
}

static void http_async_cleanup_slot(JSContext *ctx, esp32_mquickjs_http_async_slot_t *slot)
{
    if (slot == NULL || !slot->allocated) {
        return;
    }

    JS_DeleteGCRef(ctx, &slot->callback);
    esp32_mquickjs_http_free_request(&slot->request);
    esp32_mquickjs_http_operation_destroy(slot->operation);
    slot->operation = NULL;
    slot->allocated = false;
}

static void http_worker_task(void *opaque)
{
    esp32_mquickjs_http_worker_args_t *args = opaque;
    esp32_mquickjs_http_async_slot_t *slot;
    esp32_mquickjs_http_async_event_t event = {0};

    if (args == NULL) {
        vTaskDelete(NULL);
        return;
    }

    slot = args->slot;
    event.slot_id = slot->slot_id;
    event.generation = args->generation;
    event.response = esp32_mquickjs_http_perform_request(&slot->request,
                                                         slot->operation,
                                                         &event.err,
                                                         event.error_text,
                                                         sizeof(event.error_text));
    if (event.err == ESP_OK && event.response == NULL) {
        event.err = ESP_FAIL;
        snprintf(event.error_text, sizeof(event.error_text), "fetch worker returned no response");
    }

    heap_caps_free(args);
    xQueueSend(s_http_state.queue, &event, portMAX_DELAY);
    esp32_mquickjs_notify_activity(esp32_mquickjs_get_active_runtime());
    vTaskDelete(NULL);
}

static JSValue http_fetch_async(JSContext *ctx,
                                const esp32_mquickjs_http_request_t *request,
                                JSValue callback)
{
    esp32_mquickjs_http_async_slot_t *slot = NULL;
    esp32_mquickjs_http_worker_args_t *worker_args = NULL;
    JSValue *callback_ref;
    int i;

    if (!http_init_state()) {
        return JS_ThrowOutOfMemory(ctx);
    }
    if (!JS_IsFunction(ctx, callback)) {
        return JS_ThrowTypeError(ctx, "http.async.fetch(..., callback) expects a function");
    }

    http_lock();
    for (i = 0; i < ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS; ++i) {
        if (!s_http_state.slots[i].allocated) {
            slot = &s_http_state.slots[i];
            slot->allocated = true;
            slot->generation++;
            if (slot->generation == 0) {
                slot->generation++;
            }
            break;
        }
    }
    http_unlock();

    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "too many asynchronous fetch requests");
    }

    callback_ref = JS_AddGCRef(ctx, &slot->callback);
    *callback_ref = callback;
    if (esp32_mquickjs_http_clone_request(request, &slot->request) != 0) {
        http_async_cleanup_slot(ctx, slot);
        return JS_ThrowOutOfMemory(ctx);
    }
    slot->operation = esp32_mquickjs_http_operation_create();
    if (slot->operation == NULL) {
        http_async_cleanup_slot(ctx, slot);
        return JS_ThrowOutOfMemory(ctx);
    }
    worker_args = heap_caps_calloc(1, sizeof(*worker_args), MALLOC_CAP_8BIT);
    if (worker_args == NULL) {
        http_async_cleanup_slot(ctx, slot);
        return JS_ThrowOutOfMemory(ctx);
    }

    worker_args->slot = slot;
    worker_args->generation = slot->generation;
    if (xTaskCreate(http_worker_task,
                    "http_fetch",
                    ESP32_MQUICKJS_HTTP_TASK_STACK_SIZE,
                    worker_args,
                    tskIDLE_PRIORITY + 4,
                    NULL) != pdPASS) {
        heap_caps_free(worker_args);
        http_async_cleanup_slot(ctx, slot);
        return JS_ThrowInternalError(ctx, "failed to start fetch worker task");
    }

    return JS_NewInt64(ctx, (int64_t)http_async_handle(slot));
}

bool esp32_mquickjs_init_http_async_runtime(JSContext *ctx,
                                            esp32_mquickjs_runtime_t *runtime)
{
    if (!esp32_mquickjs_register_async_poller(runtime, http_async_poller, NULL)) {
        JS_ThrowInternalError(ctx, "failed to register http async poller");
        return false;
    }
    return true;
}

JSValue js_http_async_fetch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_http_request_t request = {0};
    JSValue result;

    (void)this_val;

    if (argc < 2 || argc > 3) {
        return JS_ThrowTypeError(ctx,
                                 "http.async.fetch(input, callback) or http.async.fetch(input, options, callback) expects a URL string or Request");
    }

    if (!JS_IsFunction(ctx, argv[argc - 1])) {
        return JS_ThrowTypeError(ctx, "http.async.fetch(..., callback) expects a callback function");
    }

    if (esp32_mquickjs_http_build_request_from_args(ctx, argc - 1, argv, &request) != 0) {
        esp32_mquickjs_http_free_request(&request);
        return JS_EXCEPTION;
    }

    result = http_fetch_async(ctx, &request, argv[argc - 1]);
    esp32_mquickjs_http_free_request(&request);
    return result;
}

JSValue js_http_async_cancel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_http_operation_t *operation = NULL;
    uint8_t slot_id;
    uint32_t generation;

    (void)this_val;
    if (argc < 1 || !http_decode_async_handle(ctx, argv[0], &slot_id, &generation)) {
        return JS_ThrowTypeError(ctx, "http.async.cancel(handle) expects a request handle");
    }

    http_lock();
    if (s_http_state.initialized &&
        s_http_state.slots[slot_id].allocated &&
        s_http_state.slots[slot_id].generation == generation) {
        operation = s_http_state.slots[slot_id].operation;
    }
    http_unlock();
    return JS_NewBool(esp32_mquickjs_http_operation_cancel(operation));
}

bool esp32_mquickjs_deinit_http_runtime(JSContext *ctx)
{
    esp32_mquickjs_http_async_event_t event;
    bool pending = false;
    int i;

    if (s_http_state.queue != NULL) {
        while (xQueueReceive(s_http_state.queue, &event, 0) == pdTRUE) {
            if (event.slot_id < ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS) {
                esp32_mquickjs_http_async_slot_t *slot =
                    &s_http_state.slots[event.slot_id];

                if (slot->allocated && slot->generation == event.generation) {
                    http_async_cleanup_slot(ctx, slot);
                }
            }
            esp32_mquickjs_http_free_response(event.response);
        }
    }

    http_lock();
    for (i = 0; i < ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS; ++i) {
        if (s_http_state.slots[i].allocated) {
            pending = true;
            (void)esp32_mquickjs_http_operation_cancel(s_http_state.slots[i].operation);
        }
    }
    http_unlock();
    if (pending) {
        return false;
    }

    if (s_http_state.queue != NULL) {
        vQueueDelete(s_http_state.queue);
    }
    if (s_http_state.lock != NULL) {
        vSemaphoreDelete(s_http_state.lock);
    }
    memset(&s_http_state, 0, sizeof(s_http_state));
    return true;
}

static bool http_async_poller(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              void *opaque)
{
    esp32_mquickjs_http_async_event_t event;
    bool handled = false;

    (void)opaque;
    (void)runtime;
    if (ctx == NULL || s_http_state.queue == NULL) {
        return false;
    }

    while (xQueueReceive(s_http_state.queue, &event, 0) == pdTRUE) {
        esp32_mquickjs_http_async_slot_t *slot;
        JSGCRef callback_ref;
        JSValue *callback_fn;
        JSValue argv[2];
        JSValue callback_result;

        if (event.slot_id >= ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS) {
            esp32_mquickjs_http_free_response(event.response);
            continue;
        }

        slot = &s_http_state.slots[event.slot_id];
        if (!slot->allocated || slot->generation != event.generation) {
            esp32_mquickjs_http_free_response(event.response);
            continue;
        }

        handled = true;
        callback_fn = JS_PushGCRef(ctx, &callback_ref);
        *callback_fn = slot->callback.val;
        if (esp32_mquickjs_http_operation_is_cancelled(slot->operation)) {
            esp32_mquickjs_http_free_response(event.response);
            event.response = NULL;
            event.err = ESP_ERR_INVALID_STATE;
            snprintf(event.error_text, sizeof(event.error_text), "fetch cancelled");
        }
        http_async_cleanup_slot(ctx, slot);

        if (event.err != ESP_OK) {
            argv[0] = JS_UNDEFINED;
            argv[1] = JS_NewString(ctx, event.error_text[0] != '\0' ? event.error_text : esp_err_to_name(event.err));
        } else {
            argv[0] = esp32_mquickjs_http_make_response_object(ctx, event.response);
            argv[1] = JS_UNDEFINED;
            if (JS_IsException(argv[0])) {
                JS_PopGCRef(ctx, &callback_ref);
                esp32_mquickjs_http_free_response(event.response);
                return true;
            }
        }

        callback_result = esp32_mquickjs_http_call_function(ctx, *callback_fn, JS_NULL, 2, argv);
        if (JS_IsException(callback_result)) {
            esp32_mquickjs_print_exception(ctx);
        }

        JS_PopGCRef(ctx, &callback_ref);
        esp32_mquickjs_http_free_response(event.response);
    }

    return handled;
}

#endif
