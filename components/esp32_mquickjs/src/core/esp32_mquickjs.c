#include "esp32_mquickjs_core.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_esp32.h"
#include "esp32_mquickjs_adc.h"
#include "esp32_mquickjs_dac.h"
#include "esp32_mquickjs_fs.h"
#include "esp32_mquickjs_gpio.h"
#include "esp32_mquickjs_http.h"
#include "esp32_mquickjs_http_server.h"
#include "esp32_mquickjs_i2c.h"
#include "esp32_mquickjs_ledc.h"
#include "esp32_mquickjs_spi.h"
#include "esp32_mquickjs_stream.h"
#include "esp32_mquickjs_wifi.h"
#include "js_stdlib.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static esp32_mquickjs_runtime_t *s_active_runtime;

typedef struct esp32_mquickjs_timer_slot esp32_mquickjs_timer_slot_t;

typedef struct {
    uint8_t timer_id;
    uint32_t generation;
} esp32_mquickjs_timer_event_t;

typedef struct {
    QueueHandle_t queue;
    esp32_mquickjs_timer_slot_t *slots;
} esp32_mquickjs_timer_state_t;

typedef struct {
    esp32_mquickjs_async_poller_t poller;
    void *opaque;
} esp32_mquickjs_async_poller_entry_t;

typedef struct {
    void *task_handle;
    size_t poller_count;
    esp32_mquickjs_async_poller_entry_t *pollers;
} esp32_mquickjs_async_state_t;

struct esp32_mquickjs_timer_slot {
    esp32_mquickjs_runtime_t *runtime;
    esp_timer_handle_t handle;
    JSGCRef callback;
    uint32_t generation;
    uint8_t timer_id;
    bool allocated;
    bool repeating;
    bool pending;
};

#define ESP32_MQUICKJS_MAX_ASYNC_POLLERS 8

static void run_pending_external_gc(JSContext *ctx)
{
    if (ctx != NULL && esp32_mquickjs_byte_source_take_gc_request()) {
        JS_GC(ctx);
    }
}

static void note_console_output(void)
{
    if (s_active_runtime != NULL) {
        s_active_runtime->output_generation++;
    }
}

static esp32_mquickjs_async_state_t *esp32_mquickjs_async_state(esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return NULL;
    }
    return runtime->async_state;
}

static bool esp32_mquickjs_init_async_state(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state;

    if (runtime == NULL) {
        return false;
    }
    if (runtime->async_state != NULL) {
        return true;
    }

    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        return false;
    }

    state->pollers = heap_caps_calloc(ESP32_MQUICKJS_MAX_ASYNC_POLLERS,
                                      sizeof(*state->pollers),
                                      MALLOC_CAP_8BIT);
    if (state->pollers == NULL) {
        heap_caps_free(state);
        return false;
    }

    runtime->async_state = state;
    return true;
}

bool esp32_mquickjs_register_async_poller(esp32_mquickjs_runtime_t *runtime,
                                          esp32_mquickjs_async_poller_t poller,
                                          void *opaque)
{
    esp32_mquickjs_async_state_t *state;
    size_t i;

    if (runtime == NULL || poller == NULL) {
        return false;
    }
    if (!esp32_mquickjs_init_async_state(runtime)) {
        return false;
    }

    state = esp32_mquickjs_async_state(runtime);
    if (state == NULL || state->pollers == NULL) {
        return false;
    }

    for (i = 0; i < state->poller_count; ++i) {
        if (state->pollers[i].poller == poller && state->pollers[i].opaque == opaque) {
            return true;
        }
    }
    if (state->poller_count >= ESP32_MQUICKJS_MAX_ASYNC_POLLERS) {
        return false;
    }

    state->pollers[state->poller_count].poller = poller;
    state->pollers[state->poller_count].opaque = opaque;
    state->poller_count++;
    return true;
}

void esp32_mquickjs_attach_current_task(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state;

    if (runtime == NULL || !esp32_mquickjs_init_async_state(runtime)) {
        return;
    }

    state = esp32_mquickjs_async_state(runtime);
    if (state != NULL) {
        state->task_handle = xTaskGetCurrentTaskHandle();
    }
}

void esp32_mquickjs_notify_activity(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state != NULL && state->task_handle != NULL) {
        xTaskNotifyGive(state->task_handle);
    }
}

void esp32_mquickjs_notify_active_runtime_from_isr(int *task_woken)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(s_active_runtime);

    if (state != NULL && state->task_handle != NULL) {
        vTaskNotifyGiveFromISR(state->task_handle, (BaseType_t *)task_woken);
    }
}

bool esp32_mquickjs_wait_for_activity(esp32_mquickjs_runtime_t *runtime,
                                      uint32_t timeout_ms)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);
    TickType_t wait_ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    if (timeout_ms != 0 && timeout_ms != UINT32_MAX && wait_ticks == 0) {
        wait_ticks = 1;
    }

    if (state == NULL || state->task_handle == NULL) {
        if (wait_ticks > 0) {
            vTaskDelay(wait_ticks);
        }
        return false;
    }

    return ulTaskNotifyTake(pdTRUE, wait_ticks) > 0;
}

static bool esp32_mquickjs_poll_registered(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);
    bool handled = false;
    size_t i;

    if (state == NULL || state->pollers == NULL) {
        return false;
    }

    for (i = 0; i < state->poller_count; ++i) {
        if (state->pollers[i].poller != NULL &&
            state->pollers[i].poller(ctx, runtime, state->pollers[i].opaque)) {
            handled = true;
        }
    }
    return handled;
}

static void prepare_console_output(void)
{
    if (s_active_runtime != NULL && s_active_runtime->prepare_output != NULL) {
        s_active_runtime->prepare_output(s_active_runtime->prepare_output_opaque);
    }
}

static void js_log_write(void *opaque, const void *buf, size_t buf_len)
{
    (void)opaque;
    fwrite(buf, 1, buf_len, stdout);
    fflush(stdout);
    note_console_output();
}

static int js_interrupt_handler(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_runtime_t *runtime = opaque;

    (void)ctx;
    if (runtime == NULL || runtime->deadline_us == 0) {
        return 0;
    }
    return esp_timer_get_time() > runtime->deadline_us;
}

static JSValue js_call_function(JSContext *ctx,
                                JSValue func,
                                JSValue this_val,
                                int argc,
                                JSValue *argv)
{
    int i;

    if (JS_StackCheck(ctx, (uint32_t)(argc + 2))) {
        return JS_EXCEPTION;
    }

    for (i = argc - 1; i >= 0; --i) {
        JS_PushArg(ctx, argv[i]);
    }
    JS_PushArg(ctx, func);
    JS_PushArg(ctx, this_val);
    return JS_Call(ctx, argc);
}

bool esp32_mquickjs_set_property(JSContext *ctx,
                                 JSValue target_obj,
                                 const char *name,
                                 JSValue value)
{
    return !JS_IsException(JS_SetPropertyStr(ctx, target_obj, name, value));
}

static int js_timeout_arg(JSContext *ctx,
                          JSValue value,
                          uint32_t default_timeout_ms,
                          uint32_t *timeout_ms)
{
    int timeout = 0;

    if (JS_IsUndefined(value)) {
        *timeout_ms = default_timeout_ms;
        return 0;
    }
    if (JS_ToInt32(ctx, &timeout, value) != 0 || timeout < 0) {
        return -1;
    }
    *timeout_ms = (uint32_t)timeout;
    return 0;
}

static bool js_is_object(JSContext *ctx, JSValue value)
{
    return JS_GetClassID(ctx, value) >= 0;
}

static bool js_get_bool_property(JSContext *ctx,
                                 JSValue obj,
                                 const char *name,
                                 bool *value)
{
    int int_value = 0;
    JSValue property = JS_GetPropertyStr(ctx, obj, name);

    if (JS_IsException(property) || JS_ToInt32(ctx, &int_value, property) != 0) {
        return false;
    }
    *value = int_value != 0;
    return true;
}

static bool js_settle_deferred(JSContext *ctx,
                               JSValue deferred_obj,
                               bool ok,
                               JSValue payload)
{
    bool settled = false;

    if (!js_get_bool_property(ctx, deferred_obj, "settled", &settled)) {
        return false;
    }
    if (settled) {
        return true;
    }

    if (!esp32_mquickjs_set_property(ctx, deferred_obj, "settled", JS_NewBool(true)) ||
        !esp32_mquickjs_set_property(ctx, deferred_obj, "done", JS_NewBool(true)) ||
        !esp32_mquickjs_set_property(ctx, deferred_obj, "ok", JS_NewBool(ok))) {
        return false;
    }

    if (ok) {
        return esp32_mquickjs_set_property(ctx, deferred_obj, "value", payload) &&
               esp32_mquickjs_set_property(ctx, deferred_obj, "error", JS_UNDEFINED);
    }

    return esp32_mquickjs_set_property(ctx, deferred_obj, "error", payload) &&
           esp32_mquickjs_set_property(ctx, deferred_obj, "value", JS_UNDEFINED);
}

static void js_call_best_effort(JSContext *ctx, JSValue func)
{
    JSValue ret;

    if (!JS_IsFunction(ctx, func) || JS_StackCheck(ctx, 2)) {
        return;
    }

    JS_PushArg(ctx, func);
    JS_PushArg(ctx, JS_NULL);
    ret = JS_Call(ctx, 0);
    if (JS_IsException(ret)) {
        esp32_mquickjs_print_exception(ctx);
    }
}

static bool js_is_deferred(JSContext *ctx, JSValue value)
{
    return js_is_object(ctx, value) && JS_GetClassID(ctx, value) == JS_CLASS_DEFERRED;
}

static JSValue js_bind_method(JSContext *ctx, JSValue target_obj, const char *method_name)
{
    JSGCRef method_ref;
    JSGCRef bind_ref;
    JSValue *method_fn;
    JSValue *bind_fn;
    JSValue bind_args[1];
    JSValue result = JS_EXCEPTION;

    method_fn = JS_PushGCRef(ctx, &method_ref);
    bind_fn = JS_PushGCRef(ctx, &bind_ref);
    *method_fn = JS_GetPropertyStr(ctx, target_obj, method_name);
    *bind_fn = JS_UNDEFINED;

    if (JS_IsException(*method_fn)) {
        goto done;
    }
    if (!JS_IsFunction(ctx, *method_fn)) {
        JS_ThrowInternalError(ctx, "%s is not a function", method_name);
        goto done;
    }

    *bind_fn = JS_GetPropertyStr(ctx, *method_fn, "bind");
    if (JS_IsException(*bind_fn)) {
        goto done;
    }
    if (!JS_IsFunction(ctx, *bind_fn)) {
        JS_ThrowInternalError(ctx, "Function.bind() is not available");
        goto done;
    }

    bind_args[0] = target_obj;
    result = js_call_function(ctx, *bind_fn, *method_fn, 1, bind_args);

done:
    JS_PopGCRef(ctx, &bind_ref);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

static JSValue js_wait_for_deferred(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime,
                                    JSValue deferred_obj,
                                    JSValue timeout_value,
                                    const char *api_name)
{
    uint32_t timeout_ms = runtime != NULL ? runtime->eval_timeout_ms : ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS;
    uint64_t saved_deadline_us = 0;
    uint64_t wait_deadline_us = 0;

    if (js_timeout_arg(ctx, timeout_value, timeout_ms, &timeout_ms) != 0) {
        return JS_ThrowTypeError(ctx, "%s(..., timeoutMs) expects a non-negative integer", api_name);
    }

    if (runtime != NULL) {
        saved_deadline_us = runtime->deadline_us;
        runtime->deadline_us = timeout_ms > 0
                                   ? esp_timer_get_time() + ((uint64_t)timeout_ms * 1000ULL)
                                   : 0;
    }
    if (timeout_ms > 0) {
        wait_deadline_us = esp_timer_get_time() + ((uint64_t)timeout_ms * 1000ULL);
    }

    for (;;) {
        bool settled = false;
        bool ok = false;

        if (!js_get_bool_property(ctx, deferred_obj, "settled", &settled)) {
            goto exception;
        }
        if (settled) {
            JSValue result = JS_UNDEFINED;

            if (!js_get_bool_property(ctx, deferred_obj, "ok", &ok)) {
                goto exception;
            }
            if (runtime != NULL) {
                runtime->deadline_us = saved_deadline_us;
            }

            result = JS_GetPropertyStr(ctx, deferred_obj, ok ? "value" : "error");
            if (JS_IsException(result)) {
                return result;
            }
            if (ok) {
                return result;
            }
            if (JS_IsUndefined(result)) {
                return JS_ThrowInternalError(ctx, "%s() rejected without an error value", api_name);
            }
            return JS_Throw(ctx, result);
        }

        if (wait_deadline_us > 0 && esp_timer_get_time() >= wait_deadline_us) {
            JSValue cancel = JS_GetPropertyStr(ctx, deferred_obj, "_cancel");

            if (!JS_IsException(cancel)) {
                js_call_best_effort(ctx, cancel);
            }
            if (runtime != NULL) {
                runtime->deadline_us = saved_deadline_us;
            }
            return JS_ThrowInternalError(ctx, "%s() timed out after %" PRIu32 " ms", api_name, timeout_ms);
        }

        if (esp32_mquickjs_poll(ctx, runtime) != ESP32_MQUICKJS_POLL_NONE) {
            continue;
        }

        {
            uint32_t wait_ms = UINT32_MAX;

            if (wait_deadline_us > 0) {
                uint64_t now_us = esp_timer_get_time();

                if (now_us < wait_deadline_us) {
                    uint64_t remaining_us = wait_deadline_us - now_us;
                    wait_ms = (uint32_t)((remaining_us + 999ULL) / 1000ULL);
                    if (wait_ms == 0) {
                        wait_ms = 1;
                    }
                } else {
                    wait_ms = 0;
                }
            }

            esp32_mquickjs_wait_for_activity(runtime, wait_ms);
        }
    }

exception:
    if (runtime != NULL) {
        runtime->deadline_us = saved_deadline_us;
    }
    return JS_EXCEPTION;
}

static JSValue js_make_deferred(JSContext *ctx)
{
    JSGCRef deferred_ref;
    JSValue *deferred_obj;
    JSValue resolve_fn;
    JSValue reject_fn;
    JSValue callback_fn;
    JSValue wait_fn;

    deferred_obj = JS_PushGCRef(ctx, &deferred_ref);
    *deferred_obj = JS_NewObjectClassUser(ctx, JS_CLASS_DEFERRED);
    if (JS_IsException(*deferred_obj)) {
        goto fail;
    }

    resolve_fn = js_bind_method(ctx, *deferred_obj, "resolve");
    reject_fn = js_bind_method(ctx, *deferred_obj, "reject");
    callback_fn = js_bind_method(ctx, *deferred_obj, "callback");
    wait_fn = js_bind_method(ctx, *deferred_obj, "wait");
    if (JS_IsException(resolve_fn) || JS_IsException(reject_fn) || JS_IsException(callback_fn) ||
        JS_IsException(wait_fn)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *deferred_obj, "settled", JS_NewBool(false)) ||
        !esp32_mquickjs_set_property(ctx, *deferred_obj, "done", JS_NewBool(false)) ||
        !esp32_mquickjs_set_property(ctx, *deferred_obj, "ok", JS_NewBool(false)) ||
        !esp32_mquickjs_set_property(ctx, *deferred_obj, "value", JS_UNDEFINED) ||
        !esp32_mquickjs_set_property(ctx, *deferred_obj, "error", JS_UNDEFINED) ||
        !esp32_mquickjs_set_property(ctx, *deferred_obj, "_cancel", JS_UNDEFINED) ||
        !esp32_mquickjs_set_property(ctx, *deferred_obj, "resolve", resolve_fn) ||
        !esp32_mquickjs_set_property(ctx, *deferred_obj, "reject", reject_fn) ||
        !esp32_mquickjs_set_property(ctx, *deferred_obj, "callback", callback_fn) ||
        !esp32_mquickjs_set_property(ctx, *deferred_obj, "wait", wait_fn)) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &deferred_ref);

fail:
    JS_PopGCRef(ctx, &deferred_ref);
    return JS_EXCEPTION;
}

static JSValue js_wait_for(JSContext *ctx, int argc, JSValue *argv)
{
    JSGCRef deferred_ref;
    JSValue *deferred_obj;
    JSValue resolve_fn;
    JSValue reject_fn;
    JSValue start_args[3];
    JSValue start_result;

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "waitFor(start, timeoutMs?) expects a function");
    }

    deferred_obj = JS_PushGCRef(ctx, &deferred_ref);
    *deferred_obj = js_make_deferred(ctx);
    if (JS_IsException(*deferred_obj)) {
        JS_PopGCRef(ctx, &deferred_ref);
        return JS_EXCEPTION;
    }

    resolve_fn = JS_GetPropertyStr(ctx, *deferred_obj, "resolve");
    reject_fn = JS_GetPropertyStr(ctx, *deferred_obj, "reject");
    if (JS_IsException(resolve_fn) || JS_IsException(reject_fn)) {
        JS_PopGCRef(ctx, &deferred_ref);
        return JS_EXCEPTION;
    }

    start_args[0] = resolve_fn;
    start_args[1] = reject_fn;
    start_args[2] = *deferred_obj;
    start_result = js_call_function(ctx, argv[0], JS_NULL, 3, start_args);
    if (JS_IsException(start_result)) {
        JS_PopGCRef(ctx, &deferred_ref);
        return JS_EXCEPTION;
    }
    if (JS_IsFunction(ctx, start_result) &&
        !esp32_mquickjs_set_property(ctx, *deferred_obj, "_cancel", start_result)) {
        JS_PopGCRef(ctx, &deferred_ref);
        return JS_EXCEPTION;
    }

    start_result = js_wait_for_deferred(ctx,
                                        s_active_runtime,
                                        *deferred_obj,
                                        argc >= 2 ? argv[1] : JS_UNDEFINED,
                                        "waitFor");
    JS_PopGCRef(ctx, &deferred_ref);
    return start_result;
}

static JSValue js_deferred_resolve_common(JSContext *ctx,
                                          JSValue deferred_obj,
                                          bool ok,
                                          JSValue payload,
                                          bool return_payload)
{
    if (!js_is_deferred(ctx, deferred_obj)) {
        return JS_ThrowTypeError(ctx, "Deferred method expects a deferred object");
    }
    if (!js_settle_deferred(ctx, deferred_obj, ok, payload)) {
        return JS_EXCEPTION;
    }
    return return_payload ? payload : JS_UNDEFINED;
}

JSValue js_deferred_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "_Deferred cannot be constructed directly");
}

JSValue js_deferred_resolve(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return js_deferred_resolve_common(ctx,
                                      *this_val,
                                      true,
                                      argc >= 1 ? argv[0] : JS_UNDEFINED,
                                      true);
}

JSValue js_deferred_reject(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return js_deferred_resolve_common(ctx,
                                      *this_val,
                                      false,
                                      argc >= 1 ? argv[0] : JS_UNDEFINED,
                                      true);
}

JSValue js_deferred_callback(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    if (!js_is_deferred(ctx, *this_val)) {
        return JS_ThrowTypeError(ctx, "Deferred.callback(data, error?) expects a deferred object");
    }

    if (argc >= 2 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        if (!js_settle_deferred(ctx, *this_val, false, argv[1])) {
            return JS_EXCEPTION;
        }
        return JS_UNDEFINED;
    }
    if (!js_settle_deferred(ctx, *this_val, true, argc >= 1 ? argv[0] : JS_UNDEFINED)) {
        return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
}

JSValue js_deferred_wait(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    if (!js_is_deferred(ctx, *this_val)) {
        return JS_ThrowTypeError(ctx, "Deferred.wait(timeoutMs?) expects a deferred object");
    }
    return js_wait_for_deferred(ctx,
                                s_active_runtime,
                                *this_val,
                                argc >= 1 ? argv[0] : JS_UNDEFINED,
                                "Deferred.wait");
}

esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void)
{
    return s_active_runtime;
}

static esp32_mquickjs_timer_state_t *esp32_mquickjs_timer_state(esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return NULL;
    }
    return runtime->timer_state;
}

static bool esp32_mquickjs_init_timer_state(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_timer_state_t *state;
    esp32_mquickjs_timer_slot_t *slots;
    int i;

    if (runtime == NULL) {
        return false;
    }
    if (runtime->timer_state != NULL) {
        return true;
    }

    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    slots = heap_caps_calloc(ESP32_MQUICKJS_MAX_TIMERS, sizeof(*slots), MALLOC_CAP_8BIT);
    if (state == NULL || slots == NULL) {
        heap_caps_free(state);
        heap_caps_free(slots);
        return false;
    }

    state->queue = xQueueCreate(ESP32_MQUICKJS_TIMER_QUEUE_LEN, sizeof(esp32_mquickjs_timer_event_t));
    if (state->queue == NULL) {
        heap_caps_free(slots);
        heap_caps_free(state);
        return false;
    }

    state->slots = slots;
    for (i = 0; i < ESP32_MQUICKJS_MAX_TIMERS; ++i) {
        slots[i].runtime = runtime;
        slots[i].timer_id = (uint8_t)i;
    }

    runtime->timer_state = state;
    return true;
}

static void esp32_mquickjs_timer_cb(void *arg)
{
    esp32_mquickjs_timer_slot_t *slot = arg;
    esp32_mquickjs_timer_state_t *state;
    esp32_mquickjs_timer_event_t event;

    if (slot == NULL || !slot->allocated || slot->pending) {
        return;
    }

    state = esp32_mquickjs_timer_state(slot->runtime);
    if (state == NULL || state->queue == NULL) {
        return;
    }

    event.timer_id = slot->timer_id;
    event.generation = slot->generation;
    if (xQueueSend(state->queue, &event, 0) == pdTRUE) {
        slot->pending = true;
        esp32_mquickjs_notify_activity(slot->runtime);
    }
}

static JSValue js_value_to_delay_ms(JSContext *ctx, int argc, JSValue *argv, int arg_index)
{
    int delay_ms = 0;

    if (argc > arg_index && JS_ToInt32(ctx, &delay_ms, argv[arg_index]) != 0) {
        return JS_EXCEPTION;
    }
    if (delay_ms < 0) {
        return JS_ThrowRangeError(ctx, "timer delay must be non-negative");
    }
    if (delay_ms == 0) {
        delay_ms = 1;
    }
    return JS_NewInt32(ctx, delay_ms);
}

static void esp32_mquickjs_cancel_timer(JSContext *ctx, esp32_mquickjs_timer_slot_t *slot)
{
    if (slot == NULL || !slot->allocated) {
        return;
    }

    if (slot->handle != NULL) {
        esp_timer_stop(slot->handle);
        esp_timer_delete(slot->handle);
        slot->handle = NULL;
    }

    JS_DeleteGCRef(ctx, &slot->callback);
    slot->allocated = false;
    slot->repeating = false;
    slot->pending = false;
}

static JSValue esp32_mquickjs_create_timer(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime,
                                           JSValue *callback,
                                           JSValue delay_value,
                                           bool repeating)
{
    esp32_mquickjs_timer_state_t *state = esp32_mquickjs_timer_state(runtime);
    esp32_mquickjs_timer_slot_t *slot = NULL;
    esp_timer_create_args_t timer_args = {0};
    JSValue delay_js;
    JSValue *pfunc;
    int delay_ms = 0;
    int i;

    if (!JS_IsFunction(ctx, *callback)) {
        return JS_ThrowTypeError(ctx, "timer callback must be a function");
    }
    if (state == NULL || state->slots == NULL) {
        return JS_ThrowInternalError(ctx, "timer state is not initialized");
    }

    delay_js = js_value_to_delay_ms(ctx, 1, &delay_value, 0);
    if (JS_IsException(delay_js)) {
        return delay_js;
    }
    if (JS_ToInt32(ctx, &delay_ms, delay_js) != 0) {
        return JS_EXCEPTION;
    }

    for (i = 0; i < ESP32_MQUICKJS_MAX_TIMERS; ++i) {
        if (!state->slots[i].allocated) {
            slot = &state->slots[i];
            break;
        }
    }
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "too many timers");
    }

    slot->generation++;
    slot->pending = false;
    slot->repeating = repeating;
    slot->allocated = true;
    pfunc = JS_AddGCRef(ctx, &slot->callback);
    *pfunc = *callback;

    timer_args.callback = esp32_mquickjs_timer_cb;
    timer_args.arg = slot;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = repeating ? "mqjs_interval" : "mqjs_timeout";
    timer_args.skip_unhandled_events = true;

    if (esp_timer_create(&timer_args, &slot->handle) != ESP_OK) {
        JS_DeleteGCRef(ctx, &slot->callback);
        slot->allocated = false;
        slot->repeating = false;
        return JS_ThrowInternalError(ctx, "esp_timer_create() failed");
    }

    if ((repeating ? esp_timer_start_periodic(slot->handle, (uint64_t)delay_ms * 1000ULL)
                   : esp_timer_start_once(slot->handle, (uint64_t)delay_ms * 1000ULL)) != ESP_OK) {
        esp_timer_delete(slot->handle);
        slot->handle = NULL;
        JS_DeleteGCRef(ctx, &slot->callback);
        slot->allocated = false;
        slot->repeating = false;
        return JS_ThrowInternalError(ctx, "failed to start timer");
    }

    return JS_NewInt32(ctx, slot->timer_id);
}

JSContext *esp32_mquickjs_create(void *mem_start,
                                 size_t mem_size,
                                 esp32_mquickjs_runtime_t *runtime,
                                 uint32_t eval_timeout_ms)
{
    JSContext *ctx;

    if (runtime == NULL) {
        return NULL;
    }

    runtime->deadline_us = 0;
    runtime->eval_timeout_ms = eval_timeout_ms;
    runtime->async_generation = 0;
    runtime->output_generation = 0;
    runtime->prepare_output = NULL;
    runtime->prepare_output_opaque = NULL;
    runtime->timer_state = NULL;
    runtime->async_state = NULL;
    if (!esp32_mquickjs_init_async_state(runtime)) {
        return NULL;
    }
    if (!esp32_mquickjs_init_timer_state(runtime)) {
        return NULL;
    }

    ctx = JS_NewContext(mem_start, mem_size, &js_stdlib);
    if (ctx == NULL) {
        return NULL;
    }

    JS_SetContextOpaque(ctx, runtime);
    JS_SetLogFunc(ctx, js_log_write);
    JS_SetInterruptHandler(ctx, js_interrupt_handler);
    JS_SetRandomSeed(ctx, (uint64_t)esp_timer_get_time());
    s_active_runtime = runtime;
    return ctx;
}

void esp32_mquickjs_set_eval_timeout(esp32_mquickjs_runtime_t *runtime,
                                     uint32_t eval_timeout_ms)
{
    if (runtime == NULL) {
        return;
    }
    runtime->eval_timeout_ms = eval_timeout_ms;
}

JSValue esp32_mquickjs_eval(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            const char *source,
                            const char *filename,
                            int eval_flags)
{
    JSValue result;

    if (runtime != NULL && runtime->eval_timeout_ms > 0) {
        runtime->deadline_us = esp_timer_get_time() +
                               ((uint64_t)runtime->eval_timeout_ms * 1000ULL);
    }

    result = JS_Eval(ctx, source, strlen(source), filename, eval_flags);

    if (runtime != NULL) {
        runtime->deadline_us = 0;
    }
    return result;
}

void esp32_mquickjs_print_exception(JSContext *ctx)
{
    JSValue exception = JS_GetException(ctx);

    prepare_console_output();
    JS_PrintValueF(ctx, exception, JS_DUMP_LONG);
    fputc('\n', stdout);
    fflush(stdout);
    note_console_output();
}

JSValue js_help(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;
    prepare_console_output();
    fputs("See docs/api.md, docs/c-api.md, or docs/js-api.md for the API reference.\n", stdout);
    fflush(stdout);
    note_console_output();
    return JS_UNDEFINED;
}

bool esp32_mquickjs_install_globals(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime)
{
    if (ctx == NULL || runtime == NULL) {
        return false;
    }

#if CONFIG_ESP32_MQUICKJS_FEATURE_LEDC
    esp32_mquickjs_init_ledc_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_ADC
    esp32_mquickjs_init_adc_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_DAC
    esp32_mquickjs_init_dac_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C
    esp32_mquickjs_init_i2c_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI
    esp32_mquickjs_init_spi_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (!esp32_mquickjs_init_wifi_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    if (!esp32_mquickjs_init_http_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    if (!esp32_mquickjs_init_http_server_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
    return true;
}

esp32_mquickjs_poll_result_t esp32_mquickjs_poll(JSContext *ctx,
                                                 esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_timer_state_t *state = esp32_mquickjs_timer_state(runtime);
    esp32_mquickjs_timer_event_t event;
    bool core_async_handled = false;
    bool async_handled = false;
    uint32_t output_generation;
    esp32_mquickjs_poll_result_t result = ESP32_MQUICKJS_POLL_NONE;

    if (ctx == NULL || runtime == NULL) {
        return ESP32_MQUICKJS_POLL_NONE;
    }
    output_generation = runtime->output_generation;
    if (state == NULL || state->queue == NULL || state->slots == NULL) {
        async_handled = esp32_mquickjs_poll_registered(ctx, runtime);
        run_pending_external_gc(ctx);
        if (async_handled) {
            runtime->async_generation++;
            result |= ESP32_MQUICKJS_POLL_ASYNC;
        }
        if (runtime->output_generation != output_generation) {
            result |= ESP32_MQUICKJS_POLL_OUTPUT;
        }
        return result;
    }

    while (xQueueReceive(state->queue, &event, 0) == pdTRUE) {
        esp32_mquickjs_timer_slot_t *slot;
        JSValue ret;

        if (event.timer_id >= ESP32_MQUICKJS_MAX_TIMERS) {
            continue;
        }

        slot = &state->slots[event.timer_id];
        if (!slot->allocated || slot->generation != event.generation) {
            continue;
        }

        slot->pending = false;
        core_async_handled = true;

        if (JS_StackCheck(ctx, 2)) {
            prepare_console_output();
            fputs("Timer callback skipped: JS stack overflow\n", stdout);
            fflush(stdout);
            note_console_output();
            continue;
        }

        JS_PushArg(ctx, slot->callback.val);
        JS_PushArg(ctx, JS_NULL);

        if (!slot->repeating) {
            esp32_mquickjs_cancel_timer(ctx, slot);
        }

        ret = JS_Call(ctx, 0);
        if (JS_IsException(ret)) {
            esp32_mquickjs_print_exception(ctx);
        }
    }

    async_handled = esp32_mquickjs_poll_registered(ctx, runtime);
    run_pending_external_gc(ctx);
    if (core_async_handled || async_handled) {
        runtime->async_generation++;
        result |= ESP32_MQUICKJS_POLL_ASYNC;
    }
    if (runtime->output_generation != output_generation) {
        result |= ESP32_MQUICKJS_POLL_OUTPUT;
    }
    return result;
}

JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int i;

    (void)this_val;
    prepare_console_output();
    for (i = 0; i < argc; i++) {
        if (i != 0) {
            fputc(' ', stdout);
        }

        if (JS_IsString(ctx, argv[i])) {
            JSCStringBuf buf;
            size_t len = 0;
            const char *str = JS_ToCStringLen(ctx, &len, argv[i], &buf);

            fwrite(str, 1, len, stdout);
        } else {
            JS_PrintValueF(ctx, argv[i], JS_DUMP_LONG);
        }
    }

    fputc('\n', stdout);
    fflush(stdout);
    note_console_output();
    return JS_UNDEFINED;
}

JSValue js_defer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return js_make_deferred(ctx);
}

JSValue js_waitFor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return js_wait_for(ctx, argc, argv);
}

JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_GC(ctx);
    return JS_UNDEFINED;
}

JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
#if !CONFIG_ESP32_MQUICKJS_FEATURE_FS
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowInternalError(ctx, "load() requires the fs feature");
#else
    JSCStringBuf command_buf;
    const char *command;

    (void)this_val;
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "load(path) expects a script path");
    }

    command = JS_ToCString(ctx, argv[0], &command_buf);

    return esp32_mquickjs_load_from_littlefs(ctx, s_active_runtime, command);
#endif
}

JSValue js_sleep(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int delay_ms;

    (void)this_val;
    if (argc < 1 || JS_ToInt32(ctx, &delay_ms, argv[0]) != 0 || delay_ms < 0) {
        return JS_ThrowTypeError(ctx, "sleep(ms) expects a non-negative integer");
    }
    vTaskDelay(pdMS_TO_TICKS((uint32_t)delay_ms));
    return JS_NewInt32(ctx, delay_ms);
}

JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "setTimeout(fn, ms) expects a function");
    }
    return esp32_mquickjs_create_timer(ctx,
                                       s_active_runtime,
                                       &argv[0],
                                       argc >= 2 ? argv[1] : JS_NewInt32(ctx, 0),
                                       false);
}

JSValue js_setInterval(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "setInterval(fn, ms) expects a function");
    }
    return esp32_mquickjs_create_timer(ctx,
                                       s_active_runtime,
                                       &argv[0],
                                       argc >= 2 ? argv[1] : JS_NewInt32(ctx, 0),
                                       true);
}

JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_timer_state_t *state = esp32_mquickjs_timer_state(s_active_runtime);
    int timer_id;

    (void)this_val;
    if (argc < 1 || JS_ToInt32(ctx, &timer_id, argv[0]) != 0) {
        return JS_ThrowTypeError(ctx, "clearTimeout(id) expects a timer id");
    }
    if (state != NULL && state->slots != NULL && timer_id >= 0 && timer_id < ESP32_MQUICKJS_MAX_TIMERS) {
        esp32_mquickjs_cancel_timer(ctx, &state->slots[timer_id]);
    }
    return JS_UNDEFINED;
}

JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, esp_timer_get_time() / 1000);
}

JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, esp_timer_get_time() / 1000);
}
