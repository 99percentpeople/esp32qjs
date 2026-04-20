#include "esp32_mquickjs_internal.h"

#include <string.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char *esp32_chip_model_name(void)
{
    esp_chip_info_t chip_info;

    esp_chip_info(&chip_info);
    switch (chip_info.model) {
    case CHIP_ESP32:
        return "ESP32";
    case CHIP_ESP32S2:
        return "ESP32-S2";
    case CHIP_ESP32S3:
        return "ESP32-S3";
    case CHIP_ESP32C3:
        return "ESP32-C3";
    case CHIP_ESP32C2:
        return "ESP32-C2";
    case CHIP_ESP32C6:
        return "ESP32-C6";
    case CHIP_ESP32H2:
        return "ESP32-H2";
    case CHIP_ESP32P4:
        return "ESP32-P4";
    case CHIP_ESP32C5:
        return "ESP32-C5";
    case CHIP_ESP32C61:
        return "ESP32-C61";
    default:
        return "ESP32";
    }
}

static JSValue esp32_make_info_object(JSContext *ctx)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    JSGCRef info_ref;
    JSValue *info;
    uint32_t flash_size = 0;
    bool psram_enabled = esp_psram_is_initialized();
    size_t total_psram = psram_enabled ? esp_psram_get_size() : 0;
    size_t free_psram = psram_enabled ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
    size_t total_internal_heap = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint32_t js_heap_size = runtime != NULL ? (uint32_t)runtime->js_heap_size : 0;
    const char *js_heap_region = (runtime != NULL && runtime->js_heap_in_psram) ? "psram" : "internal";
    bool littlefs_mounted = runtime != NULL && runtime->littlefs_mounted;
    bool auto_run_index_js = false;
    bool format_littlefs_on_mount_fail = false;
    bool repl_enabled = false;

#ifdef CONFIG_ESP32QJS_AUTORUN_INDEX_JS
    auto_run_index_js = true;
#endif
#ifdef CONFIG_ESP32QJS_LITTLEFS_FORMAT_ON_MOUNT_FAIL
    format_littlefs_on_mount_fail = true;
#endif
#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    repl_enabled = true;
#endif

    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }

    info = JS_PushGCRef(ctx, &info_ref);
    *info = JS_NewObject(ctx);
    if (JS_IsException(*info)) {
        goto fail;
    }
    if (!esp32_mquickjs_set_property(ctx, *info, "board",
                                     JS_NewString(ctx, "Seeed XIAO ESP32-S3")) ||
        !esp32_mquickjs_set_property(ctx, *info, "chip",
                                     JS_NewString(ctx, esp32_chip_model_name())) ||
        !esp32_mquickjs_set_property(ctx, *info, "userLedPin",
                                     JS_NewInt32(ctx, ESP32_MQUICKJS_USER_LED_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *info, "userLedActiveLow",
                                     JS_NewBool(ESP32_MQUICKJS_USER_LED_ACTIVE_LOW)) ||
        !esp32_mquickjs_set_property(ctx, *info, "scriptsDir",
                                     JS_NewString(ctx, ESP32_MQUICKJS_LITTLEFS_BASE_PATH)) ||
        !esp32_mquickjs_set_property(ctx, *info, "flashSize",
                                     JS_NewUint32(ctx, flash_size)) ||
        !esp32_mquickjs_set_property(ctx, *info, "psramEnabled",
                                     JS_NewBool(psram_enabled)) ||
        !esp32_mquickjs_set_property(ctx, *info, "psramSize",
                                     JS_NewUint32(ctx, (uint32_t)total_psram)) ||
        !esp32_mquickjs_set_property(ctx, *info, "freePsram",
                                     JS_NewUint32(ctx, (uint32_t)free_psram)) ||
        !esp32_mquickjs_set_property(ctx, *info, "totalInternalHeap",
                                     JS_NewUint32(ctx, (uint32_t)total_internal_heap)) ||
        !esp32_mquickjs_set_property(ctx, *info, "freeInternalHeap",
                                     JS_NewUint32(ctx, (uint32_t)free_internal_heap)) ||
        !esp32_mquickjs_set_property(ctx, *info, "jsHeapSize",
                                     JS_NewUint32(ctx, js_heap_size)) ||
        !esp32_mquickjs_set_property(ctx, *info, "jsHeapRegion",
                                     JS_NewString(ctx, js_heap_region)) ||
        !esp32_mquickjs_set_property(ctx, *info, "littlefsMounted",
                                     JS_NewBool(littlefs_mounted)) ||
        !esp32_mquickjs_set_property(ctx, *info, "replEnabled",
                                     JS_NewBool(repl_enabled)) ||
        !esp32_mquickjs_set_property(ctx, *info, "autoRunIndexJs",
                                     JS_NewBool(auto_run_index_js)) ||
        !esp32_mquickjs_set_property(ctx, *info, "formatLittlefsOnMountFail",
                                     JS_NewBool(format_littlefs_on_mount_fail)) ||
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
