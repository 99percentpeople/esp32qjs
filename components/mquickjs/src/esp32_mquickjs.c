#include "esp32_mquickjs.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

extern const JSSTDLibraryDef js_stdlib;

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
    (void)argc;
    (void)argv;
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
