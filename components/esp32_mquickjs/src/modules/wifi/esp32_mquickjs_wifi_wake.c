#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_heap_caps.h"

/* Outstanding SDK references belong to the native registry, not JS storage. */
static bool s_wake_retiring;

static JSValue wifi_wake_error(JSContext *ctx, const char *operation, esp_err_t err)
{
    return esp32_mquickjs_wifi_throw_operation_error(ctx, "WIFI_WAKE_LOCK_FAILED",
        operation, err, -1, UINT32_MAX);
}

bool esp32_mquickjs_init_wifi_wake_runtime(JSContext *ctx)
{
    if (!s_wake_retiring) return true;
    esp_err_t err = esp32_mquickjs_wifi_radio_wake_release_all();
    if (err != ESP_OK) {
        (void)wifi_wake_error(ctx, "WiFiWakeLock.close", err);
        return false;
    }
    s_wake_retiring = false;
    return true;
}

void esp32_mquickjs_deinit_wifi_wake_runtime(void)
{
    s_wake_retiring = true;
    (void)esp32_mquickjs_wifi_radio_wake_release_all();
}

JSValue js_wifi_wake_lock_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.acquireWakeLock() to create a WiFiWakeLock");
}

JSValue js_wifi_acquire_wake_lock(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.acquireWakeLock() takes no arguments");
    if (s_wake_retiring) return wifi_wake_error(ctx, "wifi.acquireWakeLock", ESP_ERR_INVALID_STATE);
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_WAKE_LOCK);
    if (JS_IsException(*object)) goto fail;
    esp32_mquickjs_wifi_wake_token_t *token = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*token), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (token == NULL) { JS_ThrowOutOfMemory(ctx); goto fail; }
    /* All fallible JS construction precedes native acquire. */
    esp_err_t err = esp32_mquickjs_wifi_radio_wake_acquire(token);
    if (err != ESP_OK) {
        esp32_mquickjs_memory_payload_free(token);
        (void)wifi_wake_error(ctx, "wifi.acquireWakeLock", err);
        goto fail;
    }
    JS_SetOpaque(ctx, *object, token);
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_wake_lock_acquired(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argc; (void)argv;
    if (this_val == NULL || JS_GetClassID(ctx, *this_val) != JS_CLASS_WIFI_WAKE_LOCK)
        return JS_ThrowTypeError(ctx, "WiFiWakeLock.acquired requires a WiFiWakeLock");
    esp32_mquickjs_wifi_wake_token_t *token = JS_GetOpaque(ctx, *this_val);
    return JS_NewBool(token != NULL && esp32_mquickjs_wifi_radio_wake_active(token));
}

JSValue js_wifi_wake_lock_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0 || this_val == NULL || JS_GetClassID(ctx, *this_val) != JS_CLASS_WIFI_WAKE_LOCK)
        return JS_ThrowTypeError(ctx, "WiFiWakeLock.close() takes no arguments");
    esp32_mquickjs_wifi_wake_token_t *token = JS_GetOpaque(ctx, *this_val);
    if (token == NULL) return JS_UNDEFINED;
    esp_err_t err = esp32_mquickjs_wifi_radio_wake_release(token);
    if (err != ESP_OK) return wifi_wake_error(ctx, "WiFiWakeLock.close", err);
    JS_SetOpaque(ctx, *this_val, NULL);
    esp32_mquickjs_memory_payload_free(token);
    return JS_UNDEFINED;
}

void js_wifi_wake_lock_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    esp32_mquickjs_wifi_wake_token_t *token = opaque;
    if (token == NULL) return;
    /* Failed SDK release stays in the bounded registry for runtime retirement. */
    (void)esp32_mquickjs_wifi_radio_wake_release(token);
    esp32_mquickjs_memory_payload_free(token);
}
#endif
