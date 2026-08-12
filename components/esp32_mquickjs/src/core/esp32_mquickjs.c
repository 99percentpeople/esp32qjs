#include "esp32_mquickjs_core.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_sys.h"
#include "esp32_mquickjs_adc.h"
#include "esp32_mquickjs_dac.h"
#include "esp32_mquickjs_fs.h"
#include "esp32_mquickjs_gpio.h"
#include "esp32_mquickjs_http.h"
#include "esp32_mquickjs_http_server.h"
#include "esp32_mquickjs_i2c.h"
#include "esp32_mquickjs_ledc.h"
#include "esp32_mquickjs_nvs.h"
#include "esp32_mquickjs_spi.h"
#include "esp32_mquickjs_stream.h"
#include "esp32_mquickjs_socket.h"
#include "esp32_mquickjs_uart.h"
#include "esp32_mquickjs_usb_serial.h"
#include "esp32_mquickjs_websocket.h"
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
#define ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS 6U
#define ESP32_MQUICKJS_TIMER_HANDLE_ID_MASK ((1U << ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS) - 1U)
#define ESP32_MQUICKJS_TIMER_HANDLE_MAX \
    (((uint64_t)UINT32_MAX << ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS) | \
     ESP32_MQUICKJS_TIMER_HANDLE_ID_MASK)

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

void esp32_mquickjs_detach_current_task(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state != NULL) {
        state->task_handle = NULL;
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

void esp32_mquickjs_set_cooperate_hook(esp32_mquickjs_runtime_t *runtime,
                                       esp32_mquickjs_cooperate_fn cooperate,
                                       void *opaque)
{
    if (runtime == NULL) {
        return;
    }
    runtime->cooperate = cooperate;
    runtime->cooperate_opaque = opaque;
}

bool esp32_mquickjs_cooperate(esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return true;
    }
    if (runtime->scoped_deadline_us > 0 &&
        (uint64_t)esp_timer_get_time() >= runtime->scoped_deadline_us) {
        return false;
    }
    return runtime->cooperate == NULL || runtime->cooperate(runtime->cooperate_opaque);
}

void esp32_mquickjs_native_wait_begin(esp32_mquickjs_runtime_t *runtime,
                                      esp32_mquickjs_native_wait_t *wait)
{
    if (wait == NULL) {
        return;
    }
    wait->saved_deadline_us = runtime != NULL ? runtime->deadline_us : 0;
    wait->started_us = (uint64_t)esp_timer_get_time();
    if (runtime != NULL) {
        runtime->deadline_us = 0;
    }
}

void esp32_mquickjs_native_wait_end(esp32_mquickjs_runtime_t *runtime,
                                    esp32_mquickjs_native_wait_t *wait)
{
    uint64_t elapsed_us;

    if (runtime == NULL || wait == NULL) {
        return;
    }
    elapsed_us = (uint64_t)esp_timer_get_time() - wait->started_us;
    if (wait->saved_deadline_us == 0) {
        runtime->deadline_us = 0;
    } else if (wait->saved_deadline_us > UINT64_MAX - elapsed_us) {
        runtime->deadline_us = UINT64_MAX;
    } else {
        runtime->deadline_us = wait->saved_deadline_us + elapsed_us;
    }
    wait->saved_deadline_us = 0;
    wait->started_us = 0;
}

static uint32_t esp32_mquickjs_bound_wait_slice(
    const esp32_mquickjs_runtime_t *runtime,
    uint32_t slice_ms)
{
    uint64_t now_us;
    uint64_t remaining_us;
    uint32_t remaining_ms;

    if (runtime == NULL || runtime->scoped_deadline_us == 0) {
        return slice_ms;
    }
    now_us = (uint64_t)esp_timer_get_time();
    if (now_us >= runtime->scoped_deadline_us) {
        return 0;
    }
    remaining_us = runtime->scoped_deadline_us - now_us;
    remaining_ms = (uint32_t)((remaining_us + 999ULL) / 1000ULL);
    return remaining_ms < slice_ms ? remaining_ms : slice_ms;
}

bool esp32_mquickjs_cooperative_delay(esp32_mquickjs_runtime_t *runtime,
                                      uint32_t delay_ms)
{
    esp32_mquickjs_native_wait_t wait;
    uint32_t remaining_ms = delay_ms;
    bool completed = true;

    esp32_mquickjs_native_wait_begin(runtime, &wait);
    do {
        uint32_t slice_ms;
        TickType_t wait_ticks;

        if (!esp32_mquickjs_cooperate(runtime)) {
            completed = false;
            break;
        }
        if (remaining_ms == 0) {
            break;
        }
        slice_ms = remaining_ms > ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS
                       ? ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS
                       : remaining_ms;
        slice_ms = esp32_mquickjs_bound_wait_slice(runtime, slice_ms);
        if (slice_ms == 0) {
            completed = false;
            break;
        }
        wait_ticks = pdMS_TO_TICKS(slice_ms);
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }
        vTaskDelay(wait_ticks);
        remaining_ms -= slice_ms;
    } while (remaining_ms > 0);

    if (completed && !esp32_mquickjs_cooperate(runtime)) {
        completed = false;
    }
    esp32_mquickjs_native_wait_end(runtime, &wait);
    return completed;
}

bool esp32_mquickjs_wait_for_activity(esp32_mquickjs_runtime_t *runtime,
                                      uint32_t timeout_ms)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);
    uint32_t remaining_ms = timeout_ms;

    for (;;) {
        uint32_t slice_ms;
        TickType_t wait_ticks;
        bool notified;

        if (!esp32_mquickjs_cooperate(runtime)) {
            return false;
        }
        if (timeout_ms == UINT32_MAX) {
            slice_ms = ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS;
        } else {
            slice_ms = remaining_ms > ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS
                           ? ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS
                           : remaining_ms;
        }
        slice_ms = esp32_mquickjs_bound_wait_slice(runtime, slice_ms);
        if (slice_ms == 0) {
            return false;
        }
        wait_ticks = pdMS_TO_TICKS(slice_ms);
        if (slice_ms > 0 && wait_ticks == 0) {
            wait_ticks = 1;
        }

        if (state == NULL || state->task_handle == NULL) {
            if (wait_ticks > 0) {
                vTaskDelay(wait_ticks);
            }
            notified = false;
        } else {
            notified = ulTaskNotifyTake(pdTRUE, wait_ticks) > 0;
        }
        if (!esp32_mquickjs_cooperate(runtime)) {
            return false;
        }
        if (notified) {
            return true;
        }
        if (timeout_ms == UINT32_MAX) {
            continue;
        }
        if (remaining_ms <= slice_ms) {
            return false;
        }
        remaining_ms -= slice_ms;
    }
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
    if (!esp32_mquickjs_cooperate(runtime)) {
        return 1;
    }
    return esp_timer_get_time() > runtime->deadline_us;
}

JSValue esp32_mquickjs_call(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            JSValue function,
                            JSValue this_value,
                            int argc,
                            JSValue *argv)
{
    uint64_t previous_deadline = runtime != NULL ? runtime->deadline_us : 0;
    JSGCRef function_ref;
    JSGCRef this_ref;
    JSGCRef *argument_refs = NULL;
    JSValue *rooted_function;
    JSValue *rooted_this;
    JSValue result = JS_EXCEPTION;
    int i;

    if (ctx == NULL || argc < 0 || (argc > 0 && argv == NULL)) {
        return JS_EXCEPTION;
    }
    if (argc > 0) {
        argument_refs = heap_caps_calloc((size_t)argc,
                                         sizeof(*argument_refs),
                                         MALLOC_CAP_8BIT);
        if (argument_refs == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }
    }

    rooted_function = JS_PushGCRef(ctx, &function_ref);
    rooted_this = JS_PushGCRef(ctx, &this_ref);
    *rooted_function = function;
    *rooted_this = this_value;
    for (i = 0; i < argc; ++i) {
        JSValue *rooted_argument = JS_PushGCRef(ctx, &argument_refs[i]);

        *rooted_argument = argv[i];
    }

    if (JS_StackCheck(ctx, (uint32_t)(argc + 2))) {
        goto done;
    }
    if (runtime != NULL && runtime->eval_timeout_ms > 0) {
        uint64_t call_deadline = esp_timer_get_time() +
                                 ((uint64_t)runtime->eval_timeout_ms * 1000ULL);

        if (previous_deadline == 0 || call_deadline < previous_deadline) {
            runtime->deadline_us = call_deadline;
        }
    }

    for (i = argc - 1; i >= 0; --i) {
        JS_PushArg(ctx, argument_refs[i].val);
    }
    JS_PushArg(ctx, *rooted_function);
    JS_PushArg(ctx, *rooted_this);
    result = JS_Call(ctx, argc);

    if (runtime != NULL) {
        runtime->deadline_us = previous_deadline;
    }

done:
    for (i = argc - 1; i >= 0; --i) {
        JS_PopGCRef(ctx, &argument_refs[i]);
    }
    JS_PopGCRef(ctx, &this_ref);
    JS_PopGCRef(ctx, &function_ref);
    heap_caps_free(argument_refs);
    return result;
}

bool esp32_mquickjs_set_property(JSContext *ctx,
                                 JSValue target_obj,
                                 const char *name,
                                 JSValue value)
{
    return !JS_IsException(JS_SetPropertyStr(ctx, target_obj, name, value));
}

bool esp32_mquickjs_set_property_ref(JSContext *ctx,
                                     JSValue *target_obj,
                                     const char *name,
                                     JSValue value)
{
    return target_obj != NULL &&
           !JS_IsException(JS_SetPropertyStr(ctx, *target_obj, name, value));
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
    JSGCRef deferred_ref;
    JSGCRef payload_ref;
    JSValue *rooted_deferred;
    JSValue *rooted_payload;
    bool settled = false;
    bool result = false;

    rooted_deferred = JS_PushGCRef(ctx, &deferred_ref);
    rooted_payload = JS_PushGCRef(ctx, &payload_ref);
    *rooted_deferred = deferred_obj;
    *rooted_payload = payload;

    if (!js_get_bool_property(ctx, *rooted_deferred, "settled", &settled)) {
        goto done;
    }
    if (settled) {
        result = true;
        goto done;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, rooted_deferred, "settled", JS_NewBool(true)) ||
        !esp32_mquickjs_set_property_ref(ctx, rooted_deferred, "done", JS_NewBool(true)) ||
        !esp32_mquickjs_set_property_ref(ctx, rooted_deferred, "ok", JS_NewBool(ok))) {
        goto done;
    }

    if (ok) {
        result = esp32_mquickjs_set_property_ref(ctx, rooted_deferred, "value", *rooted_payload) &&
                 esp32_mquickjs_set_property_ref(ctx, rooted_deferred, "error", JS_UNDEFINED);
    } else {
        result = esp32_mquickjs_set_property_ref(ctx, rooted_deferred, "error", *rooted_payload) &&
                 esp32_mquickjs_set_property_ref(ctx, rooted_deferred, "value", JS_UNDEFINED);
    }

done:
    JS_PopGCRef(ctx, &payload_ref);
    JS_PopGCRef(ctx, &deferred_ref);
    return result;
}

static void js_call_best_effort(JSContext *ctx, JSValue func)
{
    JSValue ret;

    if (!JS_IsFunction(ctx, func)) {
        return;
    }

    ret = esp32_mquickjs_call(ctx, s_active_runtime, func, JS_NULL, 0, NULL);
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
    JSGCRef target_ref;
    JSGCRef method_ref;
    JSGCRef bind_ref;
    JSValue *rooted_target;
    JSValue *method_fn;
    JSValue *bind_fn;
    JSValue result = JS_EXCEPTION;

    rooted_target = JS_PushGCRef(ctx, &target_ref);
    method_fn = JS_PushGCRef(ctx, &method_ref);
    bind_fn = JS_PushGCRef(ctx, &bind_ref);
    *rooted_target = target_obj;
    *method_fn = JS_GetPropertyStr(ctx, *rooted_target, method_name);
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

    result = esp32_mquickjs_call(ctx, s_active_runtime, *bind_fn, *method_fn, 1, rooted_target);

done:
    JS_PopGCRef(ctx, &bind_ref);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &target_ref);
    return result;
}

static JSValue js_wait_for_deferred(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime,
                                    JSValue deferred_obj,
                                    JSValue timeout_value,
                                    const char *api_name)
{
    JSGCRef result_ref;
    JSGCRef deferred_ref;
    JSGCRef timeout_ref;
    JSValue *result;
    JSValue *rooted_deferred;
    JSValue *rooted_timeout;
    uint32_t timeout_ms = runtime != NULL ? runtime->eval_timeout_ms : ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS;
    uint64_t saved_deadline_us = 0;
    uint64_t wait_deadline_us = 0;
    bool deadline_changed = false;

    result = JS_PushGCRef(ctx, &result_ref);
    rooted_deferred = JS_PushGCRef(ctx, &deferred_ref);
    rooted_timeout = JS_PushGCRef(ctx, &timeout_ref);
    *result = JS_EXCEPTION;
    *rooted_deferred = deferred_obj;
    *rooted_timeout = timeout_value;

    if (js_timeout_arg(ctx, *rooted_timeout, timeout_ms, &timeout_ms) != 0) {
        *result = JS_ThrowTypeError(ctx, "%s(..., timeoutMs) expects a non-negative integer", api_name);
        goto done;
    }

    if (runtime != NULL) {
        saved_deadline_us = runtime->deadline_us;
        runtime->deadline_us = timeout_ms > 0
                                   ? esp_timer_get_time() + ((uint64_t)timeout_ms * 1000ULL)
                                   : 0;
        deadline_changed = true;
    }
    if (timeout_ms > 0) {
        wait_deadline_us = esp_timer_get_time() + ((uint64_t)timeout_ms * 1000ULL);
    }

    for (;;) {
        bool settled = false;
        bool ok = false;

        if (!js_get_bool_property(ctx, *rooted_deferred, "settled", &settled)) {
            goto done;
        }
        if (settled) {
            if (!js_get_bool_property(ctx, *rooted_deferred, "ok", &ok)) {
                goto done;
            }

            *result = JS_GetPropertyStr(ctx, *rooted_deferred, ok ? "value" : "error");
            if (JS_IsException(*result) || ok) {
                goto done;
            }
            if (JS_IsUndefined(*result)) {
                *result = JS_ThrowInternalError(ctx, "%s() rejected without an error value", api_name);
            } else {
                *result = JS_Throw(ctx, *result);
            }
            goto done;
        }

        if (wait_deadline_us > 0 && esp_timer_get_time() >= wait_deadline_us) {
            JSGCRef cancel_ref;
            JSValue *cancel = JS_PushGCRef(ctx, &cancel_ref);

            *cancel = JS_GetPropertyStr(ctx, *rooted_deferred, "_cancel");
            if (!JS_IsException(*cancel)) {
                js_call_best_effort(ctx, *cancel);
            }
            JS_PopGCRef(ctx, &cancel_ref);
            *result = JS_ThrowInternalError(ctx, "%s() timed out after %" PRIu32 " ms", api_name, timeout_ms);
            goto done;
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

            if (!esp32_mquickjs_wait_for_activity(runtime, wait_ms) &&
                !esp32_mquickjs_cooperate(runtime)) {
                *result = JS_ThrowInternalError(ctx, "%s() was interrupted by a runtime stop request", api_name);
                goto done;
            }
        }
    }

done:
    if (runtime != NULL && deadline_changed) {
        runtime->deadline_us = saved_deadline_us;
    }
    JS_PopGCRef(ctx, &timeout_ref);
    JS_PopGCRef(ctx, &deferred_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

static JSValue js_make_deferred(JSContext *ctx)
{
    JSGCRef deferred_ref;
    JSGCRef resolve_ref;
    JSGCRef reject_ref;
    JSGCRef callback_ref;
    JSGCRef wait_ref;
    JSValue *deferred_obj;
    JSValue *resolve_fn;
    JSValue *reject_fn;
    JSValue *callback_fn;
    JSValue *wait_fn;

    deferred_obj = JS_PushGCRef(ctx, &deferred_ref);
    resolve_fn = JS_PushGCRef(ctx, &resolve_ref);
    reject_fn = JS_PushGCRef(ctx, &reject_ref);
    callback_fn = JS_PushGCRef(ctx, &callback_ref);
    wait_fn = JS_PushGCRef(ctx, &wait_ref);
    *deferred_obj = JS_NewObjectClassUser(ctx, JS_CLASS_DEFERRED);
    *resolve_fn = JS_UNDEFINED;
    *reject_fn = JS_UNDEFINED;
    *callback_fn = JS_UNDEFINED;
    *wait_fn = JS_UNDEFINED;
    if (JS_IsException(*deferred_obj)) {
        goto fail;
    }

    *resolve_fn = js_bind_method(ctx, *deferred_obj, "resolve");
    *reject_fn = js_bind_method(ctx, *deferred_obj, "reject");
    *callback_fn = js_bind_method(ctx, *deferred_obj, "callback");
    *wait_fn = js_bind_method(ctx, *deferred_obj, "wait");
    if (JS_IsException(*resolve_fn) || JS_IsException(*reject_fn) || JS_IsException(*callback_fn) ||
        JS_IsException(*wait_fn)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, deferred_obj, "settled", JS_NewBool(false)) ||
        !esp32_mquickjs_set_property_ref(ctx, deferred_obj, "done", JS_NewBool(false)) ||
        !esp32_mquickjs_set_property_ref(ctx, deferred_obj, "ok", JS_NewBool(false)) ||
        !esp32_mquickjs_set_property_ref(ctx, deferred_obj, "value", JS_UNDEFINED) ||
        !esp32_mquickjs_set_property_ref(ctx, deferred_obj, "error", JS_UNDEFINED) ||
        !esp32_mquickjs_set_property_ref(ctx, deferred_obj, "_cancel", JS_UNDEFINED) ||
        !esp32_mquickjs_set_property_ref(ctx, deferred_obj, "resolve", *resolve_fn) ||
        !esp32_mquickjs_set_property_ref(ctx, deferred_obj, "reject", *reject_fn) ||
        !esp32_mquickjs_set_property_ref(ctx, deferred_obj, "callback", *callback_fn) ||
        !esp32_mquickjs_set_property_ref(ctx, deferred_obj, "wait", *wait_fn)) {
        goto fail;
    }

    JS_PopGCRef(ctx, &wait_ref);
    JS_PopGCRef(ctx, &callback_ref);
    JS_PopGCRef(ctx, &reject_ref);
    JS_PopGCRef(ctx, &resolve_ref);
    return JS_PopGCRef(ctx, &deferred_ref);

fail:
    JS_PopGCRef(ctx, &wait_ref);
    JS_PopGCRef(ctx, &callback_ref);
    JS_PopGCRef(ctx, &reject_ref);
    JS_PopGCRef(ctx, &resolve_ref);
    JS_PopGCRef(ctx, &deferred_ref);
    return JS_EXCEPTION;
}

static JSValue js_wait_for(JSContext *ctx, int argc, JSValue *argv)
{
    JSGCRef result_ref;
    JSGCRef deferred_ref;
    JSGCRef resolve_ref;
    JSGCRef reject_ref;
    JSValue *start_result;
    JSValue *deferred_obj;
    JSValue *resolve_fn;
    JSValue *reject_fn;
    JSValue start_args[3];

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "waitFor(start, timeoutMs?) expects a function");
    }

    start_result = JS_PushGCRef(ctx, &result_ref);
    deferred_obj = JS_PushGCRef(ctx, &deferred_ref);
    resolve_fn = JS_PushGCRef(ctx, &resolve_ref);
    reject_fn = JS_PushGCRef(ctx, &reject_ref);
    *start_result = JS_EXCEPTION;
    *deferred_obj = js_make_deferred(ctx);
    *resolve_fn = JS_UNDEFINED;
    *reject_fn = JS_UNDEFINED;
    if (JS_IsException(*deferred_obj)) {
        goto done;
    }

    *resolve_fn = JS_GetPropertyStr(ctx, *deferred_obj, "resolve");
    *reject_fn = JS_GetPropertyStr(ctx, *deferred_obj, "reject");
    if (JS_IsException(*resolve_fn) || JS_IsException(*reject_fn)) {
        goto done;
    }

    start_args[0] = *resolve_fn;
    start_args[1] = *reject_fn;
    start_args[2] = *deferred_obj;
    *start_result = esp32_mquickjs_call(ctx, s_active_runtime, argv[0], JS_NULL, 3, start_args);
    if (JS_IsException(*start_result)) {
        goto done;
    }
    if (JS_IsFunction(ctx, *start_result) &&
        !esp32_mquickjs_set_property_ref(ctx, deferred_obj, "_cancel", *start_result)) {
        *start_result = JS_EXCEPTION;
        goto done;
    }

    *start_result = js_wait_for_deferred(ctx,
                                         s_active_runtime,
                                         *deferred_obj,
                                         argc >= 2 ? argv[1] : JS_UNDEFINED,
                                         "waitFor");

done:
    JS_PopGCRef(ctx, &reject_ref);
    JS_PopGCRef(ctx, &resolve_ref);
    JS_PopGCRef(ctx, &deferred_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

static JSValue js_deferred_resolve_common(JSContext *ctx,
                                          JSValue deferred_obj,
                                          bool ok,
                                          JSValue payload,
                                          bool return_payload)
{
    JSGCRef deferred_ref;
    JSGCRef payload_ref;
    JSValue *rooted_deferred;
    JSValue *rooted_payload;
    JSValue result;

    rooted_deferred = JS_PushGCRef(ctx, &deferred_ref);
    rooted_payload = JS_PushGCRef(ctx, &payload_ref);
    *rooted_deferred = deferred_obj;
    *rooted_payload = payload;

    if (!js_is_deferred(ctx, *rooted_deferred)) {
        result = JS_ThrowTypeError(ctx, "Deferred method expects a deferred object");
    } else if (!js_settle_deferred(ctx, *rooted_deferred, ok, *rooted_payload)) {
        result = JS_EXCEPTION;
    } else {
        result = return_payload ? *rooted_payload : JS_UNDEFINED;
    }

    JS_PopGCRef(ctx, &payload_ref);
    JS_PopGCRef(ctx, &deferred_ref);
    return result;
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

static JSValue js_value_to_delay_ms(JSContext *ctx,
                                     int argc,
                                     JSValue *argv,
                                     int arg_index,
                                     bool repeating)
{
    int delay_ms = 0;

    if (argc > arg_index && JS_ToInt32(ctx, &delay_ms, argv[arg_index]) != 0) {
        return JS_EXCEPTION;
    }
    if (delay_ms < 0) {
        return JS_ThrowRangeError(ctx, "timer delay must be non-negative");
    }
    if (repeating && delay_ms < CONFIG_ESP32_MQUICKJS_MIN_INTERVAL_MS) {
        delay_ms = CONFIG_ESP32_MQUICKJS_MIN_INTERVAL_MS;
    } else if (delay_ms == 0) {
        delay_ms = 1;
    }
    return JS_NewInt32(ctx, delay_ms);
}

static uint64_t esp32_mquickjs_timer_handle(const esp32_mquickjs_timer_slot_t *slot)
{
    return ((uint64_t)slot->generation << ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS) |
           (uint64_t)slot->timer_id;
}

static bool esp32_mquickjs_decode_timer_handle(JSContext *ctx,
                                                JSValue value,
                                                uint8_t *out_timer_id,
                                                uint32_t *out_generation)
{
    double raw_handle;
    uint64_t handle;

    if (JS_ToNumber(ctx, &raw_handle, value) != 0 ||
        !(raw_handle > 0) ||
        raw_handle > (double)ESP32_MQUICKJS_TIMER_HANDLE_MAX) {
        return false;
    }
    handle = (uint64_t)raw_handle;
    if ((double)handle != raw_handle) {
        return false;
    }
    *out_timer_id = (uint8_t)(handle & ESP32_MQUICKJS_TIMER_HANDLE_ID_MASK);
    *out_generation = (uint32_t)(handle >> ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS);
    return *out_generation != 0 &&
           *out_timer_id < ESP32_MQUICKJS_MAX_TIMERS &&
           handle == (((uint64_t)*out_generation << ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS) |
                      (uint64_t)*out_timer_id);
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

static void esp32_mquickjs_deinit_timer_state(JSContext *ctx,
                                               esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_timer_state_t *state = esp32_mquickjs_timer_state(runtime);
    int i;

    if (state == NULL) {
        return;
    }
    if (state->slots != NULL) {
        for (i = 0; i < ESP32_MQUICKJS_MAX_TIMERS; ++i) {
            esp32_mquickjs_timer_slot_t *slot = &state->slots[i];

            if (slot->allocated && ctx != NULL) {
                esp32_mquickjs_cancel_timer(ctx, slot);
            } else if (slot->handle != NULL) {
                esp_timer_stop(slot->handle);
                esp_timer_delete(slot->handle);
                slot->handle = NULL;
            }
        }
    }
    if (state->queue != NULL) {
        vQueueDelete(state->queue);
    }
    heap_caps_free(state->slots);
    heap_caps_free(state);
    runtime->timer_state = NULL;
}

static void esp32_mquickjs_deinit_async_state(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state == NULL) {
        return;
    }
    heap_caps_free(state->pollers);
    heap_caps_free(state);
    runtime->async_state = NULL;
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

    delay_js = js_value_to_delay_ms(ctx, 1, &delay_value, 0, repeating);
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
    if (slot->generation == 0) {
        slot->generation++;
    }
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

    return JS_NewInt64(ctx, (int64_t)esp32_mquickjs_timer_handle(slot));
}

JSContext *esp32_mquickjs_create(void *mem_start,
                                 size_t mem_size,
                                 esp32_mquickjs_runtime_t *runtime,
                                 uint32_t eval_timeout_ms)
{
    JSContext *ctx;

    if (runtime == NULL || s_active_runtime != NULL) {
        return NULL;
    }

    runtime->deadline_us = 0;
    runtime->eval_timeout_ms = eval_timeout_ms;
    runtime->async_generation = 0;
    runtime->output_generation = 0;
    runtime->littlefs_mounted = false;
    runtime->scoped_deadline_us = 0;
    runtime->load_root_depth = 0;
    snprintf(runtime->fs_root,
             sizeof(runtime->fs_root),
             "%s",
             ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    runtime->load_root[0] = '\0';
    runtime->repl_enabled = false;
    runtime->auto_run_startup_script = false;
    runtime->format_littlefs_on_mount_fail = false;
#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    runtime->repl_enabled = true;
#endif
#ifdef CONFIG_ESP32QJS_AUTORUN_INDEX_JS
    runtime->auto_run_startup_script = true;
#endif
#ifdef CONFIG_ESP32QJS_LITTLEFS_FORMAT_ON_MOUNT_FAIL
    runtime->format_littlefs_on_mount_fail = true;
#endif
    runtime->prepare_output = NULL;
    runtime->prepare_output_opaque = NULL;
    runtime->cooperate = NULL;
    runtime->cooperate_opaque = NULL;
    runtime->timer_state = NULL;
    runtime->async_state = NULL;
    if (!esp32_mquickjs_init_async_state(runtime)) {
        return NULL;
    }
    if (!esp32_mquickjs_init_timer_state(runtime)) {
        esp32_mquickjs_deinit_async_state(runtime);
        return NULL;
    }

    ctx = JS_NewContext(mem_start, mem_size, &js_stdlib);
    if (ctx == NULL) {
        esp32_mquickjs_deinit_timer_state(NULL, runtime);
        esp32_mquickjs_deinit_async_state(runtime);
        return NULL;
    }

    JS_SetContextOpaque(ctx, runtime);
    JS_SetLogFunc(ctx, js_log_write);
    JS_SetInterruptHandler(ctx, js_interrupt_handler);
    JS_SetRandomSeed(ctx, (uint64_t)esp_timer_get_time());
    s_active_runtime = runtime;
    return ctx;
}

bool esp32_mquickjs_destroy(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return false;
    }

#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
    esp32_mquickjs_deinit_socket_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
    esp32_mquickjs_deinit_websocket_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    if (!esp32_mquickjs_deinit_http_runtime(ctx)) {
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL
    esp32_mquickjs_deinit_usb_serial_runtime(ctx);
#endif
    if (s_active_runtime == runtime) {
        s_active_runtime = NULL;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    esp32_mquickjs_deinit_http_server_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    esp32_mquickjs_deinit_wifi_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_GPIO
    esp32_mquickjs_deinit_gpio_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_ADC
    esp32_mquickjs_deinit_adc_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_DAC
    esp32_mquickjs_deinit_dac_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_LEDC
    esp32_mquickjs_deinit_ledc_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C
    esp32_mquickjs_deinit_i2c_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI
    esp32_mquickjs_deinit_spi_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_UART
    esp32_mquickjs_deinit_uart_runtime();
#endif

    esp32_mquickjs_deinit_timer_state(ctx, runtime);
    if (ctx != NULL) {
        JS_FreeContext(ctx);
    }
    esp32_mquickjs_deinit_async_state(runtime);
    runtime->deadline_us = 0;
    runtime->scoped_deadline_us = 0;
    runtime->littlefs_mounted = false;
    runtime->load_root_depth = 0;
    runtime->fs_root[0] = '\0';
    runtime->load_root[0] = '\0';
    runtime->repl_enabled = false;
    runtime->auto_run_startup_script = false;
    runtime->format_littlefs_on_mount_fail = false;
    runtime->prepare_output = NULL;
    runtime->prepare_output_opaque = NULL;
    runtime->cooperate = NULL;
    runtime->cooperate_opaque = NULL;
    return true;
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
    uint64_t previous_deadline = runtime != NULL ? runtime->deadline_us : 0;
    JSValue result;

    if (runtime != NULL && runtime->eval_timeout_ms > 0) {
        uint64_t eval_deadline = esp_timer_get_time() +
                                 ((uint64_t)runtime->eval_timeout_ms * 1000ULL);

        if (previous_deadline == 0 || eval_deadline < previous_deadline) {
            runtime->deadline_us = eval_deadline;
        }
    }

    result = JS_Eval(ctx, source, strlen(source), filename, eval_flags);

    if (runtime != NULL) {
        runtime->deadline_us = previous_deadline;
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

    if (!esp32_mquickjs_init_secure_random(ctx)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_NVS
    if (!esp32_mquickjs_init_nvs_runtime(ctx)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
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
#if CONFIG_ESP32_MQUICKJS_FEATURE_UART
    esp32_mquickjs_init_uart_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL
    if (!esp32_mquickjs_init_usb_serial_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (!esp32_mquickjs_init_wifi_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
    if (!esp32_mquickjs_init_socket_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
    if (!esp32_mquickjs_init_websocket_runtime(ctx, runtime)) {
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
        JSGCRef callback_ref;
        JSValue *callback;
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
            esp32_mquickjs_cancel_timer(ctx, slot);
            continue;
        }

        callback = JS_PushGCRef(ctx, &callback_ref);
        *callback = slot->callback.val;
        if (!slot->repeating) {
            esp32_mquickjs_cancel_timer(ctx, slot);
        }

        ret = esp32_mquickjs_call(ctx, runtime, *callback, JS_NULL, 0, NULL);
        if (JS_IsException(ret)) {
            esp32_mquickjs_print_exception(ctx);
            if (slot->allocated && slot->generation == event.generation && slot->repeating) {
                esp32_mquickjs_cancel_timer(ctx, slot);
            }
        }
        JS_PopGCRef(ctx, &callback_ref);
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

    return esp32_mquickjs_load_from_active_fs(ctx, s_active_runtime, command);
#endif
}

JSValue js_sleep(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int delay_ms;

    (void)this_val;
    if (argc < 1 || JS_ToInt32(ctx, &delay_ms, argv[0]) != 0 || delay_ms < 0) {
        return JS_ThrowTypeError(ctx, "sleep(ms) expects a non-negative integer");
    }
    if (!esp32_mquickjs_cooperative_delay(s_active_runtime, (uint32_t)delay_ms)) {
        return JS_ThrowInternalError(ctx, "sleep(ms) was interrupted by a runtime stop request");
    }
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
    uint8_t timer_id;
    uint32_t generation;

    (void)this_val;
    if (argc < 1 || !esp32_mquickjs_decode_timer_handle(ctx, argv[0], &timer_id, &generation)) {
        return JS_ThrowTypeError(ctx, "clearTimeout(handle) expects a timer handle");
    }
    if (state != NULL && state->slots != NULL) {
        esp32_mquickjs_timer_slot_t *slot = &state->slots[timer_id];

        if (slot->allocated && slot->generation == generation) {
            esp32_mquickjs_cancel_timer(ctx, slot);
        }
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
