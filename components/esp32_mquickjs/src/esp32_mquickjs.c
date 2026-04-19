#include "esp32_mquickjs_internal.h"

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

extern const JSSTDLibraryDef js_stdlib;

static const char *TAG = "esp32qjs";

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

static void note_console_output(void)
{
    if (s_active_runtime != NULL) {
        s_active_runtime->output_generation++;
    }
}

static void prepare_console_output(void)
{
    if (s_active_runtime != NULL && s_active_runtime->before_output != NULL) {
        s_active_runtime->prompt_needs_redraw = true;
        s_active_runtime->before_output(s_active_runtime->before_output_opaque);
    }
}

static void begin_async_console_output(uint32_t lines)
{
    if (s_active_runtime != NULL && s_active_runtime->before_async_output != NULL) {
        s_active_runtime->before_async_output(s_active_runtime->before_async_output_opaque, lines);
    }
}

static void end_async_console_output(void)
{
    if (s_active_runtime != NULL && s_active_runtime->after_async_output != NULL) {
        s_active_runtime->after_async_output(s_active_runtime->after_async_output_opaque);
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

static JSValue esp32_mquickjs_make_bound_bridge_function(JSContext *ctx,
                                                         JSValue global_obj,
                                                         const char *operation)
{
    JSGCRef load_ref;
    JSGCRef bind_ref;
    JSGCRef namespace_ref;
    JSGCRef operation_ref;
    JSValue *load_fn;
    JSValue *bind_fn;
    JSValue *bridge_namespace;
    JSValue *bridge_operation;
    JSValue bind_args[3];
    JSValue result = JS_EXCEPTION;

    load_fn = JS_PushGCRef(ctx, &load_ref);
    bind_fn = JS_PushGCRef(ctx, &bind_ref);
    bridge_namespace = JS_PushGCRef(ctx, &namespace_ref);
    bridge_operation = JS_PushGCRef(ctx, &operation_ref);

    *load_fn = JS_GetPropertyStr(ctx, global_obj, "load");
    *bind_fn = JS_UNDEFINED;
    *bridge_namespace = JS_UNDEFINED;
    *bridge_operation = JS_UNDEFINED;

    if (JS_IsException(*load_fn)) {
        goto done;
    }
    if (!JS_IsFunction(ctx, *load_fn)) {
        JS_ThrowInternalError(ctx, "global load() is not available");
        goto done;
    }

    *bind_fn = JS_GetPropertyStr(ctx, *load_fn, "bind");
    if (JS_IsException(*bind_fn)) {
        goto done;
    }
    if (!JS_IsFunction(ctx, *bind_fn)) {
        JS_ThrowInternalError(ctx, "Function.bind() is not available");
        goto done;
    }

    *bridge_namespace = JS_NewString(ctx, ESP32_MQUICKJS_BRIDGE_NAMESPACE);
    if (JS_IsException(*bridge_namespace)) {
        goto done;
    }

    *bridge_operation = JS_NewString(ctx, operation);
    if (JS_IsException(*bridge_operation)) {
        goto done;
    }

    bind_args[0] = JS_UNDEFINED;
    bind_args[1] = *bridge_namespace;
    bind_args[2] = *bridge_operation;
    result = js_call_function(ctx, *bind_fn, *load_fn, 3, bind_args);

done:
    JS_PopGCRef(ctx, &operation_ref);
    JS_PopGCRef(ctx, &namespace_ref);
    JS_PopGCRef(ctx, &bind_ref);
    JS_PopGCRef(ctx, &load_ref);
    return result;
}

bool esp32_mquickjs_set_property(JSContext *ctx,
                                 JSValue target_obj,
                                 const char *name,
                                 JSValue value)
{
    return !JS_IsException(JS_SetPropertyStr(ctx, target_obj, name, value));
}

bool esp32_mquickjs_set_alias(JSContext *ctx,
                              JSValue target_obj,
                              JSValue source_obj,
                              const char *target_name,
                              const char *source_name)
{
    JSValue value = JS_GetPropertyStr(ctx, source_obj, source_name);

    if (JS_IsException(value)) {
        return false;
    }
    return esp32_mquickjs_set_property(ctx, target_obj, target_name, value);
}

bool esp32_mquickjs_set_bound_bridge_function(JSContext *ctx,
                                              JSValue target_obj,
                                              JSValue global_obj,
                                              const char *target_name,
                                              const char *operation)
{
    JSValue func = esp32_mquickjs_make_bound_bridge_function(ctx, global_obj, operation);

    if (JS_IsException(func)) {
        return false;
    }
    return esp32_mquickjs_set_property(ctx, target_obj, target_name, func);
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
    runtime->output_generation = 0;
    runtime->prompt_needs_redraw = false;
    runtime->before_output = NULL;
    runtime->before_output_opaque = NULL;
    runtime->before_async_output = NULL;
    runtime->before_async_output_opaque = NULL;
    runtime->after_async_output = NULL;
    runtime->after_async_output_opaque = NULL;
    runtime->timer_state = NULL;
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

static JSValue js_print_help(void)
{
    prepare_console_output();
    fputs("See docs/repl-api.md for the REPL API reference.\n", stdout);
    fflush(stdout);
    note_console_output();
    return JS_UNDEFINED;
}

static JSValue js_host_bridge(JSContext *ctx, int argc, JSValue *argv)
{
    JSCStringBuf op_buf;
    const char *operation;
    JSValue result = JS_EXCEPTION;

    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "missing host bridge operation");
    }

    operation = JS_ToCString(ctx, argv[0], &op_buf);

    if (strcmp(operation, "help") == 0) {
        return js_print_help();
    }

    if (strcmp(operation, "setInterval") == 0) {
        if (argc < 3) {
            return JS_ThrowTypeError(ctx, "setInterval(fn, ms) expects a function and delay");
        }
        return esp32_mquickjs_create_timer(ctx, s_active_runtime, &argv[1], argv[2], true);
    }

    if (strcmp(operation, "sleep") == 0) {
        int delay_ms;

        if (argc < 2 || JS_ToInt32(ctx, &delay_ms, argv[1]) != 0 || delay_ms < 0) {
            return JS_ThrowTypeError(ctx, "sleep(ms) expects a non-negative integer");
        }
        vTaskDelay(pdMS_TO_TICKS((uint32_t)delay_ms));
        return JS_NewInt32(ctx, delay_ms);
    }

    if (strncmp(operation, "fs.", 3) == 0 &&
        esp32_mquickjs_dispatch_fs(ctx, operation + 3, argc - 1, argv + 1, &result)) {
        return result;
    }

    if (strncmp(operation, "gpio.", 5) == 0 &&
        esp32_mquickjs_dispatch_gpio(ctx, operation + 5, argc - 1, argv + 1, &result)) {
        return result;
    }

    if (strncmp(operation, "esp32.", 6) == 0 &&
        esp32_mquickjs_dispatch_esp32(ctx, operation + 6, argc - 1, argv + 1, &result)) {
        return result;
    }

    if (strncmp(operation, "wifi.", 5) == 0 &&
        esp32_mquickjs_dispatch_wifi(ctx, operation + 5, argc - 1, argv + 1, &result)) {
        return result;
    }

    return JS_ThrowReferenceError(ctx, "unknown host bridge operation: %s", operation);
}

bool esp32_mquickjs_install_globals(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref;
    JSValue *global_obj;

    if (ctx == NULL || runtime == NULL) {
        return false;
    }

    global_obj = JS_PushGCRef(ctx, &global_ref);
    *global_obj = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global_obj)) {
        JS_PopGCRef(ctx, &global_ref);
        esp32_mquickjs_print_exception(ctx);
        return false;
    }

    if (!esp32_mquickjs_set_property(ctx, *global_obj, "SCRIPTS_DIR",
                                     JS_NewString(ctx, ESP32_MQUICKJS_LITTLEFS_BASE_PATH)) ||
        !esp32_mquickjs_set_property(ctx, *global_obj, "LED_BUILTIN",
                                     JS_NewInt32(ctx, ESP32_MQUICKJS_USER_LED_PIN)) ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *global_obj, *global_obj, "help", "help") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *global_obj, *global_obj, "sleep", "sleep") ||
        !esp32_mquickjs_set_alias(ctx, *global_obj, *global_obj, "delay", "sleep") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *global_obj, *global_obj, "setInterval", "setInterval") ||
        !esp32_mquickjs_set_alias(ctx, *global_obj, *global_obj, "clearInterval", "clearTimeout") ||
        !esp32_mquickjs_install_fs_module(ctx, *global_obj) ||
        !esp32_mquickjs_install_gpio_module(ctx, *global_obj) ||
        !esp32_mquickjs_install_esp32_module(ctx, *global_obj) ||
        !esp32_mquickjs_install_wifi_module(ctx, *global_obj)) {
        JS_PopGCRef(ctx, &global_ref);
        esp32_mquickjs_print_exception(ctx);
        return false;
    }

    JS_PopGCRef(ctx, &global_ref);
    return true;
}

bool esp32_mquickjs_poll(JSContext *ctx,
                         esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_timer_state_t *state = esp32_mquickjs_timer_state(runtime);
    esp32_mquickjs_timer_event_t event;
    bool needs_redraw = false;
    bool wifi_handled = false;

    if (ctx == NULL || runtime == NULL) {
        return false;
    }
    if (state == NULL || state->queue == NULL || state->slots == NULL) {
        wifi_handled = esp32_mquickjs_poll_wifi(ctx, runtime);
        needs_redraw = runtime->prompt_needs_redraw || wifi_handled;
        runtime->prompt_needs_redraw = false;
        return needs_redraw;
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

    wifi_handled = esp32_mquickjs_poll_wifi(ctx, runtime);
    needs_redraw = runtime->prompt_needs_redraw || wifi_handled;
    runtime->prompt_needs_redraw = false;
    return needs_redraw;
}

static uint32_t js_print_output_lines(JSContext *ctx, int argc, JSValue *argv)
{
    uint32_t lines = 1;
    int i;

    for (i = 0; i < argc; i++) {
        if (JS_IsString(ctx, argv[i])) {
            JSCStringBuf buf;
            size_t len = 0;
            const char *str = JS_ToCStringLen(ctx, &len, argv[i], &buf);
            size_t j;

            for (j = 0; j < len; ++j) {
                if (str[j] == '\n') {
                    lines++;
                }
            }
        }
    }
    return lines;
}

JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int i;

    (void)this_val;
    begin_async_console_output(js_print_output_lines(ctx, argc, argv));
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
    end_async_console_output();
    note_console_output();
    return JS_UNDEFINED;
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
    JSCStringBuf command_buf;
    const char *command;

    (void)this_val;
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "load(path) expects a script path");
    }

    command = JS_ToCString(ctx, argv[0], &command_buf);

    if (strcmp(command, ESP32_MQUICKJS_BRIDGE_NAMESPACE) == 0) {
        return js_host_bridge(ctx, argc - 1, argv + 1);
    }

    return esp32_mquickjs_load_from_littlefs(ctx, s_active_runtime, command);
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
