#include "esp32_mquickjs.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern const JSSTDLibraryDef js_stdlib;

#define ESP32_BRIDGE_NAMESPACE "__esp32__"
#define ESP32_MQUICKJS_MAX_TIMERS 16
#define ESP32_MQUICKJS_TIMER_QUEUE_LEN 16
#define XIAO_ESP32S3_USER_LED_PIN 21
/* Seeed documents the XIAO ESP32-S3 user LED on GPIO21 as active-low. */
#define XIAO_ESP32S3_USER_LED_ACTIVE_LOW 1

static uint64_t s_output_gpio_mask;
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

static const char ESP32_BOOTSTRAP_SOURCE[] =
    "var __helpEntries = [];\n"
    "function __attachHelp(target, text) {\n"
    "  if (target && (typeof target === 'function' || typeof target === 'object')) {\n"
    "    __helpEntries.push([target, text]);\n"
    "  }\n"
    "  return target;\n"
    "}\n"
    "function __lookupHelp(target) {\n"
    "  var i;\n"
    "  for (i = 0; i < __helpEntries.length; i++) {\n"
    "    if (__helpEntries[i][0] === target) {\n"
    "      return __helpEntries[i][1];\n"
    "    }\n"
    "  }\n"
    "  return undefined;\n"
    "}\n"
    "function __resolveHelpTarget(name) {\n"
    "  var value = globalThis;\n"
    "  var parts = name.split('.');\n"
    "  var i;\n"
    "  for (i = 0; i < parts.length; i++) {\n"
    "    if (!parts[i]) {\n"
    "      return undefined;\n"
    "    }\n"
    "    value = value[parts[i]];\n"
    "    if (value === undefined || value === null) {\n"
    "      return undefined;\n"
    "    }\n"
    "  }\n"
    "  return value;\n"
    "}\n"
    "function help(target) {\n"
    "  var lookup = target;\n"
    "  var doc;\n"
    "  if (arguments.length === 0) {\n"
    "    print('Use help(nameOrValue) to inspect a function or module.');\n"
    "    print('Try: help(help), help(print), help(gc), help(setTimeout), help(setInterval), help(esp32), help(esp32.led)');\n"
    "    return undefined;\n"
    "  }\n"
    "  if (typeof lookup === 'string') {\n"
    "    lookup = __resolveHelpTarget(lookup);\n"
    "    if (lookup === undefined) {\n"
    "      print('No help topic named ' + target);\n"
    "      return undefined;\n"
    "    }\n"
    "  }\n"
    "  if (lookup && (typeof lookup === 'function' || typeof lookup === 'object')) {\n"
    "    doc = __lookupHelp(lookup);\n"
    "    if (typeof doc === 'string') {\n"
    "      print(doc);\n"
    "      return undefined;\n"
    "    }\n"
    "  }\n"
    "  if (typeof lookup === 'function') {\n"
    "    print('No built-in help for function ' + (lookup.name || '<anonymous>') + '.');\n"
    "    return undefined;\n"
    "  }\n"
    "  if (lookup && typeof lookup === 'object') {\n"
    "    print('No built-in help for this object.');\n"
    "    return undefined;\n"
    "  }\n"
    "  print('help() expects a function, module object, or dotted name string.');\n"
    "  return undefined;\n"
    "}\n"
    "var __nativeGc = gc;\n"
    "globalThis.gc = function gc() {\n"
    "  __nativeGc();\n"
    "  return 'GC complete';\n"
    "};\n"
    "globalThis.LED_BUILTIN = 21;\n"
    "globalThis.setInterval = function(fn, ms) { return load('__esp32__', 'setInterval', fn, ms); };\n"
    "globalThis.clearInterval = function(id) { return clearTimeout(id); };\n"
    "globalThis.esp32 = {\n"
    "  INPUT: 'input',\n"
    "  OUTPUT: 'output',\n"
    "  USER_LED_PIN: 21,\n"
    "  USER_LED_ACTIVE_LOW: true,\n"
    "  info() { return load('__esp32__', 'info'); },\n"
    "  millis() { return load('__esp32__', 'millis'); },\n"
    "  micros() { return load('__esp32__', 'micros'); },\n"
    "  freeHeap() { return load('__esp32__', 'freeHeap'); },\n"
    "  sleep(ms) { return load('__esp32__', 'sleep', ms); },\n"
    "  delay(ms) { return load('__esp32__', 'sleep', ms); },\n"
    "  setInterval(fn, ms) { return globalThis.setInterval(fn, ms); },\n"
    "  clearInterval(id) { return globalThis.clearInterval(id); },\n"
    "  pinMode(pin, mode) { return load('__esp32__', 'pinMode', pin, mode); },\n"
    "  digitalWrite(pin, value) { return load('__esp32__', 'digitalWrite', pin, value); },\n"
    "  digitalRead(pin) { return load('__esp32__', 'digitalRead', pin); },\n"
    "  led(value) { return load('__esp32__', 'led', value); },\n"
    "};\n"
    "__attachHelp(help, 'help([topic])\\nPrint built-in help for a function or module.\\nExamples: help(), help(setTimeout), help(esp32), help(\"esp32.led\")');\n"
    "__attachHelp(print, 'print(...values)\\nWrite values to the REPL console.');\n"
    "__attachHelp(gc, 'gc()\\nRun the JavaScript garbage collector and return a confirmation string.');\n"
    "__attachHelp(setTimeout, 'setTimeout(fn, ms)\\nRun fn once after ms milliseconds using esp_timer. Returns a timer id.');\n"
    "__attachHelp(clearTimeout, 'clearTimeout(id)\\nCancel a timer created by setTimeout().');\n"
    "__attachHelp(setInterval, 'setInterval(fn, ms)\\nRun fn repeatedly every ms milliseconds using esp_timer. Returns a timer id.');\n"
    "__attachHelp(clearInterval, 'clearInterval(id)\\nCancel a timer created by setInterval().');\n"
    "__attachHelp(esp32, 'esp32\\nBoard helper module for time, memory, GPIO, and the user LED.\\nMembers: info, millis, micros, freeHeap, sleep, delay, setInterval, clearInterval, pinMode, digitalWrite, digitalRead, led');\n"
    "__attachHelp(esp32.info, 'esp32.info()\\nReturn board, chip, LED pin, active-low flag, free heap, and current JS time.');\n"
    "__attachHelp(esp32.millis, 'esp32.millis()\\nReturn monotonic time in milliseconds from esp_timer.');\n"
    "__attachHelp(esp32.micros, 'esp32.micros()\\nReturn monotonic time in microseconds from esp_timer.');\n"
    "__attachHelp(esp32.freeHeap, 'esp32.freeHeap()\\nReturn current free heap in bytes.');\n"
    "__attachHelp(esp32.sleep, 'esp32.sleep(ms)\\nBlock the REPL task for ms milliseconds.');\n"
    "__attachHelp(esp32.delay, 'esp32.delay(ms)\\nAlias of esp32.sleep(ms).');\n"
    "__attachHelp(esp32.setInterval, 'esp32.setInterval(fn, ms)\\nAlias of the global setInterval(fn, ms).');\n"
    "__attachHelp(esp32.clearInterval, 'esp32.clearInterval(id)\\nAlias of the global clearInterval(id).');\n"
    "__attachHelp(esp32.pinMode, 'esp32.pinMode(pin, mode)\\nConfigure a GPIO as esp32.INPUT or esp32.OUTPUT.');\n"
    "__attachHelp(esp32.digitalWrite, 'esp32.digitalWrite(pin, value)\\nSet a GPIO output level. Non-zero values map to high.');\n"
    "__attachHelp(esp32.digitalRead, 'esp32.digitalRead(pin)\\nRead a GPIO level and return true or false.');\n"
    "__attachHelp(esp32.led, 'esp32.led(value)\\nControl the XIAO ESP32-S3 user LED. true turns the LED on.');\n";

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

static int js_value_to_bool(JSContext *ctx, JSValue value, bool *out_value)
{
    int int_value;

    if (JS_IsBool(value)) {
        *out_value = (value == JS_TRUE);
        return 0;
    }

    if (JS_ToInt32(ctx, &int_value, value) == 0) {
        *out_value = (int_value != 0);
        return 0;
    }

    return -1;
}

static int js_value_to_gpio_num(JSContext *ctx, JSValue value, gpio_num_t *out_pin)
{
    int pin;

    if (JS_ToInt32(ctx, &pin, value) != 0) {
        return -1;
    }
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin)) {
        return -1;
    }

    *out_pin = (gpio_num_t)pin;
    return 0;
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

static JSValue esp32_make_info_object(JSContext *ctx)
{
    JSGCRef info_ref;
    JSValue *info;

    info = JS_PushGCRef(ctx, &info_ref);
    *info = JS_NewObject(ctx);
    if (JS_IsException(*info)) {
        goto fail;
    }
    if (JS_IsException(JS_SetPropertyStr(ctx, *info, "board",
                                         JS_NewString(ctx, "Seeed XIAO ESP32-S3")))) {
        goto fail;
    }
    if (JS_IsException(JS_SetPropertyStr(ctx, *info, "chip",
                                         JS_NewString(ctx, "ESP32-S3")))) {
        goto fail;
    }
    if (JS_IsException(JS_SetPropertyStr(ctx, *info, "userLedPin",
                                         JS_NewInt32(ctx, XIAO_ESP32S3_USER_LED_PIN)))) {
        goto fail;
    }
    if (JS_IsException(JS_SetPropertyStr(ctx, *info, "userLedActiveLow",
                                         JS_NewBool(XIAO_ESP32S3_USER_LED_ACTIVE_LOW)))) {
        goto fail;
    }
    if (JS_IsException(JS_SetPropertyStr(ctx, *info, "freeHeap",
                                         JS_NewUint32(ctx, esp_get_free_heap_size())))) {
        goto fail;
    }
    if (JS_IsException(JS_SetPropertyStr(ctx, *info, "jsTimeMs",
                                         JS_NewInt64(ctx, esp_timer_get_time() / 1000)))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &info_ref);

fail:
    JS_PopGCRef(ctx, &info_ref);
    return JS_EXCEPTION;
}

static JSValue esp32_gpio_set_mode(JSContext *ctx, gpio_num_t pin, const char *mode)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (strcmp(mode, "output") == 0) {
        io_cfg.mode = GPIO_MODE_OUTPUT;
    } else if (strcmp(mode, "input") == 0) {
        io_cfg.mode = GPIO_MODE_INPUT;
    } else {
        return JS_ThrowTypeError(ctx, "pinMode() expects 'input' or 'output'");
    }

    if (gpio_config(&io_cfg) != ESP_OK) {
        return JS_ThrowInternalError(ctx, "gpio_config(%d) failed", (int)pin);
    }

    if (io_cfg.mode == GPIO_MODE_OUTPUT) {
        s_output_gpio_mask |= 1ULL << pin;
    } else {
        s_output_gpio_mask &= ~(1ULL << pin);
    }

    return JS_NewInt32(ctx, (int32_t)pin);
}

static JSValue esp32_gpio_write(JSContext *ctx, gpio_num_t pin, bool level)
{
    if ((s_output_gpio_mask & (1ULL << pin)) == 0) {
        JSValue mode_result = esp32_gpio_set_mode(ctx, pin, "output");
        if (JS_IsException(mode_result)) {
            return mode_result;
        }
    }

    if (gpio_set_level(pin, level ? 1 : 0) != ESP_OK) {
        return JS_ThrowInternalError(ctx, "gpio_set_level(%d) failed", (int)pin);
    }

    return JS_NewBool(level);
}

static JSValue js_esp32_bridge(JSContext *ctx, int argc, JSValue *argv)
{
    JSCStringBuf op_buf;
    const char *operation;

    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "missing ESP32 bridge operation");
    }

    operation = JS_ToCString(ctx, argv[0], &op_buf);

    if (strcmp(operation, "info") == 0) {
        return esp32_make_info_object(ctx);
    }

    if (strcmp(operation, "millis") == 0) {
        return JS_NewInt64(ctx, esp_timer_get_time() / 1000);
    }

    if (strcmp(operation, "micros") == 0) {
        return JS_NewInt64(ctx, esp_timer_get_time());
    }

    if (strcmp(operation, "freeHeap") == 0) {
        return JS_NewUint32(ctx, esp_get_free_heap_size());
    }

    if (strcmp(operation, "sleep") == 0) {
        int delay_ms;

        if (argc < 2 || JS_ToInt32(ctx, &delay_ms, argv[1]) != 0 || delay_ms < 0) {
            return JS_ThrowTypeError(ctx, "sleep(ms) expects a non-negative integer");
        }
        vTaskDelay(pdMS_TO_TICKS((uint32_t)delay_ms));
        return JS_NewInt32(ctx, delay_ms);
    }

    if (strcmp(operation, "setInterval") == 0) {
        if (argc < 3) {
            return JS_ThrowTypeError(ctx, "setInterval(fn, ms) expects a function and delay");
        }
        return esp32_mquickjs_create_timer(ctx, s_active_runtime, &argv[1], argv[2], true);
    }

    if (strcmp(operation, "pinMode") == 0) {
        JSCStringBuf mode_buf;
        const char *mode;
        gpio_num_t pin;

        if (argc < 3 || js_value_to_gpio_num(ctx, argv[1], &pin) != 0 || !JS_IsString(ctx, argv[2])) {
            return JS_ThrowTypeError(ctx, "pinMode(pin, mode) expects a valid GPIO and mode string");
        }

        mode = JS_ToCString(ctx, argv[2], &mode_buf);
        return esp32_gpio_set_mode(ctx, pin, mode);
    }

    if (strcmp(operation, "digitalWrite") == 0) {
        gpio_num_t pin;
        bool level;

        if (argc < 3 || js_value_to_gpio_num(ctx, argv[1], &pin) != 0 ||
            js_value_to_bool(ctx, argv[2], &level) != 0) {
            return JS_ThrowTypeError(ctx, "digitalWrite(pin, value) expects a valid GPIO and boolean-like value");
        }

        return esp32_gpio_write(ctx, pin, level);
    }

    if (strcmp(operation, "digitalRead") == 0) {
        gpio_num_t pin;

        if (argc < 2 || js_value_to_gpio_num(ctx, argv[1], &pin) != 0) {
            return JS_ThrowTypeError(ctx, "digitalRead(pin) expects a valid GPIO");
        }

        return JS_NewBool(gpio_get_level(pin) != 0);
    }

    if (strcmp(operation, "led") == 0) {
        bool level;
        bool gpio_level;
        JSValue result;

        if (argc < 2 || js_value_to_bool(ctx, argv[1], &level) != 0) {
            return JS_ThrowTypeError(ctx, "led(value) expects a boolean-like value");
        }

        gpio_level = XIAO_ESP32S3_USER_LED_ACTIVE_LOW ? !level : level;
        result = esp32_gpio_write(ctx, (gpio_num_t)XIAO_ESP32S3_USER_LED_PIN, gpio_level);
        if (JS_IsException(result)) {
            return result;
        }
        return JS_NewBool(level);
    }

    return JS_ThrowReferenceError(ctx, "unknown ESP32 bridge operation: %s", operation);
}

bool esp32_mquickjs_install_globals(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime)
{
    JSValue result;

    if (ctx == NULL || runtime == NULL) {
        return false;
    }

    result = esp32_mquickjs_eval(ctx,
                                 runtime,
                                 ESP32_BOOTSTRAP_SOURCE,
                                 "<esp32-bootstrap>",
                                 JS_EVAL_STRIP_COL);

    if (JS_IsException(result)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }

    return true;
}

bool esp32_mquickjs_poll(JSContext *ctx,
                         esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_timer_state_t *state = esp32_mquickjs_timer_state(runtime);
    esp32_mquickjs_timer_event_t event;
    bool needs_redraw;
    bool handled = false;

    if (ctx == NULL || state == NULL || state->queue == NULL || state->slots == NULL) {
        return false;
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
        handled = true;
        if (JS_IsException(ret)) {
            esp32_mquickjs_print_exception(ctx);
        }
    }

    needs_redraw = handled && runtime->prompt_needs_redraw;
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

            for (size_t j = 0; j < len; ++j) {
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
    (void)this_val;

    if (argc >= 1 && JS_IsString(ctx, argv[0])) {
        JSCStringBuf command_buf;
        const char *command = JS_ToCString(ctx, argv[0], &command_buf);

        if (strcmp(command, ESP32_BRIDGE_NAMESPACE) == 0) {
            return js_esp32_bridge(ctx, argc - 1, argv + 1);
        }
    }

    return JS_ThrowInternalError(ctx, "load() is not supported on ESP32");
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
