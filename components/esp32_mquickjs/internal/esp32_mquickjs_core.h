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

/**
 * Throw the sole v1 native operational-error shape.
 *
 * The supplied details object is attached as-is. Callers keep module-specific
 * diagnostics inside details while code and operation remain predictable at
 * the top level.
 */
JSValue esp32_mquickjs_throw_native_error(JSContext *ctx,
                                          const char *code,
                                          const char *operation,
                                          const char *message,
                                          JSValue details);

esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void);

bool esp32_mquickjs_register_reserved_reaper(
    esp32_mquickjs_runtime_t *runtime,
    size_t reserved_slot,
    esp32_mquickjs_reap_fn reap,
    void *opaque);

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
