#pragma once

#include "esp32_mquickjs_types.h"

bool esp32_mquickjs_set_property(JSContext *ctx,
                                 JSValue target_obj,
                                 const char *name,
                                 JSValue value);
bool esp32_mquickjs_set_property_ref(JSContext *ctx,
                                     JSValue *target_obj,
                                     const char *name,
                                     JSValue value);

esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void);
void esp32_mquickjs_native_gc_alloc(esp32_mquickjs_runtime_t *runtime,
                                    size_t size);
void esp32_mquickjs_native_gc_free(esp32_mquickjs_runtime_t *runtime,
                                   size_t size);
void esp32_mquickjs_native_gc_reclaimable(
    esp32_mquickjs_runtime_t *runtime);

JSValue esp32_mquickjs_load_from_active_fs(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime,
                                           const char *script_path);
JSValue esp32_mquickjs_load_from_root(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime,
                                      const char *base_path,
                                      const char *script_path);

JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_help(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_framework_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_get_root(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_volume(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_volume_constructor(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv);
void js_fs_volume_finalizer(JSContext *ctx, void *opaque);
JSValue js_sleep(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_runtime_defer_idle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_setInterval(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_date_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
