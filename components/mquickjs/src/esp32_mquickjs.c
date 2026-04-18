#include "esp32_mquickjs.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const JSSTDLibraryDef js_stdlib;

#define ESP32_BRIDGE_NAMESPACE "__esp32__"
#define XIAO_ESP32S3_USER_LED_PIN 21
/* Seeed documents the XIAO ESP32-S3 user LED on GPIO21 as active-low. */
#define XIAO_ESP32S3_USER_LED_ACTIVE_LOW 1

static uint64_t s_output_gpio_mask;

static const char ESP32_BOOTSTRAP_SOURCE[] =
    "globalThis.LED_BUILTIN = 21;\n"
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
    "  pinMode(pin, mode) { return load('__esp32__', 'pinMode', pin, mode); },\n"
    "  digitalWrite(pin, value) { return load('__esp32__', 'digitalWrite', pin, value); },\n"
    "  digitalRead(pin) { return load('__esp32__', 'digitalRead', pin); },\n"
    "  led(value) { return load('__esp32__', 'led', value); },\n"
    "};\n";

static void js_log_write(void *opaque, const void *buf, size_t buf_len)
{
    (void)opaque;
    fwrite(buf, 1, buf_len, stdout);
    fflush(stdout);
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

    ctx = JS_NewContext(mem_start, mem_size, &js_stdlib);
    if (ctx == NULL) {
        return NULL;
    }

    JS_SetContextOpaque(ctx, runtime);
    JS_SetLogFunc(ctx, js_log_write);
    JS_SetInterruptHandler(ctx, js_interrupt_handler);
    JS_SetRandomSeed(ctx, (uint64_t)esp_timer_get_time());
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

    JS_PrintValueF(ctx, exception, JS_DUMP_LONG);
    fputc('\n', stdout);
    fflush(stdout);
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

JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int i;

    (void)this_val;
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
    (void)argc;
    (void)argv;
    return JS_ThrowInternalError(ctx, "setTimeout() is not available in this REPL");
}

JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowInternalError(ctx, "clearTimeout() is not available in this REPL");
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
