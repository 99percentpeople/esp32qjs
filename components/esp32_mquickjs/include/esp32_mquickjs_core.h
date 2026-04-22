#pragma once

#include "esp32_mquickjs_types.h"

bool esp32_mquickjs_set_property(JSContext *ctx,
                                 JSValue target_obj,
                                 const char *name,
                                 JSValue value);

esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void);

JSValue esp32_mquickjs_load_from_littlefs(JSContext *ctx,
                                          esp32_mquickjs_runtime_t *runtime,
                                          const char *script_path);

JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_help(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_defer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_waitFor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_deferred_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_deferred_resolve(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_deferred_reject(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_deferred_callback(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_deferred_nodeCallback(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_deferred_wait(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sleep(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_setInterval(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
