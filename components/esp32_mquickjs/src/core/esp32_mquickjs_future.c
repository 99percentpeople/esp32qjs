#include "esp32_mquickjs_future.h"

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future_runtime_resources.h"
#include "esp32_mquickjs_future_scheduler.h"
#include "esp32_mquickjs_future_timeout.h"
#include "esp32_mquickjs_future_worker_pool.h"
#include "esp32_mquickjs_options.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define ESP32_MQUICKJS_FUTURE_MAX_ARGS 16U
#define ESP32_MQUICKJS_FUTURE_MAX_DRIVERS 128U
#define ESP32_MQUICKJS_FUTURE_SLOT_COUNT \
    (CONFIG_ESP32_MQUICKJS_MAX_FUTURES + CONFIG_ESP32_MQUICKJS_INTERNAL_FUTURE_RESERVE)

typedef enum {
    FUTURE_STATE_QUEUED,
    FUTURE_STATE_PENDING,
    FUTURE_STATE_FULFILLED,
    FUTURE_STATE_REJECTED,
    FUTURE_STATE_CANCELLED,
} future_state_t;

typedef enum {
    FUTURE_KIND_CALL,
    FUTURE_KIND_DRIVER,
    FUTURE_KIND_JS_CALL,
    FUTURE_KIND_SLEEP,
    FUTURE_KIND_ALL,
    FUTURE_KIND_RACE,
    FUTURE_KIND_TIMEOUT,
    FUTURE_KIND_MAP,
    FUTURE_KIND_FLAT_MAP,
} future_kind_t;

typedef struct {
    esp32_mquickjs_runtime_t *runtime;
    uint8_t slot;
    uint32_t generation;
    bool terminal;
    bool observed;
    bool rejection_reported;
    bool result_retained;
    future_state_t state;
    JSValue result;
} future_handle_t;

typedef struct {
    JSGCRef function;
    const esp32_mquickjs_future_driver_t *driver;
    bool retained;
} future_driver_entry_t;

typedef struct {
    esp32_mquickjs_runtime_t *runtime;
    uint8_t slot_id;
    uint32_t generation;
    bool allocated;
    bool call_refs_retained;
    bool result_retained;
    bool input_refs_retained;
    bool driver_active;
    bool driver_started;
    bool lane_waiting;
    bool cancel_requested;
    bool observed;
    future_state_t state;
    future_kind_t kind;
    uint64_t submitted_us;
    uint64_t submission_sequence;
    uint64_t deadline_us;
    JSGCRef function;
    JSGCRef receiver;
    JSGCRef *arguments;
    uint16_t argument_count;
    JSGCRef result;
    JSGCRef *inputs;
    uint16_t input_count;
    esp_timer_handle_t timer;
    const esp32_mquickjs_future_driver_t *driver;
    esp32_mquickjs_future_driver_state_t *driver_state;
    esp32_mquickjs_resource_key_t resource_key;
    future_handle_t *handle;
    bool continuation_running;
    bool continuation_called;
} future_slot_t;

typedef struct {
    JSContext *ctx;
    QueueHandle_t submissions;
    QueueHandle_t ready;
    future_slot_t *slots;
    esp32_mquickjs_future_runtime_resources_t resources;
    future_driver_entry_t drivers[ESP32_MQUICKJS_FUTURE_MAX_DRIVERS];
    size_t driver_count;
    int running_slot;
    uint64_t next_submission_sequence;
    uint16_t internal_allocation_depth;
    bool shutting_down;
} future_runtime_t;

typedef struct {
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp32_mquickjs_future_worker_fn_t function;
    void *opaque;
    bool wake_future;
} future_worker_item_t;

static const char *TAG = "esp32qjs_future";
static QueueHandle_t s_future_worker_queue;
static bool s_future_worker_pool_initialized;

static void *future_runtime_resource_allocate(size_t count,
                                              size_t size,
                                              void *opaque)
{
    (void)opaque;
    return heap_caps_calloc(count, size, MALLOC_CAP_8BIT);
}

static void future_runtime_resource_release(void *value, void *opaque)
{
    (void)opaque;
    heap_caps_free(value);
}

static void *future_runtime_queue_create(size_t length,
                                         size_t item_size,
                                         void *opaque)
{
    (void)opaque;
    return xQueueCreate((UBaseType_t)length, (UBaseType_t)item_size);
}

static void future_runtime_queue_delete(void *queue, void *opaque)
{
    (void)opaque;
    vQueueDelete((QueueHandle_t)queue);
}

static const esp32_mquickjs_future_runtime_resource_ops_t
    s_future_runtime_resource_ops = {
        .allocate = future_runtime_resource_allocate,
        .release = future_runtime_resource_release,
        .queue_create = future_runtime_queue_create,
        .queue_delete = future_runtime_queue_delete,
    };

typedef struct {
    TaskHandle_t *workers;
    QueueHandle_t queue;
} future_worker_pool_cleanup_t;

static void future_scheduler_snapshot(
    const future_runtime_t *state,
    esp32_mquickjs_future_scheduler_slot_t *out_slots)
{
    int i;

    if (state == NULL || state->slots == NULL || out_slots == NULL) {
        return;
    }
    for (i = 0; i < ESP32_MQUICKJS_FUTURE_SLOT_COUNT; ++i) {
        const future_slot_t *slot = &state->slots[i];

        out_slots[i].allocated = slot->allocated;
        out_slots[i].driver_active = slot->driver_active;
        out_slots[i].driver_started = slot->driver_started;
        out_slots[i].lane_waiting = slot->lane_waiting;
        out_slots[i].submission_sequence = slot->submission_sequence;
        out_slots[i].resource_key = slot->resource_key;
    }
}

static void future_cleanup_partial_worker(size_t worker_index, void *opaque)
{
    future_worker_pool_cleanup_t *cleanup = opaque;

    if (cleanup != NULL && cleanup->workers != NULL) {
        vTaskDelete(cleanup->workers[worker_index]);
    }
}

static void future_cleanup_partial_queue(void *opaque)
{
    future_worker_pool_cleanup_t *cleanup = opaque;

    if (cleanup != NULL && cleanup->queue != NULL) {
        vQueueDelete(cleanup->queue);
    }
}

static void future_worker_task(void *opaque)
{
    future_worker_item_t item;

    (void)opaque;
    for (;;) {
        if (xQueueReceive(s_future_worker_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (item.function != NULL) {
            item.function(item.opaque);
            if (item.wake_future) {
                (void)esp32_mquickjs_future_wake(item.runtime, item.token);
            }
        }
    }
}

static bool future_init_worker_pool(void)
{
    TaskHandle_t workers[CONFIG_ESP32_MQUICKJS_FUTURE_WORKER_POOL_SIZE] = {0};
    future_worker_pool_cleanup_t cleanup = {
        .workers = workers,
    };
    int started = 0;
    int i;

    if (s_future_worker_pool_initialized) {
        return true;
    }
    s_future_worker_queue = xQueueCreate(
        CONFIG_ESP32_MQUICKJS_FUTURE_WORKER_QUEUE_LEN,
        sizeof(future_worker_item_t));
    if (s_future_worker_queue == NULL) {
        return false;
    }
    for (i = 0; i < CONFIG_ESP32_MQUICKJS_FUTURE_WORKER_POOL_SIZE; ++i) {
        if (xTaskCreate(future_worker_task,
                        "mqjs_io",
                        4096,
                        NULL,
                        tskIDLE_PRIORITY + 2,
                        &workers[i]) == pdPASS) {
            started++;
        } else {
            break;
        }
    }
    if (started != CONFIG_ESP32_MQUICKJS_FUTURE_WORKER_POOL_SIZE) {
        ESP_LOGE(TAG,
                 "Future worker pool initialization failed (%d/%d workers)",
                 started,
                 CONFIG_ESP32_MQUICKJS_FUTURE_WORKER_POOL_SIZE);
        cleanup.queue = s_future_worker_queue;
        esp32_mquickjs_future_worker_pool_cleanup_partial(
            (size_t)started, future_cleanup_partial_worker,
            future_cleanup_partial_queue, &cleanup);
        s_future_worker_queue = NULL;
        return false;
    }
    s_future_worker_pool_initialized = true;
    return true;
}

static future_runtime_t *future_runtime(esp32_mquickjs_runtime_t *runtime)
{
    return runtime != NULL ? runtime->future_state : NULL;
}

static bool future_is_terminal(future_state_t state)
{
    return state == FUTURE_STATE_FULFILLED ||
           state == FUTURE_STATE_REJECTED ||
           state == FUTURE_STATE_CANCELLED;
}

static const char *future_state_name(future_state_t state)
{
    switch (state) {
        case FUTURE_STATE_QUEUED:
            return "queued";
        case FUTURE_STATE_PENDING:
            return "pending";
        case FUTURE_STATE_FULFILLED:
            return "fulfilled";
        case FUTURE_STATE_REJECTED:
            return "rejected";
        case FUTURE_STATE_CANCELLED:
            return "cancelled";
        default:
            return "cancelled";
    }
}

static future_slot_t *future_resolve_token(esp32_mquickjs_runtime_t *runtime,
                                           esp32_mquickjs_future_token_t token)
{
    future_runtime_t *state = future_runtime(runtime);
    future_slot_t *slot;

    if (state == NULL || state->slots == NULL ||
        token.slot >= ESP32_MQUICKJS_FUTURE_SLOT_COUNT) {
        return NULL;
    }
    slot = &state->slots[token.slot];
    return slot->allocated && slot->generation == token.generation ? slot : NULL;
}

static esp32_mquickjs_future_token_t future_token(const future_slot_t *slot)
{
    esp32_mquickjs_future_token_t token = {0};

    if (slot != NULL) {
        token.slot = slot->slot_id;
        token.generation = slot->generation;
    }
    return token;
}

static void future_release_call_refs(JSContext *ctx, future_slot_t *slot)
{
    int i;

    if (slot == NULL || !slot->call_refs_retained) {
        return;
    }
    for (i = (int)slot->argument_count - 1; i >= 0; --i) {
        JS_DeleteGCRef(ctx, &slot->arguments[i]);
    }
    heap_caps_free(slot->arguments);
    slot->arguments = NULL;
    slot->argument_count = 0;
    JS_DeleteGCRef(ctx, &slot->receiver);
    JS_DeleteGCRef(ctx, &slot->function);
    slot->call_refs_retained = false;
}

static void future_release_input_refs(JSContext *ctx, future_slot_t *slot)
{
    int i;

    if (slot == NULL || !slot->input_refs_retained) {
        return;
    }
    for (i = (int)slot->input_count - 1; i >= 0; --i) {
        JS_DeleteGCRef(ctx, &slot->inputs[i]);
    }
    heap_caps_free(slot->inputs);
    slot->inputs = NULL;
    slot->input_count = 0;
    slot->input_refs_retained = false;
}

static void future_stop_timer(future_slot_t *slot)
{
    if (slot == NULL || slot->timer == NULL) {
        return;
    }
    (void)esp_timer_stop(slot->timer);
    (void)esp_timer_delete(slot->timer);
    slot->timer = NULL;
}

static void future_destroy_driver(future_slot_t *slot)
{
    if (slot == NULL || !slot->driver_active) {
        return;
    }
    if (slot->driver != NULL && slot->driver->destroy != NULL) {
        slot->driver->destroy(slot->driver_state);
    }
    slot->driver_state = NULL;
    slot->driver = NULL;
    slot->driver_active = false;
    slot->driver_started = false;
    slot->lane_waiting = false;
    slot->resource_key = NULL;
}

static void future_clear_slot(JSContext *ctx, future_slot_t *slot)
{
    uint8_t slot_id;
    uint32_t generation;
    esp32_mquickjs_runtime_t *runtime;

    if (slot == NULL || !slot->allocated) {
        return;
    }
    slot_id = slot->slot_id;
    generation = slot->generation;
    runtime = slot->runtime;
    future_stop_timer(slot);
    future_destroy_driver(slot);
    future_release_call_refs(ctx, slot);
    future_release_input_refs(ctx, slot);
    if (slot->result_retained) {
        JS_DeleteGCRef(ctx, &slot->result);
    }
    memset(slot, 0, sizeof(*slot));
    slot->slot_id = slot_id;
    slot->generation = generation;
    slot->runtime = runtime;
}

static void future_report_unobserved(JSContext *ctx, future_handle_t *handle)
{
    if (handle == NULL || !handle->terminal || handle->state != FUTURE_STATE_REJECTED ||
        handle->observed || handle->rejection_reported || !handle->result_retained) {
        return;
    }
    handle->rejection_reported = true;
    (void)JS_Throw(ctx, handle->result);
    esp32_mquickjs_print_exception(ctx);
}

static void future_release_if_terminal(JSContext *ctx, future_slot_t *slot)
{
    if (slot == NULL || !slot->allocated || !future_is_terminal(slot->state) ||
        slot->driver_active) {
        return;
    }
    future_clear_slot(ctx, slot);
}

static void future_publish_terminal(JSContext *ctx, future_slot_t *slot)
{
    future_handle_t *handle;

    if (slot == NULL || !slot->allocated || !future_is_terminal(slot->state)) {
        return;
    }
    handle = slot->handle;
    if (handle == NULL) {
        if (slot->state == FUTURE_STATE_REJECTED && !slot->observed &&
            slot->result_retained) {
            (void)JS_Throw(ctx, slot->result.val);
            esp32_mquickjs_print_exception(ctx);
        }
        return;
    }
    handle->terminal = true;
    handle->observed = handle->observed || slot->observed;
    handle->state = slot->state;
    if (slot->result_retained) {
        handle->result = slot->result.val;
        handle->result_retained = true;
    }
    slot->handle = NULL;
}

static void future_store_result(JSContext *ctx, future_slot_t *slot, JSValue value)
{
    JSValue *rooted;

    if (slot->result_retained) {
        JS_DeleteGCRef(ctx, &slot->result);
    }
    rooted = JS_AddGCRef(ctx, &slot->result);
    *rooted = value;
    slot->result_retained = true;
}

static void future_settle(JSContext *ctx,
                          future_slot_t *slot,
                          future_state_t state,
                          JSValue value)
{
    if (slot == NULL || !slot->allocated || future_is_terminal(slot->state)) {
        return;
    }
    if (state == FUTURE_STATE_FULFILLED || state == FUTURE_STATE_REJECTED) {
        future_store_result(ctx, slot, value);
    }
    slot->state = state;
    future_release_call_refs(ctx, slot);
    future_release_input_refs(ctx, slot);
    future_stop_timer(slot);
    future_publish_terminal(ctx, slot);
    future_release_if_terminal(ctx, slot);
}

static void future_reject_current_exception(JSContext *ctx, future_slot_t *slot)
{
    JSValue exception = JS_GetException(ctx);

    if (JS_IsUndefined(exception)) {
        exception = JS_NewString(ctx, "Future operation failed");
    }
    future_settle(ctx, slot, FUTURE_STATE_REJECTED, exception);
}

static void future_reject_message(JSContext *ctx, future_slot_t *slot, const char *message)
{
    future_settle(ctx,
                  slot,
                  FUTURE_STATE_REJECTED,
                  JS_NewString(ctx, message != NULL ? message : "Future operation failed"));
}

static future_slot_t *future_find_free_slot(future_runtime_t *state,
                                            bool internal)
{
    esp32_mquickjs_future_scheduler_slot_t
        slots[ESP32_MQUICKJS_FUTURE_SLOT_COUNT];
    size_t index;

    if (state == NULL || state->slots == NULL) {
        return NULL;
    }
    future_scheduler_snapshot(state, slots);
    index = esp32_mquickjs_future_scheduler_find_free(
        slots, ESP32_MQUICKJS_FUTURE_SLOT_COUNT,
        CONFIG_ESP32_MQUICKJS_MAX_FUTURES, internal);
    return index != ESP32_MQUICKJS_FUTURE_SCHEDULER_NO_SLOT
               ? &state->slots[index]
               : NULL;
}

static future_slot_t *future_allocate_slot(esp32_mquickjs_runtime_t *runtime,
                                           future_kind_t kind)
{
    future_runtime_t *state = future_runtime(runtime);
    future_slot_t *slot;
    bool internal;

    if (state == NULL || state->shutting_down) {
        return NULL;
    }
    internal = state->internal_allocation_depth > 0;
    slot = future_find_free_slot(state, internal);
    if (slot == NULL) {
        return NULL;
    }
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation++;
    }
    slot->allocated = true;
    slot->kind = kind;
    slot->state = FUTURE_STATE_QUEUED;
    slot->submitted_us = (uint64_t)esp_timer_get_time();
    slot->submission_sequence = ++state->next_submission_sequence;
    slot->deadline_us = runtime->scoped_deadline_us;
    return slot;
}

static JSValue future_make_handle(JSContext *ctx, future_slot_t *slot)
{
    future_handle_t *handle;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue result;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_FUTURE);
    if (JS_IsException(*object)) {
        future_clear_slot(ctx, slot);
        return JS_PopGCRef(ctx, &object_ref);
    }
    handle = heap_caps_calloc(1, sizeof(*handle), MALLOC_CAP_8BIT);
    if (handle == NULL) {
        future_clear_slot(ctx, slot);
        result = JS_ThrowOutOfMemory(ctx);
        JS_PopGCRef(ctx, &object_ref);
        return result;
    }
    handle->runtime = slot->runtime;
    handle->slot = slot->slot_id;
    handle->generation = slot->generation;
    handle->state = slot->state;
    handle->result = JS_UNDEFINED;
    slot->handle = handle;
    JS_SetOpaque(ctx, *object, handle);
    return JS_PopGCRef(ctx, &object_ref);
}

static void future_abandon_handle(future_slot_t *slot)
{
    if (slot == NULL || slot->handle == NULL) {
        return;
    }
    /* The JS object owns the handle until MQuickJS runs its finalizer. */
    slot->handle = NULL;
}

static bool future_submit(future_slot_t *slot)
{
    future_runtime_t *state;
    esp32_mquickjs_future_token_t token;
    UBaseType_t queued_count;
    UBaseType_t i;

    if (slot == NULL) {
        return false;
    }
    state = future_runtime(slot->runtime);
    token = future_token(slot);
    if (state == NULL || state->submissions == NULL) {
        return false;
    }
    if (xQueueSend(state->submissions, &token, 0) != pdTRUE) {
        esp32_mquickjs_future_token_t queued_token;

        queued_count = uxQueueMessagesWaiting(state->submissions);
        for (i = 0; i < queued_count; ++i) {
            future_slot_t *queued_slot;

            if (xQueueReceive(state->submissions, &queued_token, 0) != pdTRUE) {
                break;
            }
            queued_slot = future_resolve_token(slot->runtime, queued_token);
            if (queued_slot != NULL && queued_slot->state == FUTURE_STATE_QUEUED) {
                (void)xQueueSend(state->submissions, &queued_token, 0);
            }
        }
        if (xQueueSend(state->submissions, &token, 0) != pdTRUE) {
            return false;
        }
    }
    esp32_mquickjs_notify_activity(slot->runtime);
    return true;
}

static future_handle_t *future_handle_from_value(JSContext *ctx, JSValue value)
{
    future_handle_t *handle;

    if (JS_GetClassID(ctx, value) != JS_CLASS_FUTURE) {
        return NULL;
    }
    handle = JS_GetOpaque(ctx, value);
    if (handle == NULL) {
        return NULL;
    }
    return handle;
}

static future_slot_t *future_handle_slot(future_handle_t *handle)
{
    if (handle == NULL || handle->terminal) {
        return NULL;
    }
    return future_resolve_token(handle->runtime,
                                (esp32_mquickjs_future_token_t){
                                    .slot = handle->slot,
                                    .generation = handle->generation,
                                });
}

static void future_mark_observed(future_handle_t *handle)
{
    future_slot_t *slot;

    if (handle == NULL) {
        return;
    }
    handle->observed = true;
    slot = future_handle_slot(handle);
    if (slot != NULL) {
        slot->observed = true;
    }
}

static future_handle_t *future_this_handle(JSContext *ctx,
                                           JSValue *this_val,
                                           const char *api_name)
{
    future_handle_t *handle = this_val != NULL
        ? future_handle_from_value(ctx, *this_val) : NULL;

    if (handle == NULL || (!handle->terminal && future_handle_slot(handle) == NULL)) {
        JS_ThrowTypeError(ctx, "%s expects a live Future", api_name);
    }
    return handle;
}

static int future_array_length(JSContext *ctx, JSValue array, uint32_t *out_length)
{
    JSValue length_value;
    int result;

    if (JS_GetClassID(ctx, array) != JS_CLASS_ARRAY) {
        return -1;
    }
    length_value = JS_GetPropertyStr(ctx, array, "length");
    if (JS_IsException(length_value)) {
        return -1;
    }
    result = JS_ToUint32(ctx, out_length, length_value);
    return result;
}

static bool future_retain_call(JSContext *ctx,
                               future_slot_t *slot,
                               JSValue function,
                               JSValue receiver,
                               JSValue args_array)
{
    JSGCRef function_ref;
    JSGCRef receiver_ref;
    JSGCRef args_ref;
    JSValue *rooted_function;
    JSValue *rooted_receiver;
    JSValue *rooted_args;
    uint32_t argument_count = 0;
    JSValue *ref;
    uint32_t i;
    bool retained = false;

    rooted_function = JS_PushGCRef(ctx, &function_ref);
    rooted_receiver = JS_PushGCRef(ctx, &receiver_ref);
    rooted_args = JS_PushGCRef(ctx, &args_ref);
    *rooted_function = function;
    *rooted_receiver = receiver;
    *rooted_args = args_array;

    if (!JS_IsUndefined(*rooted_args) &&
        future_array_length(ctx, *rooted_args, &argument_count) != 0) {
        JS_ThrowTypeError(ctx, "Future.call(fn, thisValue?, args?) expects args to be an array");
        goto done;
    }
    if (argument_count > ESP32_MQUICKJS_FUTURE_MAX_ARGS) {
        JS_ThrowRangeError(ctx,
                           "Future.call() supports at most %u arguments",
                           (unsigned)ESP32_MQUICKJS_FUTURE_MAX_ARGS);
        goto done;
    }
    if (argument_count > 0) {
        slot->arguments = heap_caps_calloc(argument_count,
                                           sizeof(*slot->arguments),
                                           MALLOC_CAP_8BIT);
        if (slot->arguments == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }
    }

    ref = JS_AddGCRef(ctx, &slot->function);
    *ref = *rooted_function;
    ref = JS_AddGCRef(ctx, &slot->receiver);
    *ref = *rooted_receiver;
    slot->call_refs_retained = true;
    slot->argument_count = 0;

    for (i = 0; i < argument_count; ++i) {
        JSValue value = JS_GetPropertyUint32(ctx, *rooted_args, i);

        if (JS_IsException(value)) {
            goto done;
        }
        ref = JS_AddGCRef(ctx, &slot->arguments[i]);
        *ref = value;
        slot->argument_count++;
    }
    retained = true;

done:
    JS_PopGCRef(ctx, &args_ref);
    JS_PopGCRef(ctx, &receiver_ref);
    JS_PopGCRef(ctx, &function_ref);
    return retained;
}

static bool future_retain_inputs(JSContext *ctx,
                                 future_slot_t *slot,
                                 JSValue inputs_array,
                                 bool allow_empty)
{
    JSGCRef inputs_ref;
    JSValue *rooted_inputs;
    uint32_t input_count = 0;
    uint32_t i;
    bool retained = false;

    rooted_inputs = JS_PushGCRef(ctx, &inputs_ref);
    *rooted_inputs = inputs_array;
    if (future_array_length(ctx, *rooted_inputs, &input_count) != 0) {
        JS_ThrowTypeError(ctx, "Future combinator expects an array of Futures");
        goto done;
    }
    if (!allow_empty && input_count == 0) {
        JS_ThrowRangeError(ctx, "Future.race() expects at least one Future");
        goto done;
    }
    if (input_count > CONFIG_ESP32_MQUICKJS_MAX_FUTURES) {
        JS_ThrowRangeError(ctx,
                           "Future combinator supports at most %u inputs",
                           (unsigned)CONFIG_ESP32_MQUICKJS_MAX_FUTURES);
        goto done;
    }
    if (input_count > 0) {
        slot->inputs = heap_caps_calloc(input_count,
                                        sizeof(*slot->inputs),
                                        MALLOC_CAP_8BIT);
        if (slot->inputs == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }
    }
    slot->input_count = 0;
    slot->input_refs_retained = true;
    for (i = 0; i < input_count; ++i) {
        JSValue value = JS_GetPropertyUint32(ctx, *rooted_inputs, i);
        JSValue *rooted;

        if (JS_IsException(value) || future_handle_from_value(ctx, value) == NULL) {
            if (!JS_IsException(value)) {
                JS_ThrowTypeError(ctx, "Future combinator input %u is not a live Future", (unsigned)i);
            }
            goto done;
        }
        rooted = JS_AddGCRef(ctx, &slot->inputs[i]);
        *rooted = value;
        slot->input_count++;
    }
    retained = true;

done:
    JS_PopGCRef(ctx, &inputs_ref);
    return retained;
}

static bool future_retain_continuation(JSContext *ctx,
                                       future_slot_t *slot,
                                       JSValue input,
                                       JSValue callback)
{
    JSValue *rooted;

    if (slot == NULL || future_handle_from_value(ctx, input) == NULL ||
        !JS_IsFunction(ctx, callback)) {
        JS_ThrowTypeError(ctx, "Future continuation expects a live Future and a function");
        return false;
    }
    slot->inputs = heap_caps_calloc(1, sizeof(*slot->inputs), MALLOC_CAP_8BIT);
    if (slot->inputs == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    rooted = JS_AddGCRef(ctx, &slot->inputs[0]);
    *rooted = input;
    slot->input_count = 1;
    slot->input_refs_retained = true;
    rooted = JS_AddGCRef(ctx, &slot->function);
    *rooted = callback;
    rooted = JS_AddGCRef(ctx, &slot->receiver);
    *rooted = JS_UNDEFINED;
    slot->call_refs_retained = true;
    return true;
}

static void future_observe_inputs(JSContext *ctx, future_slot_t *slot)
{
    uint16_t i;

    if (slot == NULL) {
        return;
    }
    /* Promise-style combinators attach a rejection handler to every input. */
    for (i = 0; i < slot->input_count; ++i) {
        future_mark_observed(future_handle_from_value(ctx, slot->inputs[i].val));
    }
}

static const esp32_mquickjs_future_driver_t *future_find_driver(future_runtime_t *state,
                                                                JSValue function)
{
    size_t i;

    if (state == NULL) {
        return NULL;
    }
    for (i = 0; i < state->driver_count; ++i) {
        if (state->drivers[i].retained && state->drivers[i].function.val == function) {
            return state->drivers[i].driver;
        }
    }
    return NULL;
}

static void future_apply_driver_deadline(future_slot_t *slot)
{
    uint32_t timeout_ms;
    uint64_t deadline_us;

    if (slot == NULL || slot->driver == NULL || slot->driver->timeout_ms == NULL) {
        return;
    }
    timeout_ms = slot->driver->timeout_ms(slot->driver_state);
    if (timeout_ms == 0) {
        return;
    }
    deadline_us = slot->submitted_us + ((uint64_t)timeout_ms * 1000ULL);
    if (slot->deadline_us == 0 || deadline_us < slot->deadline_us) {
        slot->deadline_us = deadline_us;
    }
}

static bool future_capture_call_driver(JSContext *ctx,
                                       future_runtime_t *state,
                                       future_slot_t *slot)
{
    const esp32_mquickjs_future_driver_t *driver;

    if (slot == NULL || !slot->call_refs_retained) {
        return false;
    }
    driver = future_find_driver(state, slot->function.val);
    if (driver == NULL) {
        return true;
    }
    slot->driver = driver;
    slot->kind = FUTURE_KIND_DRIVER;
    if (driver->capture == NULL ||
        !driver->capture(ctx,
                         &slot->receiver,
                         slot->argument_count,
                         slot->arguments,
                         &slot->driver_state)) {
        slot->driver = NULL;
        slot->kind = FUTURE_KIND_CALL;
        future_reject_current_exception(ctx, slot);
        return false;
    }
    slot->driver_active = true;
    slot->resource_key = driver->resource_key != NULL
        ? driver->resource_key(slot->driver_state) : NULL;
    future_apply_driver_deadline(slot);
    future_release_call_refs(ctx, slot);
    return true;
}

static esp32_mquickjs_future_lane_admission_t future_lane_admission(
    const future_runtime_t *state,
    const future_slot_t *candidate)
{
    esp32_mquickjs_future_scheduler_slot_t
        slots[ESP32_MQUICKJS_FUTURE_SLOT_COUNT];

    if (state == NULL || candidate == NULL ||
        candidate->slot_id >= ESP32_MQUICKJS_FUTURE_SLOT_COUNT) {
        return ESP32_MQUICKJS_FUTURE_LANE_REJECT;
    }
    future_scheduler_snapshot(state, slots);
    return esp32_mquickjs_future_scheduler_lane_admission(
        slots, ESP32_MQUICKJS_FUTURE_SLOT_COUNT, candidate->slot_id,
        CONFIG_ESP32_MQUICKJS_FUTURE_RESOURCE_LANE_QUEUE_LEN);
}

static bool future_start_captured_driver(JSContext *ctx,
                                         esp32_mquickjs_runtime_t *runtime,
                                         future_slot_t *slot)
{
    if (slot == NULL || slot->state != FUTURE_STATE_QUEUED ||
        !slot->driver_active || slot->driver == NULL ||
        slot->driver_state == NULL || slot->driver->start == NULL) {
        return false;
    }
    slot->lane_waiting = false;
    slot->driver_started = true;
    slot->state = FUTURE_STATE_PENDING;
    if (!slot->driver->start(ctx, runtime, future_token(slot),
                             slot->driver_state)) {
        future_reject_current_exception(ctx, slot);
        future_destroy_driver(slot);
        future_release_if_terminal(ctx, slot);
    }
    return true;
}

static void future_dispatch_call(JSContext *ctx,
                                 esp32_mquickjs_runtime_t *runtime,
                                 future_slot_t *slot)
{
    future_runtime_t *state = future_runtime(runtime);
    JSValue *argv = NULL;
    uint64_t saved_deadline_us;
    JSValue result;
    int i;

    if (slot == NULL || slot->state != FUTURE_STATE_QUEUED) {
        return;
    }
    if (slot->kind == FUTURE_KIND_DRIVER) {
        esp32_mquickjs_future_lane_admission_t admission;

        if (!slot->driver_active || slot->driver == NULL ||
            slot->driver_state == NULL || slot->driver->start == NULL) {
            future_reject_message(ctx, slot, "Future native driver was not captured");
            future_destroy_driver(slot);
            future_release_if_terminal(ctx, slot);
            return;
        }
        admission = future_lane_admission(state, slot);
        if (admission == ESP32_MQUICKJS_FUTURE_LANE_REJECT) {
            future_reject_message(ctx, slot,
                                  "Future resource lane queue is full");
            future_destroy_driver(slot);
            future_release_if_terminal(ctx, slot);
            return;
        }
        if (admission == ESP32_MQUICKJS_FUTURE_LANE_WAIT) {
            slot->lane_waiting = true;
            return;
        }
        if (!future_start_captured_driver(ctx, runtime, slot)) {
            future_reject_message(ctx, slot, "Future native driver failed to start");
            future_destroy_driver(slot);
            future_release_if_terminal(ctx, slot);
        }
        return;
    }
    if (slot->kind != FUTURE_KIND_CALL || !slot->call_refs_retained) {
        future_reject_message(ctx, slot, "Future has an invalid call operation");
        return;
    }

    if (slot->argument_count > 0) {
        argv = heap_caps_calloc(slot->argument_count, sizeof(*argv), MALLOC_CAP_8BIT);
        if (argv == NULL) {
            future_reject_message(ctx, slot, "Future dispatch ran out of memory");
            return;
        }
        for (i = 0; i < slot->argument_count; ++i) {
            argv[i] = slot->arguments[i].val;
        }
    }

    slot->kind = FUTURE_KIND_JS_CALL;
    slot->state = FUTURE_STATE_PENDING;
    state->running_slot = slot->slot_id;
    saved_deadline_us = runtime->deadline_us;
    if (slot->deadline_us > 0 &&
        (runtime->deadline_us == 0 || slot->deadline_us < runtime->deadline_us)) {
        runtime->deadline_us = slot->deadline_us;
    }
    result = esp32_mquickjs_call(ctx,
                                 runtime,
                                 slot->function.val,
                                 slot->receiver.val,
                                 slot->argument_count,
                                 argv);
    runtime->deadline_us = saved_deadline_us;
    state->running_slot = -1;
    heap_caps_free(argv);
    future_release_call_refs(ctx, slot);
    if (future_is_terminal(slot->state)) {
        if (JS_IsException(result)) {
            (void)JS_GetException(ctx);
        }
        future_publish_terminal(ctx, slot);
        future_release_if_terminal(ctx, slot);
    } else if (JS_IsException(result)) {
        future_reject_current_exception(ctx, slot);
    } else {
        future_settle(ctx, slot, FUTURE_STATE_FULFILLED, result);
    }
}

static void future_sleep_timer_callback(void *opaque)
{
    future_slot_t *slot = opaque;

    if (slot != NULL && slot->allocated) {
        (void)esp32_mquickjs_future_wake(slot->runtime, future_token(slot));
    }
}

static void future_start_sleep(JSContext *ctx, future_slot_t *slot)
{
    esp_timer_create_args_t args = {0};
    uint64_t now_us = (uint64_t)esp_timer_get_time();
    uint64_t remaining_us;

    slot->state = FUTURE_STATE_PENDING;
    if (slot->deadline_us <= now_us) {
        future_settle(ctx, slot, FUTURE_STATE_FULFILLED, JS_UNDEFINED);
        return;
    }
    remaining_us = slot->deadline_us - now_us;
    args.callback = future_sleep_timer_callback;
    args.arg = slot;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "mqjs_future";
    args.skip_unhandled_events = true;
    if (esp_timer_create(&args, &slot->timer) != ESP_OK ||
        esp_timer_start_once(slot->timer, remaining_us) != ESP_OK) {
        future_stop_timer(slot);
        future_reject_message(ctx, slot, "Future.sleep() failed to start timer");
    }
}

static void future_dispatch_submission(JSContext *ctx,
                                       esp32_mquickjs_runtime_t *runtime,
                                       future_slot_t *slot)
{
    if (slot == NULL || slot->state != FUTURE_STATE_QUEUED) {
        return;
    }
    switch (slot->kind) {
        case FUTURE_KIND_CALL:
        case FUTURE_KIND_DRIVER:
            future_dispatch_call(ctx, runtime, slot);
            break;
        case FUTURE_KIND_SLEEP:
            future_start_sleep(ctx, slot);
            break;
        case FUTURE_KIND_ALL:
        case FUTURE_KIND_RACE:
        case FUTURE_KIND_TIMEOUT:
        case FUTURE_KIND_MAP:
        case FUTURE_KIND_FLAT_MAP:
            slot->state = FUTURE_STATE_PENDING;
            break;
        default:
            future_reject_message(ctx, slot, "Future has an invalid queued operation");
            break;
    }
}

static future_state_t future_handle_state(future_handle_t *handle)
{
    future_slot_t *slot;

    if (handle == NULL) {
        return FUTURE_STATE_CANCELLED;
    }
    if (handle->terminal) {
        return handle->state;
    }
    slot = future_handle_slot(handle);
    return slot != NULL ? slot->state : FUTURE_STATE_CANCELLED;
}

static JSValue future_handle_result(future_handle_t *handle)
{
    future_slot_t *slot;

    if (handle == NULL) {
        return JS_UNDEFINED;
    }
    if (handle->terminal) {
        return handle->result_retained ? handle->result : JS_UNDEFINED;
    }
    slot = future_handle_slot(handle);
    return slot != NULL && slot->result_retained ? slot->result.val : JS_UNDEFINED;
}

static void future_copy_terminal(JSContext *ctx,
                                 future_slot_t *target,
                                 future_handle_t *source)
{
    future_state_t state = future_handle_state(source);

    if (source != NULL) {
        future_mark_observed(source);
    }
    if (state == FUTURE_STATE_FULFILLED) {
        future_settle(ctx, target, FUTURE_STATE_FULFILLED, future_handle_result(source));
    } else if (state == FUTURE_STATE_REJECTED) {
        future_settle(ctx, target, FUTURE_STATE_REJECTED, future_handle_result(source));
    } else if (state == FUTURE_STATE_CANCELLED) {
        future_settle(ctx, target, FUTURE_STATE_CANCELLED, JS_UNDEFINED);
    }
}

static void future_advance_all(JSContext *ctx, future_slot_t *slot)
{
    JSGCRef results_ref;
    JSValue *results;
    uint16_t i;

    for (i = 0; i < slot->input_count; ++i) {
        future_handle_t *input = future_handle_from_value(ctx, slot->inputs[i].val);
        future_state_t state;

        if (input == NULL) {
            future_reject_message(ctx, slot, "Future.all() input became stale");
            return;
        }
        state = future_handle_state(input);
        if (state == FUTURE_STATE_REJECTED || state == FUTURE_STATE_CANCELLED) {
            future_copy_terminal(ctx, slot, input);
            return;
        }
        if (state != FUTURE_STATE_FULFILLED) {
            return;
        }
    }

    results = JS_PushGCRef(ctx, &results_ref);
    *results = JS_NewArray(ctx, slot->input_count);
    if (JS_IsException(*results)) {
        JS_PopGCRef(ctx, &results_ref);
        future_reject_current_exception(ctx, slot);
        return;
    }
    for (i = 0; i < slot->input_count; ++i) {
        future_handle_t *input = future_handle_from_value(ctx, slot->inputs[i].val);

        if (input == NULL ||
            JS_IsException(JS_SetPropertyUint32(ctx, *results, i,
                                                future_handle_result(input)))) {
            JS_PopGCRef(ctx, &results_ref);
            future_reject_current_exception(ctx, slot);
            return;
        }
    }
    future_settle(ctx, slot, FUTURE_STATE_FULFILLED, *results);
    JS_PopGCRef(ctx, &results_ref);
}

static void future_advance_race(JSContext *ctx, future_slot_t *slot)
{
    uint16_t i;

    for (i = 0; i < slot->input_count; ++i) {
        future_handle_t *input = future_handle_from_value(ctx, slot->inputs[i].val);
        future_state_t state;

        if (input == NULL) {
            future_reject_message(ctx, slot, "Future.race() input became stale");
            return;
        }
        state = future_handle_state(input);
        if (!future_is_terminal(state)) {
            continue;
        }
        if (state == FUTURE_STATE_FULFILLED) {
            JSGCRef result_ref;
            JSValue *result = JS_PushGCRef(ctx, &result_ref);

            *result = JS_NewObject(ctx);
            if (JS_IsException(*result) ||
                !esp32_mquickjs_set_property_ref(ctx, result, "index", JS_NewUint32(ctx, i)) ||
                !esp32_mquickjs_set_property_ref(ctx, result, "value",
                                                future_handle_result(input))) {
                JS_PopGCRef(ctx, &result_ref);
                future_reject_current_exception(ctx, slot);
                return;
            }
            future_settle(ctx, slot, FUTURE_STATE_FULFILLED, *result);
            JS_PopGCRef(ctx, &result_ref);
        } else {
            future_copy_terminal(ctx, slot, input);
        }
        return;
    }
}

static bool future_cancel_slot(JSContext *ctx, future_slot_t *slot)
{
    esp32_mquickjs_cancel_result_t result;

    if (slot == NULL || future_is_terminal(slot->state)) {
        return false;
    }
    if (slot->state == FUTURE_STATE_QUEUED) {
        future_destroy_driver(slot);
        future_settle(ctx, slot, FUTURE_STATE_CANCELLED, JS_UNDEFINED);
        return true;
    }
    if (slot->driver_active) {
        if (slot->cancel_requested || slot->driver == NULL ||
            slot->driver->cancel == NULL) {
            return false;
        }
        result = slot->driver->cancel(slot->driver_state);
        if (result == ESP32_MQUICKJS_CANCEL_REJECTED) {
            return false;
        }
        if (result == ESP32_MQUICKJS_CANCEL_REQUESTED) {
            slot->cancel_requested = true;
            return true;
        }
    }
    future_settle(ctx, slot, FUTURE_STATE_CANCELLED, JS_UNDEFINED);
    return true;
}

static void future_advance_timeout(JSContext *ctx, future_slot_t *slot, uint64_t now_us)
{
    future_handle_t *input;
    future_slot_t *input_slot;
    future_state_t input_state;

    if (slot->input_count != 1) {
        future_reject_message(ctx, slot, "Future.timeout() lost its input");
        return;
    }
    input = future_handle_from_value(ctx, slot->inputs[0].val);
    if (input == NULL) {
        future_reject_message(ctx, slot, "Future.timeout() input became stale");
        return;
    }
    input_state = future_handle_state(input);
    if (future_is_terminal(input_state)) {
        future_copy_terminal(ctx, slot, input);
    } else if (slot->deadline_us > 0 && now_us >= slot->deadline_us) {
        char message[96];
        uint32_t timeout_ms = esp32_mquickjs_future_elapsed_timeout_ms(
            slot->submitted_us, slot->deadline_us);

        input_slot = future_handle_slot(input);
        if (input_slot != NULL) {
            future_mark_observed(input);
            (void)future_cancel_slot(ctx, input_slot);
        }
        snprintf(message, sizeof(message), "Future.timeout() expired after %" PRIu32 " ms", timeout_ms);
        future_reject_message(ctx, slot, message);
    }
}

static void future_advance_continuation(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime,
                                        future_slot_t *slot)
{
    future_handle_t *input;
    future_state_t input_state;
    JSValue result;
    JSValue argument;

    if (slot->input_count != 1) {
        future_reject_message(ctx, slot, "Future continuation lost its input");
        return;
    }
    input = future_handle_from_value(ctx, slot->inputs[0].val);
    if (input == NULL) {
        future_reject_message(ctx, slot, "Future continuation input became stale");
        return;
    }
    input_state = future_handle_state(input);
    if (!future_is_terminal(input_state)) {
        return;
    }
    future_mark_observed(input);
    if (input_state != FUTURE_STATE_FULFILLED) {
        future_copy_terminal(ctx, slot, input);
        return;
    }
    if (slot->continuation_running) {
        return;
    }
    if (slot->continuation_called) {
        future_copy_terminal(ctx, slot, input);
        return;
    }

    argument = future_handle_result(input);
    slot->continuation_running = true;
    result = esp32_mquickjs_call(ctx,
                                 runtime,
                                 slot->function.val,
                                 JS_UNDEFINED,
                                 1,
                                 &argument);
    slot->continuation_running = false;
    slot->continuation_called = true;
    if (JS_IsException(result)) {
        future_reject_current_exception(ctx, slot);
        return;
    }
    if (slot->kind == FUTURE_KIND_MAP) {
        future_settle(ctx, slot, FUTURE_STATE_FULFILLED, result);
        return;
    }

    input = future_handle_from_value(ctx, result);
    if (input == NULL) {
        future_reject_message(ctx, slot, "future.flatMap() callback must return a Future");
        return;
    }
    JS_DeleteGCRef(ctx, &slot->inputs[0]);
    *JS_AddGCRef(ctx, &slot->inputs[0]) = result;
    future_mark_observed(input);
}

static bool future_advance_combinators(JSContext *ctx,
                                       esp32_mquickjs_runtime_t *runtime)
{
    future_runtime_t *state = future_runtime(runtime);
    uint64_t now_us = (uint64_t)esp_timer_get_time();
    bool handled = false;
    int i;

    for (i = 0; i < ESP32_MQUICKJS_FUTURE_SLOT_COUNT; ++i) {
        future_slot_t *slot = &state->slots[i];
        future_state_t before;

        if (!slot->allocated || slot->state != FUTURE_STATE_PENDING) {
            continue;
        }
        before = slot->state;
        if (slot->kind == FUTURE_KIND_ALL) {
            future_advance_all(ctx, slot);
        } else if (slot->kind == FUTURE_KIND_RACE) {
            future_advance_race(ctx, slot);
        } else if (slot->kind == FUTURE_KIND_TIMEOUT) {
            future_advance_timeout(ctx, slot, now_us);
        } else if (slot->kind == FUTURE_KIND_MAP ||
                   slot->kind == FUTURE_KIND_FLAT_MAP) {
            future_advance_continuation(ctx, runtime, slot);
        }
        handled = handled || slot->state != before;
    }
    return handled;
}

static bool future_advance_sleep_deadlines(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime)
{
    future_runtime_t *state = future_runtime(runtime);
    uint64_t now_us = (uint64_t)esp_timer_get_time();
    bool handled = false;
    int i;

    if (state == NULL || state->slots == NULL) {
        return false;
    }
    for (i = 0; i < ESP32_MQUICKJS_FUTURE_SLOT_COUNT; ++i) {
        future_slot_t *slot = &state->slots[i];

        if (!slot->allocated || slot->state != FUTURE_STATE_PENDING ||
            slot->kind != FUTURE_KIND_SLEEP || slot->deadline_us == 0 ||
            slot->deadline_us > now_us) {
            continue;
        }
        /*
         * The ready queue is only a latency hint. A full queue must not strand
         * a timer-backed Future after its esp_timer callback has fired.
         */
        future_settle(ctx, slot, FUTURE_STATE_FULFILLED, JS_UNDEFINED);
        handled = true;
    }
    return handled;
}

static bool future_expire_deadlines(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime)
{
    future_runtime_t *state = future_runtime(runtime);
    uint64_t now_us = (uint64_t)esp_timer_get_time();
    bool handled = false;
    int i;

    for (i = 0; i < ESP32_MQUICKJS_FUTURE_SLOT_COUNT; ++i) {
        future_slot_t *slot = &state->slots[i];
        char message[96];
        uint32_t timeout_ms;
        JSValue timeout_error = JS_UNDEFINED;
        bool custom_timeout_error = false;

        if (!slot->allocated || future_is_terminal(slot->state) ||
            slot->kind == FUTURE_KIND_SLEEP || slot->kind == FUTURE_KIND_TIMEOUT ||
            slot->deadline_us == 0 || now_us < slot->deadline_us) {
            continue;
        }
        timeout_ms = esp32_mquickjs_future_elapsed_timeout_ms(
            slot->submitted_us, slot->deadline_us);
        if (slot->driver_active && slot->driver != NULL &&
            slot->driver->on_timeout != NULL) {
            JSValue result = slot->driver->on_timeout(
                ctx, slot->driver_state, timeout_ms);

            if (JS_IsException(result) && JS_HasException(ctx)) {
                timeout_error = JS_GetException(ctx);
                custom_timeout_error = true;
            }
        }
        if (slot->driver_active && slot->state == FUTURE_STATE_QUEUED) {
            future_destroy_driver(slot);
        } else if (slot->driver_active && slot->driver != NULL &&
                   slot->driver->cancel != NULL && !slot->cancel_requested) {
            esp32_mquickjs_cancel_result_t cancel_result =
                slot->driver->cancel(slot->driver_state);

            if (cancel_result == ESP32_MQUICKJS_CANCEL_REQUESTED) {
                slot->cancel_requested = true;
            }
        }
        if (custom_timeout_error) {
            future_settle(
                ctx, slot, FUTURE_STATE_REJECTED, timeout_error);
        } else {
            snprintf(message, sizeof(message), "Future operation timed out after %" PRIu32 " ms", timeout_ms);
            future_reject_message(ctx, slot, message);
        }
        handled = true;
    }
    return handled;
}

static bool future_poll_ready(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              esp32_mquickjs_future_token_t token)
{
    future_slot_t *slot = future_resolve_token(runtime, token);
    JSValue result;

    if (slot == NULL) {
        return false;
    }
    if (slot->kind == FUTURE_KIND_SLEEP) {
        if (!future_is_terminal(slot->state)) {
            future_settle(ctx, slot, FUTURE_STATE_FULFILLED, JS_UNDEFINED);
        }
        return true;
    }
    if (!slot->driver_active || slot->driver == NULL || slot->driver->poll == NULL ||
        slot->driver->poll(slot->driver_state) != ESP32_MQUICKJS_FUTURE_READY) {
        return false;
    }
    if (future_is_terminal(slot->state)) {
        future_destroy_driver(slot);
        future_publish_terminal(ctx, slot);
        future_release_if_terminal(ctx, slot);
        return true;
    }
    if (slot->cancel_requested) {
        future_destroy_driver(slot);
        future_settle(ctx, slot, FUTURE_STATE_CANCELLED, JS_UNDEFINED);
        return true;
    }
    result = slot->driver->finish(ctx, slot->driver_state);
    future_destroy_driver(slot);
    if (JS_IsException(result)) {
        future_reject_current_exception(ctx, slot);
    } else {
        future_settle(ctx, slot, FUTURE_STATE_FULFILLED, result);
    }
    return true;
}

static bool future_poll_active_drivers(JSContext *ctx,
                                       esp32_mquickjs_runtime_t *runtime)
{
    future_runtime_t *state = future_runtime(runtime);
    bool handled = false;
    int i;

    if (state == NULL || state->slots == NULL) {
        return false;
    }
    /*
     * A wake token is only a latency optimization. ISR and worker completions
     * use a bounded queue, so a full queue must not be able to strand a driver
     * that has already completed or acknowledged cancellation. Polling the
     * bounded active-slot set at each scheduler safe point also guarantees
     * teardown progress when the final wake races with runtime shutdown.
     */
    for (i = 0; i < ESP32_MQUICKJS_FUTURE_SLOT_COUNT; ++i) {
        future_slot_t *slot = &state->slots[i];

        if (!slot->allocated || !slot->driver_active ||
            slot->state == FUTURE_STATE_QUEUED) {
            continue;
        }
        handled = future_poll_ready(ctx, runtime, future_token(slot)) || handled;
    }
    return handled;
}

static bool future_dispatch_waiting_lanes(JSContext *ctx,
                                          esp32_mquickjs_runtime_t *runtime)
{
    future_runtime_t *state = future_runtime(runtime);
    bool handled = false;
    int dispatched = 0;

    if (state == NULL || state->slots == NULL) {
        return false;
    }
    while (dispatched < CONFIG_ESP32_MQUICKJS_FUTURE_DISPATCH_BATCH) {
        esp32_mquickjs_future_scheduler_slot_t
            slots[ESP32_MQUICKJS_FUTURE_SLOT_COUNT];
        future_slot_t *candidate;
        size_t candidate_index;

        future_scheduler_snapshot(state, slots);
        candidate_index = esp32_mquickjs_future_scheduler_next_waiting(
            slots, ESP32_MQUICKJS_FUTURE_SLOT_COUNT);
        if (candidate_index == ESP32_MQUICKJS_FUTURE_SCHEDULER_NO_SLOT) {
            break;
        }
        candidate = &state->slots[candidate_index];
        if (candidate->state != FUTURE_STATE_QUEUED) {
            candidate->lane_waiting = false;
            continue;
        }
        handled = future_start_captured_driver(ctx, runtime, candidate) || handled;
        dispatched++;
    }
    return handled;
}

bool esp32_mquickjs_init_future_runtime(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_future_runtime_resources_t resources;
    future_runtime_t *state;
    int i;

    if (ctx == NULL || runtime == NULL) {
        return false;
    }
    if (runtime->future_state != NULL) {
        return true;
    }
    if (!future_init_worker_pool()) {
        return false;
    }
    if (!esp32_mquickjs_future_runtime_resources_init(
            &resources, &s_future_runtime_resource_ops, sizeof(*state),
            ESP32_MQUICKJS_FUTURE_SLOT_COUNT, sizeof(*state->slots),
            CONFIG_ESP32_MQUICKJS_FUTURE_READY_QUEUE_LEN,
            sizeof(esp32_mquickjs_future_token_t))) {
        return false;
    }
    state = resources.runtime_state;
    state->resources = resources;
    state->slots = resources.slots;
    state->submissions = resources.submissions;
    state->ready = resources.ready;
    state->ctx = ctx;
    state->running_slot = -1;
    for (i = 0; i < ESP32_MQUICKJS_FUTURE_SLOT_COUNT; ++i) {
        state->slots[i].runtime = runtime;
        state->slots[i].slot_id = (uint8_t)i;
    }
    runtime->future_state = state;
    return true;
}

bool esp32_mquickjs_prepare_future_runtime_destroy(JSContext *ctx,
                                                   esp32_mquickjs_runtime_t *runtime)
{
    future_runtime_t *state = future_runtime(runtime);
    bool driver_pending = false;
    size_t i;

    if (state == NULL) {
        return true;
    }
    state->shutting_down = true;
    for (i = 0; i < ESP32_MQUICKJS_FUTURE_SLOT_COUNT; ++i) {
        future_slot_t *slot = &state->slots[i];

        if (!slot->allocated) {
            continue;
        }
        if (slot->driver_active) {
            bool driver_ready;
            bool poll_available;

            if (slot->state == FUTURE_STATE_QUEUED) {
                future_clear_slot(ctx, slot);
                continue;
            }
            if (!future_is_terminal(slot->state) && slot->driver != NULL &&
                slot->driver->cancel != NULL && !slot->cancel_requested) {
                esp32_mquickjs_cancel_result_t cancel_result =
                    slot->driver->cancel(slot->driver_state);

                if (cancel_result == ESP32_MQUICKJS_CANCELLED) {
                    slot->state = FUTURE_STATE_CANCELLED;
                } else if (cancel_result == ESP32_MQUICKJS_CANCEL_REQUESTED) {
                    slot->cancel_requested = true;
                }
            }
            poll_available = slot->driver != NULL &&
                             slot->driver->poll != NULL;
            driver_ready = poll_available &&
                           slot->driver->poll(slot->driver_state) ==
                               ESP32_MQUICKJS_FUTURE_READY;
            if (esp32_mquickjs_future_scheduler_teardown_must_wait(
                    slot->driver_active, poll_available, driver_ready)) {
                driver_pending = true;
                continue;
            }
        }
        future_clear_slot(ctx, slot);
    }
    if (driver_pending) {
        return false;
    }
    for (i = 0; i < state->driver_count; ++i) {
        if (state->drivers[i].retained) {
            JS_DeleteGCRef(ctx, &state->drivers[i].function);
            state->drivers[i].retained = false;
        }
    }
    state->driver_count = 0;
    return true;
}

void esp32_mquickjs_deinit_future_runtime(esp32_mquickjs_runtime_t *runtime)
{
    future_runtime_t *state = future_runtime(runtime);
    esp32_mquickjs_future_runtime_resources_t resources;

    if (state == NULL) {
        return;
    }
    resources = state->resources;
    runtime->future_state = NULL;
    esp32_mquickjs_future_runtime_resources_deinit(
        &resources, &s_future_runtime_resource_ops);
}

bool esp32_mquickjs_get_future_status(esp32_mquickjs_runtime_t *runtime,
                                      esp32_mquickjs_future_status_t *status)
{
    future_runtime_t *state = future_runtime(runtime);
    size_t i;

    if (status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    status->capacity = ESP32_MQUICKJS_FUTURE_SLOT_COUNT;
    status->user_capacity = CONFIG_ESP32_MQUICKJS_MAX_FUTURES;
    status->internal_reserve = CONFIG_ESP32_MQUICKJS_INTERNAL_FUTURE_RESERVE;
    if (state == NULL || state->slots == NULL) {
        return true;
    }
    for (i = 0; i < ESP32_MQUICKJS_FUTURE_SLOT_COUNT; ++i) {
        future_slot_t *slot = &state->slots[i];

        if (!slot->allocated) {
            continue;
        }
        if (slot->state == FUTURE_STATE_QUEUED) {
            status->queued++;
        } else if (slot->state == FUTURE_STATE_PENDING) {
            status->pending++;
        }
    }
    return true;
}

bool esp32_mquickjs_future_register_driver(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime,
                                           JSValue function,
                                           const esp32_mquickjs_future_driver_t *driver)
{
    future_runtime_t *state = future_runtime(runtime);
    future_driver_entry_t *entry;
    JSValue *rooted;
    size_t i;

    if (ctx == NULL || state == NULL || driver == NULL || !JS_IsFunction(ctx, function)) {
        return false;
    }
    for (i = 0; i < state->driver_count; ++i) {
        if (state->drivers[i].function.val == function) {
            return state->drivers[i].driver == driver;
        }
    }
    if (state->driver_count >= ESP32_MQUICKJS_FUTURE_MAX_DRIVERS) {
        ESP_LOGE(TAG,
                 "Future driver registry exhausted: count=%u capacity=%u",
                 (unsigned)state->driver_count,
                 (unsigned)ESP32_MQUICKJS_FUTURE_MAX_DRIVERS);
        return false;
    }
    entry = &state->drivers[state->driver_count++];
    rooted = JS_AddGCRef(ctx, &entry->function);
    *rooted = function;
    entry->driver = driver;
    entry->retained = true;
    return true;
}

bool esp32_mquickjs_future_wake(esp32_mquickjs_runtime_t *runtime,
                               esp32_mquickjs_future_token_t token)
{
    future_runtime_t *state = future_runtime(runtime);

    if (state == NULL || state->ready == NULL ||
        xQueueSend(state->ready, &token, 0) != pdTRUE) {
        return false;
    }
    esp32_mquickjs_notify_activity(runtime);
    return true;
}

bool esp32_mquickjs_future_wake_from_isr(esp32_mquickjs_runtime_t *runtime,
                                        esp32_mquickjs_future_token_t token,
                                        int *task_woken)
{
    future_runtime_t *state = future_runtime(runtime);
    BaseType_t woken = pdFALSE;

    if (state == NULL || state->ready == NULL ||
        xQueueSendFromISR(state->ready, &token, &woken) != pdTRUE) {
        return false;
    }
    esp32_mquickjs_notify_active_runtime_from_isr((int *)&woken);
    if (task_woken != NULL && woken == pdTRUE) {
        *task_woken = 1;
    }
    return true;
}

bool esp32_mquickjs_future_submit_worker(
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_worker_fn_t function,
    void *opaque)
{
    future_worker_item_t item = {
        .runtime = runtime,
        .token = token,
        .function = function,
        .opaque = opaque,
        .wake_future = true,
    };

    return runtime != NULL && function != NULL &&
           s_future_worker_pool_initialized && s_future_worker_queue != NULL &&
           xQueueSend(s_future_worker_queue, &item, 0) == pdTRUE;
}

bool esp32_mquickjs_submit_background_worker(
    esp32_mquickjs_future_worker_fn_t function,
    void *opaque)
{
    future_worker_item_t item = {
        .function = function,
        .opaque = opaque,
        .wake_future = false,
    };

    return function != NULL && s_future_worker_pool_initialized &&
           s_future_worker_queue != NULL &&
           xQueueSend(s_future_worker_queue, &item, 0) == pdTRUE;
}

bool esp32_mquickjs_future_poll(JSContext *ctx,
                               esp32_mquickjs_runtime_t *runtime)
{
    future_runtime_t *state = future_runtime(runtime);
    esp32_mquickjs_future_token_t token;
    bool handled = false;
    int count = 0;

    if (ctx == NULL || state == NULL || state->shutting_down) {
        return false;
    }
    handled = future_expire_deadlines(ctx, runtime);
    handled = future_advance_sleep_deadlines(ctx, runtime) || handled;
    while (count < CONFIG_ESP32_MQUICKJS_FUTURE_DISPATCH_BATCH &&
           xQueueReceive(state->submissions, &token, 0) == pdTRUE) {
        future_slot_t *slot = future_resolve_token(runtime, token);

        if (slot != NULL) {
            future_dispatch_submission(ctx, runtime, slot);
            handled = true;
        }
        count++;
    }
    count = 0;
    while (count < CONFIG_ESP32_MQUICKJS_FUTURE_READY_BATCH &&
           xQueueReceive(state->ready, &token, 0) == pdTRUE) {
        handled = future_poll_ready(ctx, runtime, token) || handled;
        count++;
    }
    handled = future_poll_active_drivers(ctx, runtime) || handled;
    handled = future_dispatch_waiting_lanes(ctx, runtime) || handled;
    handled = future_advance_combinators(ctx, runtime) || handled;
    return handled;
}

bool esp32_mquickjs_future_cooperate(esp32_mquickjs_runtime_t *runtime)
{
    future_runtime_t *state = future_runtime(runtime);

    return state != NULL && state->ctx != NULL
        ? esp32_mquickjs_poll(state->ctx, runtime) != ESP32_MQUICKJS_POLL_NONE
        : false;
}

uint32_t esp32_mquickjs_future_next_wait_ms(esp32_mquickjs_runtime_t *runtime,
                                           uint32_t requested_ms)
{
    future_runtime_t *state = future_runtime(runtime);
    uint64_t now_us = (uint64_t)esp_timer_get_time();
    uint32_t result = requested_ms;
    int i;

    if (state == NULL) {
        return result;
    }
    if (uxQueueMessagesWaiting(state->submissions) > 0 ||
        uxQueueMessagesWaiting(state->ready) > 0) {
        return 0;
    }
    for (i = 0; i < ESP32_MQUICKJS_FUTURE_SLOT_COUNT; ++i) {
        future_slot_t *slot = &state->slots[i];
        uint64_t remaining_us;
        uint32_t remaining_ms;

        if (!slot->allocated || future_is_terminal(slot->state) || slot->deadline_us == 0) {
            continue;
        }
        if (slot->deadline_us <= now_us) {
            return 0;
        }
        remaining_us = slot->deadline_us - now_us;
        remaining_ms = (uint32_t)((remaining_us + 999ULL) / 1000ULL);
        if (result == UINT32_MAX || remaining_ms < result) {
            result = remaining_ms;
        }
    }
    return result;
}

JSValue esp32_mquickjs_future_call_and_wait(JSContext *ctx,
                                            esp32_mquickjs_runtime_t *runtime,
                                            JSValue function,
                                            JSValue this_value,
                                            int argc,
                                            JSValue *argv)
{
    JSGCRef function_ref;
    JSGCRef receiver_ref;
    JSGCRef args_ref;
    JSGCRef future_ref;
    JSGCRef result_ref;
    JSValue *rooted_function;
    JSValue *rooted_receiver;
    JSValue *args_array;
    JSValue *future;
    JSValue *result;
    JSValue return_value;
    JSValue call_args[3];
    future_runtime_t *state = future_runtime(runtime);
    future_handle_t *handle;
    int i;

    if (ctx == NULL || state == NULL || argc < 0 || (argc > 0 && argv == NULL)) {
        return JS_EXCEPTION;
    }
    rooted_function = JS_PushGCRef(ctx, &function_ref);
    rooted_receiver = JS_PushGCRef(ctx, &receiver_ref);
    args_array = JS_PushGCRef(ctx, &args_ref);
    future = JS_PushGCRef(ctx, &future_ref);
    result = JS_PushGCRef(ctx, &result_ref);
    *rooted_function = function;
    *rooted_receiver = this_value;
    *args_array = JS_NewArray(ctx, argc);
    *future = JS_UNDEFINED;
    *result = JS_EXCEPTION;
    if (JS_IsException(*args_array)) {
        goto done;
    }
    for (i = 0; i < argc; ++i) {
        if (JS_IsException(JS_SetPropertyUint32(ctx, *args_array, (uint32_t)i, argv[i]))) {
            goto done;
        }
    }
    call_args[0] = *rooted_function;
    call_args[1] = *rooted_receiver;
    call_args[2] = *args_array;
    state->internal_allocation_depth++;
    *future = js_future_call(ctx, NULL, 3, call_args);
    state->internal_allocation_depth--;
    if (JS_IsException(*future)) {
        goto done;
    }
    *result = js_future_wait(ctx, future, 0, NULL);
    handle = JS_GetOpaque(ctx, *future);
    if (handle != NULL) {
        JS_SetOpaque(ctx, *future, NULL);
        js_future_finalizer(ctx, handle);
    }

done:
    return_value = JS_PopGCRef(ctx, &result_ref);
    JS_PopGCRef(ctx, &future_ref);
    JS_PopGCRef(ctx, &args_ref);
    JS_PopGCRef(ctx, &receiver_ref);
    JS_PopGCRef(ctx, &function_ref);
    return return_value;
}

JSValue js_future_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "Future cannot be constructed directly");
}

void js_future_finalizer(JSContext *ctx, void *opaque)
{
    future_handle_t *handle = opaque;
    future_slot_t *slot;

    if (handle == NULL) {
        return;
    }
    slot = future_handle_slot(handle);
    if (slot != NULL && slot->handle == handle) {
        slot->handle = NULL;
        future_release_if_terminal(ctx, slot);
    }
    future_report_unobserved(ctx, handle);
    handle->result = JS_UNDEFINED;
    handle->result_retained = false;
    heap_caps_free(handle);
}

void js_future_gc_trace(JSContext *ctx, void *opaque,
                        JSCGCVisitor visit, void *visitor_opaque)
{
    future_handle_t *handle = opaque;

    (void)ctx;
    if (handle != NULL && handle->result_retained) {
        visit(visitor_opaque, &handle->result);
    }
}

JSValue js_future_call(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    future_runtime_t *state = future_runtime(runtime);
    JSGCRef function_ref;
    JSGCRef receiver_ref;
    JSGCRef args_ref;
    JSGCRef result_ref;
    JSValue *function = JS_PushGCRef(ctx, &function_ref);
    JSValue *receiver = JS_PushGCRef(ctx, &receiver_ref);
    JSValue *args = JS_PushGCRef(ctx, &args_ref);
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    future_slot_t *slot;
    JSValue return_value;

    (void)this_val;
    *function = argc >= 1 ? argv[0] : JS_UNDEFINED;
    *receiver = argc >= 2 && !JS_IsUndefined(argv[1])
        ? argv[1] : JS_GetGlobalObject(ctx);
    *args = argc >= 3 ? argv[2] : JS_UNDEFINED;
    *result = JS_EXCEPTION;
    if (argc < 1 || argc > 3 || !JS_IsFunction(ctx, *function)) {
        *result = JS_ThrowTypeError(ctx,
                                    "Future.call(fn, thisValue?, args?) expects a function");
        goto done;
    }
    slot = future_allocate_slot(runtime, FUTURE_KIND_CALL);
    if (slot == NULL) {
        *result = JS_ThrowInternalError(ctx, "Future capacity is exhausted");
        goto done;
    }
    if (!future_retain_call(ctx,
                            slot,
                            *function,
                            *receiver,
                            *args)) {
        future_clear_slot(ctx, slot);
        goto done;
    }
    *result = future_make_handle(ctx, slot);
    if (JS_IsException(*result)) {
        goto done;
    }
    if (!future_capture_call_driver(ctx, state, slot)) {
        goto done;
    }
    if (!future_submit(slot)) {
        future_abandon_handle(slot);
        future_clear_slot(ctx, slot);
        *result = JS_ThrowInternalError(ctx, "Future submission queue is full");
    }

done:
    return_value = JS_PopGCRef(ctx, &result_ref);
    JS_PopGCRef(ctx, &args_ref);
    JS_PopGCRef(ctx, &receiver_ref);
    JS_PopGCRef(ctx, &function_ref);
    return return_value;
}

static JSValue future_make_combinator(JSContext *ctx,
                                      JSValue inputs,
                                      future_kind_t kind,
                                      bool allow_empty)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    JSGCRef inputs_ref;
    JSValue *rooted_inputs = JS_PushGCRef(ctx, &inputs_ref);
    future_slot_t *slot;
    JSValue result;

    *rooted_inputs = inputs;
    slot = future_allocate_slot(runtime, kind);
    if (slot == NULL) {
        result = JS_ThrowInternalError(ctx, "Future capacity is exhausted");
        goto done;
    }
    if (!future_retain_inputs(ctx, slot, *rooted_inputs, allow_empty)) {
        future_clear_slot(ctx, slot);
        result = JS_EXCEPTION;
        goto done;
    }
    result = future_make_handle(ctx, slot);
    if (JS_IsException(result)) {
        goto done;
    }
    if (!future_submit(slot)) {
        future_abandon_handle(slot);
        future_clear_slot(ctx, slot);
        result = JS_ThrowInternalError(ctx, "Future submission queue is full");
    } else {
        future_observe_inputs(ctx, slot);
    }

done:
    JS_PopGCRef(ctx, &inputs_ref);
    return result;
}

JSValue js_future_all(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1) {
        return JS_ThrowTypeError(ctx, "Future.all(futures) expects one array");
    }
    return future_make_combinator(ctx, argv[0], FUTURE_KIND_ALL, true);
}

JSValue js_future_race(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1) {
        return JS_ThrowTypeError(ctx, "Future.race(futures) expects one array");
    }
    return future_make_combinator(ctx, argv[0], FUTURE_KIND_RACE, false);
}

JSValue js_future_sleep(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    future_slot_t *slot;
    int delay_ms;
    JSValue result;

    (void)this_val;
    if (argc != 1 || JS_ToInt32(ctx, &delay_ms, argv[0]) != 0 || delay_ms < 0) {
        return JS_ThrowTypeError(ctx, "Future.sleep(ms) expects a non-negative integer");
    }
    slot = future_allocate_slot(runtime, FUTURE_KIND_SLEEP);
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "Future capacity is exhausted");
    }
    slot->deadline_us = slot->submitted_us + ((uint64_t)(uint32_t)delay_ms * 1000ULL);
    result = future_make_handle(ctx, slot);
    if (JS_IsException(result)) {
        return result;
    }
    if (!future_submit(slot)) {
        future_abandon_handle(slot);
        future_clear_slot(ctx, slot);
        return JS_ThrowInternalError(ctx, "Future submission queue is full");
    }
    return result;
}

JSValue js_future_timeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    future_slot_t *slot;
    JSGCRef inputs_ref;
    JSValue *inputs;
    JSValue result;
    uint32_t timeout_ms;

    (void)this_val;
    if (argc != 2 || future_handle_from_value(ctx, argv[0]) == NULL ||
        !esp32_mquickjs_value_to_bounded_u32(
            ctx, argv[1], 0, INT32_MAX, &timeout_ms)) {
        return JS_ThrowTypeError(ctx, "Future.timeout(future, timeoutMs) expects a Future and non-negative integer");
    }
    slot = future_allocate_slot(runtime, FUTURE_KIND_TIMEOUT);
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "Future capacity is exhausted");
    }
    inputs = JS_PushGCRef(ctx, &inputs_ref);
    *inputs = JS_NewArray(ctx, 1);
    if (JS_IsException(*inputs) || JS_IsException(JS_SetPropertyUint32(ctx, *inputs, 0, argv[0])) ||
        !future_retain_inputs(ctx, slot, *inputs, false)) {
        JS_PopGCRef(ctx, &inputs_ref);
        future_clear_slot(ctx, slot);
        return JS_EXCEPTION;
    }
    JS_PopGCRef(ctx, &inputs_ref);
    slot->deadline_us = slot->submitted_us +
                        ((uint64_t)timeout_ms * 1000ULL);
    result = future_make_handle(ctx, slot);
    if (JS_IsException(result)) {
        return result;
    }
    if (!future_submit(slot)) {
        future_abandon_handle(slot);
        future_clear_slot(ctx, slot);
        return JS_ThrowInternalError(ctx, "Future submission queue is full");
    }
    future_observe_inputs(ctx, slot);
    return result;
}

static JSValue future_make_continuation(JSContext *ctx,
                                        JSValue *this_val,
                                        int argc,
                                        JSValue *argv,
                                        future_kind_t kind,
                                        const char *api_name)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    future_handle_t *input = future_this_handle(ctx, this_val, api_name);
    future_slot_t *slot;
    JSValue result;

    if (input == NULL) {
        return JS_EXCEPTION;
    }
    if (argc != 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "%s expects one function", api_name);
    }
    slot = future_allocate_slot(runtime, kind);
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "Future capacity is exhausted");
    }
    if (!future_retain_continuation(ctx, slot, *this_val, argv[0])) {
        future_clear_slot(ctx, slot);
        return JS_EXCEPTION;
    }
    result = future_make_handle(ctx, slot);
    if (JS_IsException(result)) {
        return result;
    }
    if (!future_submit(slot)) {
        future_abandon_handle(slot);
        future_clear_slot(ctx, slot);
        return JS_ThrowInternalError(ctx, "Future submission queue is full");
    }
    future_observe_inputs(ctx, slot);
    return result;
}

JSValue js_future_map(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return future_make_continuation(ctx, this_val, argc, argv,
                                    FUTURE_KIND_MAP, "future.map()");
}

JSValue js_future_flat_map(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return future_make_continuation(ctx, this_val, argc, argv,
                                    FUTURE_KIND_FLAT_MAP, "future.flatMap()");
}

JSValue js_future_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    future_handle_t *handle = future_this_handle(ctx, this_val, "future.status()");

    (void)argc;
    (void)argv;
    return handle != NULL
        ? JS_NewString(ctx, future_state_name(future_handle_state(handle)))
        : JS_EXCEPTION;
}

JSValue js_future_wait(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    future_handle_t *handle = future_this_handle(ctx, this_val, "future.wait()");
    esp32_mquickjs_native_wait_t native_wait;
    uint64_t wait_deadline_us = 0;
    uint32_t timeout_ms = 0;
    JSValue result;

    if (handle == NULL) {
        return JS_EXCEPTION;
    }
    if (argc > 1 ||
        (argc == 1 && !esp32_mquickjs_value_to_bounded_u32(
            ctx, argv[0], 0, INT32_MAX, &timeout_ms))) {
        return JS_ThrowTypeError(ctx, "future.wait(timeoutMs?) expects a non-negative integer");
    }
    if (argc == 1) {
        wait_deadline_us = (uint64_t)esp_timer_get_time() +
                           ((uint64_t)timeout_ms * 1000ULL);
    }
    future_mark_observed(handle);
    esp32_mquickjs_native_wait_begin(runtime, &native_wait);
    while (!handle->terminal) {
        esp32_mquickjs_poll_result_t poll_result;
        uint32_t wait_ms = ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS;
        uint64_t now_us;

        if (future_handle_slot(handle) == NULL) {
            result = JS_ThrowInternalError(ctx, "future.wait() lost its operation");
            goto done;
        }

        poll_result = esp32_mquickjs_poll(ctx, runtime);
        if (handle->terminal) {
            break;
        }
        now_us = (uint64_t)esp_timer_get_time();
        if (wait_deadline_us > 0 && now_us >= wait_deadline_us) {
            result = JS_ThrowInternalError(
                ctx, "future.wait() timed out after %" PRIu32 " ms",
                timeout_ms);
            goto done;
        }
        if (!esp32_mquickjs_cooperate(runtime)) {
            result = JS_ThrowInternalError(
                ctx, "future.wait() was interrupted by a runtime stop request");
            goto done;
        }
        if (poll_result != ESP32_MQUICKJS_POLL_NONE) {
            continue;
        }
        if (wait_deadline_us > now_us) {
            uint64_t remaining_us = wait_deadline_us - now_us;
            uint32_t remaining_ms = (uint32_t)((remaining_us + 999ULL) / 1000ULL);

            if (remaining_ms < wait_ms) {
                wait_ms = remaining_ms;
            }
        }
        wait_ms = esp32_mquickjs_future_next_wait_ms(runtime, wait_ms);
        if (wait_ms == 0) {
            continue;
        }
        (void)esp32_mquickjs_wait_for_activity(runtime, wait_ms);
    }
    if (handle->state == FUTURE_STATE_FULFILLED) {
        result = handle->result;
    } else if (handle->state == FUTURE_STATE_REJECTED) {
        result = JS_Throw(ctx, handle->result);
    } else {
        result = JS_ThrowInternalError(ctx, "future.wait() was cancelled");
    }

done:
    esp32_mquickjs_native_wait_end(runtime, &native_wait);
    return result;
}

JSValue js_future_cancel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    future_handle_t *handle = future_this_handle(ctx, this_val, "future.cancel()");
    future_slot_t *slot;

    (void)argc;
    (void)argv;
    if (handle == NULL) {
        return JS_EXCEPTION;
    }
    future_mark_observed(handle);
    slot = future_handle_slot(handle);
    return JS_NewBool(slot != NULL && future_cancel_slot(ctx, slot));
}
