#include "esp32_mquickjs_nvs.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_NVS

#include "esp32_mquickjs_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "nvs.h"
#include "nvs_flash.h"

#define ESP32_MQUICKJS_NVS_MAX_NAME_BYTES 15U
#define ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES 2048U

#ifdef CONFIG_NVS_ENCRYPTION
#define ESP32_MQUICKJS_NVS_ENCRYPTED 1
#else
#define ESP32_MQUICKJS_NVS_ENCRYPTED 0
#endif

static bool s_nvs_initialized;

static bool nvs_name_valid(const char *name, size_t length)
{
    size_t i;

    if (name == NULL || length == 0 || length > ESP32_MQUICKJS_NVS_MAX_NAME_BYTES) {
        return false;
    }
    for (i = 0; i < length; ++i) {
        char ch = name[i];

        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-')) {
            return false;
        }
    }
    return true;
}

static bool nvs_parse_name(JSContext *ctx,
                           JSValue value,
                           const char *label,
                           char output[ESP32_MQUICKJS_NVS_MAX_NAME_BYTES + 1U])
{
    JSCStringBuf buffer;
    const char *name;
    size_t length = 0;

    if (!JS_IsString(ctx, value)) {
        JS_ThrowTypeError(ctx, "nvs %s must be a string", label);
        return false;
    }
    name = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (name == NULL) {
        return false;
    }
    if (!nvs_name_valid(name, length)) {
        JS_ThrowTypeError(ctx,
                          "nvs %s must be 1..%u ASCII letters, digits, '_' or '-'",
                          label,
                          (unsigned)ESP32_MQUICKJS_NVS_MAX_NAME_BYTES);
        return false;
    }
    memcpy(output, name, length);
    output[length] = '\0';
    return true;
}

static JSValue nvs_throw_error(JSContext *ctx, const char *operation, esp_err_t err)
{
    return JS_ThrowInternalError(ctx,
                                 "nvs.%s() failed: %s",
                                 operation,
                                 esp_err_to_name(err));
}

bool esp32_mquickjs_init_nvs_runtime(JSContext *ctx)
{
    esp_err_t err;

    if (s_nvs_initialized) {
        return true;
    }
    err = nvs_flash_init();
    if (err != ESP_OK) {
        nvs_throw_error(ctx, "init", err);
        return false;
    }
    s_nvs_initialized = true;
    return true;
}

JSValue js_nvs_getString(JSContext *ctx,
                         JSValue *this_val,
                         int argc,
                         JSValue *argv)
{
    char namespace_name[ESP32_MQUICKJS_NVS_MAX_NAME_BYTES + 1U];
    char key[ESP32_MQUICKJS_NVS_MAX_NAME_BYTES + 1U];
    nvs_handle_t handle;
    size_t required = 0;
    char *value = NULL;
    JSValue result;
    esp_err_t err;

    (void)this_val;
    if (!s_nvs_initialized) {
        return JS_ThrowInternalError(ctx, "nvs is not initialized");
    }
    if (argc != 2) {
        return JS_ThrowTypeError(ctx, "nvs.getString(namespace, key) expects two strings");
    }
    if (!nvs_parse_name(ctx, argv[0], "namespace", namespace_name) ||
        !nvs_parse_name(ctx, argv[1], "key", key)) {
        return JS_EXCEPTION;
    }

    err = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return JS_NULL;
    }
    if (err != ESP_OK) {
        return nvs_throw_error(ctx, "getString", err);
    }

    err = nvs_get_str(handle, key, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return JS_NULL;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return nvs_throw_error(ctx, "getString", err);
    }
    if (required == 0 || required - 1U > ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES) {
        nvs_close(handle);
        return JS_ThrowRangeError(ctx,
                                  "nvs.getString() value exceeds %u bytes",
                                  (unsigned)ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES);
    }

    value = heap_caps_malloc(required, MALLOC_CAP_8BIT);
    if (value == NULL) {
        nvs_close(handle);
        return JS_ThrowOutOfMemory(ctx);
    }
    err = nvs_get_str(handle, key, value, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        heap_caps_free(value);
        return nvs_throw_error(ctx, "getString", err);
    }

    result = JS_NewStringLen(ctx, value, required - 1U);
    heap_caps_free(value);
    return result;
}

JSValue js_nvs_setString(JSContext *ctx,
                         JSValue *this_val,
                         int argc,
                         JSValue *argv)
{
    JSCStringBuf value_buffer;
    char namespace_name[ESP32_MQUICKJS_NVS_MAX_NAME_BYTES + 1U];
    char key[ESP32_MQUICKJS_NVS_MAX_NAME_BYTES + 1U];
    const char *value;
    size_t value_length = 0;
    nvs_handle_t handle;
    esp_err_t err;

    (void)this_val;
    if (!s_nvs_initialized) {
        return JS_ThrowInternalError(ctx, "nvs is not initialized");
    }
    if (argc != 3 || !JS_IsString(ctx, argv[2])) {
        return JS_ThrowTypeError(ctx,
                                 "nvs.setString(namespace, key, value) expects three strings");
    }
    if (!nvs_parse_name(ctx, argv[0], "namespace", namespace_name) ||
        !nvs_parse_name(ctx, argv[1], "key", key)) {
        return JS_EXCEPTION;
    }
    value = JS_ToCStringLen(ctx, &value_length, argv[2], &value_buffer);
    if (value == NULL) {
        return JS_EXCEPTION;
    }
    if (value_length > ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES ||
        memchr(value, '\0', value_length) != NULL) {
        return JS_ThrowRangeError(ctx,
                                  "nvs.setString() value must contain at most %u bytes and no NUL",
                                  (unsigned)ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES);
    }

    err = nvs_open(namespace_name, NVS_READWRITE_PURGE, &handle);
    if (err != ESP_OK) {
        return nvs_throw_error(ctx, "setString", err);
    }
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        return nvs_throw_error(ctx, "setString", err);
    }
    return JS_NewInt32(ctx, (int32_t)value_length);
}

JSValue js_nvs_erase(JSContext *ctx,
                     JSValue *this_val,
                     int argc,
                     JSValue *argv)
{
    char namespace_name[ESP32_MQUICKJS_NVS_MAX_NAME_BYTES + 1U];
    char key[ESP32_MQUICKJS_NVS_MAX_NAME_BYTES + 1U];
    nvs_handle_t handle;
    esp_err_t err;

    (void)this_val;
    if (!s_nvs_initialized) {
        return JS_ThrowInternalError(ctx, "nvs is not initialized");
    }
    if (argc != 2) {
        return JS_ThrowTypeError(ctx, "nvs.erase(namespace, key) expects two strings");
    }
    if (!nvs_parse_name(ctx, argv[0], "namespace", namespace_name) ||
        !nvs_parse_name(ctx, argv[1], "key", key)) {
        return JS_EXCEPTION;
    }

    err = nvs_open(namespace_name, NVS_READWRITE_PURGE, &handle);
    if (err != ESP_OK) {
        return nvs_throw_error(ctx, "erase", err);
    }
    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return JS_NewBool(false);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        return nvs_throw_error(ctx, "erase", err);
    }
    return JS_NewBool(true);
}

JSValue js_nvs_clear(JSContext *ctx,
                     JSValue *this_val,
                     int argc,
                     JSValue *argv)
{
    char namespace_name[ESP32_MQUICKJS_NVS_MAX_NAME_BYTES + 1U];
    nvs_handle_t handle;
    esp_err_t err;

    (void)this_val;
    if (!s_nvs_initialized) {
        return JS_ThrowInternalError(ctx, "nvs is not initialized");
    }
    if (argc != 1) {
        return JS_ThrowTypeError(ctx, "nvs.clear(namespace) expects one string");
    }
    if (!nvs_parse_name(ctx, argv[0], "namespace", namespace_name)) {
        return JS_EXCEPTION;
    }

    err = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return JS_NewBool(false);
    }
    if (err != ESP_OK) {
        return nvs_throw_error(ctx, "clear", err);
    }
    nvs_close(handle);

    err = nvs_open(namespace_name, NVS_READWRITE_PURGE, &handle);
    if (err != ESP_OK) {
        return nvs_throw_error(ctx, "clear", err);
    }
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        return nvs_throw_error(ctx, "clear", err);
    }
    return JS_NewBool(true);
}

JSValue js_nvs_status(JSContext *ctx,
                      JSValue *this_val,
                      int argc,
                      JSValue *argv)
{
    JSGCRef status_ref;
    JSValue *status;

    (void)this_val;
    (void)argc;
    (void)argv;
    status = JS_PushGCRef(ctx, &status_ref);
    *status = JS_NewObject(ctx);
    if (JS_IsException(*status) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "initialized",
                                         JS_NewBool(s_nvs_initialized)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "encrypted",
                                         JS_NewBool(ESP32_MQUICKJS_NVS_ENCRYPTED)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "maxValueBytes",
                                         JS_NewInt32(ctx,
                                                     ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &status_ref);
}

JSValue js_nvs_get_max_value_bytes(JSContext *ctx,
                                    JSValue *this_val,
                                    int argc,
                                    JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES);
}

#endif
