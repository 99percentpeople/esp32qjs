#include "esp32_mquickjs_internal.h"

#include <string.h>

#include "esp_system.h"
#include "esp_timer.h"

static JSValue esp32_make_info_object(JSContext *ctx)
{
    JSGCRef info_ref;
    JSValue *info;

    info = JS_PushGCRef(ctx, &info_ref);
    *info = JS_NewObject(ctx);
    if (JS_IsException(*info)) {
        goto fail;
    }
    if (!esp32_mquickjs_set_property(ctx, *info, "board",
                                     JS_NewString(ctx, "Seeed XIAO ESP32-S3")) ||
        !esp32_mquickjs_set_property(ctx, *info, "chip",
                                     JS_NewString(ctx, "ESP32-S3")) ||
        !esp32_mquickjs_set_property(ctx, *info, "userLedPin",
                                     JS_NewInt32(ctx, ESP32_MQUICKJS_USER_LED_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *info, "userLedActiveLow",
                                     JS_NewBool(ESP32_MQUICKJS_USER_LED_ACTIVE_LOW)) ||
        !esp32_mquickjs_set_property(ctx, *info, "scriptsDir",
                                     JS_NewString(ctx, ESP32_MQUICKJS_LITTLEFS_BASE_PATH)) ||
        !esp32_mquickjs_set_property(ctx, *info, "freeHeap",
                                     JS_NewUint32(ctx, esp_get_free_heap_size())) ||
        !esp32_mquickjs_set_property(ctx, *info, "jsTimeMs",
                                     JS_NewInt64(ctx, esp_timer_get_time() / 1000))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &info_ref);

fail:
    JS_PopGCRef(ctx, &info_ref);
    return JS_EXCEPTION;
}

bool esp32_mquickjs_install_esp32_module(JSContext *ctx, JSValue global_obj)
{
    JSGCRef module_ref;
    JSValue *module_obj;

    module_obj = JS_PushGCRef(ctx, &module_ref);
    *module_obj = JS_NewObject(ctx);
    if (JS_IsException(*module_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "info", "esp32.info") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "millis", "esp32.millis") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "micros", "esp32.micros") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "freeHeap", "esp32.freeHeap")) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, global_obj, "esp32", JS_PopGCRef(ctx, &module_ref))) {
        return false;
    }
    return true;

fail:
    JS_PopGCRef(ctx, &module_ref);
    return false;
}

bool esp32_mquickjs_dispatch_esp32(JSContext *ctx,
                                   const char *operation,
                                   int argc,
                                   JSValue *argv,
                                   JSValue *result)
{
    (void)argc;
    (void)argv;

    if (strcmp(operation, "info") == 0) {
        *result = esp32_make_info_object(ctx);
        return true;
    }

    if (strcmp(operation, "millis") == 0) {
        *result = JS_NewInt64(ctx, esp_timer_get_time() / 1000);
        return true;
    }

    if (strcmp(operation, "micros") == 0) {
        *result = JS_NewInt64(ctx, esp_timer_get_time());
        return true;
    }

    if (strcmp(operation, "freeHeap") == 0) {
        *result = JS_NewUint32(ctx, esp_get_free_heap_size());
        return true;
    }

    return false;
}
