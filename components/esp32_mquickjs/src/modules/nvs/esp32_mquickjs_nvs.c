#include "esp32_mquickjs_nvs.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_NVS

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
static SemaphoreHandle_t s_nvs_worker_lock;
static bool nvs_register_future_drivers(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);

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

bool esp32_mquickjs_init_nvs_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime)
{
    esp_err_t err;

    if (!s_nvs_initialized) {
        err = nvs_flash_init();
        if (err != ESP_OK) {
            nvs_throw_error(ctx, "init", err);
            return false;
        }
        s_nvs_worker_lock = xSemaphoreCreateMutex();
        if (s_nvs_worker_lock == NULL) {
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        s_nvs_initialized = true;
    }
    return nvs_register_future_drivers(ctx, runtime);
}

typedef enum {
    NVS_FUTURE_GET_STRING,
    NVS_FUTURE_SET_STRING,
    NVS_FUTURE_ERASE,
    NVS_FUTURE_CLEAR,
} nvs_future_kind_t;

struct esp32_mquickjs_future_driver_state {
    nvs_future_kind_t kind;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    char namespace_name[ESP32_MQUICKJS_NVS_MAX_NAME_BYTES + 1U];
    char key[ESP32_MQUICKJS_NVS_MAX_NAME_BYTES + 1U];
    char *value;
    size_t value_length;
    esp_err_t err;
    bool found;
    bool result;
    volatile bool completed;
    bool cancelled;
};

static void nvs_future_release(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    heap_caps_free(state->value);
    heap_caps_free(state);
}

static esp32_mquickjs_future_driver_state_t *nvs_future_allocate(
    JSContext *ctx,
    nvs_future_kind_t kind)
{
    esp32_mquickjs_future_driver_state_t *state;

    if (!s_nvs_initialized) {
        JS_ThrowInternalError(ctx, "nvs is not initialized");
        return NULL;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    state->kind = kind;
    return state;
}

static bool nvs_get_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    (void)this_ref;
    if (out_state == NULL || argc != 2) {
        JS_ThrowTypeError(ctx, "nvs.getString(namespace, key) expects two strings");
        return false;
    }
    state = nvs_future_allocate(ctx, NVS_FUTURE_GET_STRING);
    if (state == NULL) {
        return false;
    }
    if (!nvs_parse_name(ctx, argv[0].val, "namespace", state->namespace_name) ||
        !nvs_parse_name(ctx, argv[1].val, "key", state->key)) {
        nvs_future_release(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool nvs_set_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    JSCStringBuf value_buffer;
    const char *value;
    size_t value_length = 0;

    (void)this_ref;
    if (out_state == NULL || argc != 3 || !JS_IsString(ctx, argv[2].val)) {
        JS_ThrowTypeError(ctx,
                          "nvs.setString(namespace, key, value) expects three strings");
        return false;
    }
    state = nvs_future_allocate(ctx, NVS_FUTURE_SET_STRING);
    if (state == NULL) {
        return false;
    }
    if (!nvs_parse_name(ctx, argv[0].val, "namespace", state->namespace_name) ||
        !nvs_parse_name(ctx, argv[1].val, "key", state->key)) {
        nvs_future_release(state);
        return false;
    }
    value = JS_ToCStringLen(ctx, &value_length, argv[2].val, &value_buffer);
    if (value == NULL) {
        nvs_future_release(state);
        return false;
    }
    if (value_length > ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES ||
        memchr(value, '\0', value_length) != NULL) {
        nvs_future_release(state);
        JS_ThrowRangeError(ctx,
                           "nvs.setString() value must contain at most %u bytes and no NUL",
                           (unsigned)ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES);
        return false;
    }
    state->value = heap_caps_malloc(value_length + 1U, MALLOC_CAP_8BIT);
    if (state->value == NULL) {
        nvs_future_release(state);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    memcpy(state->value, value, value_length);
    state->value[value_length] = '\0';
    state->value_length = value_length;
    *out_state = state;
    return true;
}

static bool nvs_erase_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    (void)this_ref;
    if (out_state == NULL || argc != 2) {
        JS_ThrowTypeError(ctx, "nvs.erase(namespace, key) expects two strings");
        return false;
    }
    state = nvs_future_allocate(ctx, NVS_FUTURE_ERASE);
    if (state == NULL) {
        return false;
    }
    if (!nvs_parse_name(ctx, argv[0].val, "namespace", state->namespace_name) ||
        !nvs_parse_name(ctx, argv[1].val, "key", state->key)) {
        nvs_future_release(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool nvs_clear_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    (void)this_ref;
    if (out_state == NULL || argc != 1) {
        JS_ThrowTypeError(ctx, "nvs.clear(namespace) expects one string");
        return false;
    }
    state = nvs_future_allocate(ctx, NVS_FUTURE_CLEAR);
    if (state == NULL) {
        return false;
    }
    if (!nvs_parse_name(ctx, argv[0].val, "namespace", state->namespace_name)) {
        nvs_future_release(state);
        return false;
    }
    *out_state = state;
    return true;
}

static void nvs_future_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    nvs_handle_t handle;
    esp_err_t err = ESP_OK;

    if (state == NULL) {
        return;
    }
    xSemaphoreTake(s_nvs_worker_lock, portMAX_DELAY);
    if (state->kind == NVS_FUTURE_GET_STRING) {
        size_t required = 0;

        err = nvs_open(state->namespace_name, NVS_READONLY, &handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        } else if (err == ESP_OK) {
            err = nvs_get_str(handle, state->key, NULL, &required);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                err = ESP_OK;
            } else if (err == ESP_OK &&
                       (required == 0 || required - 1U > ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES)) {
                err = ESP_ERR_NVS_INVALID_LENGTH;
            } else if (err == ESP_OK) {
                state->value = heap_caps_malloc(required, MALLOC_CAP_8BIT);
                if (state->value == NULL) {
                    err = ESP_ERR_NO_MEM;
                } else {
                    err = nvs_get_str(handle, state->key, state->value, &required);
                    if (err == ESP_OK) {
                        state->value_length = required - 1U;
                        state->found = true;
                    }
                }
            }
            nvs_close(handle);
        }
    } else if (state->kind == NVS_FUTURE_SET_STRING) {
        err = nvs_open(state->namespace_name, NVS_READWRITE_PURGE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_str(handle, state->key, state->value);
            if (err == ESP_OK) {
                err = nvs_commit(handle);
            }
            nvs_close(handle);
        }
    } else if (state->kind == NVS_FUTURE_ERASE) {
        err = nvs_open(state->namespace_name, NVS_READWRITE_PURGE, &handle);
        if (err == ESP_OK) {
            err = nvs_erase_key(handle, state->key);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                err = ESP_OK;
                state->result = false;
            } else if (err == ESP_OK) {
                err = nvs_commit(handle);
                state->result = err == ESP_OK;
            }
            nvs_close(handle);
        }
    } else {
        err = nvs_open(state->namespace_name, NVS_READONLY, &handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
            state->result = false;
        } else if (err == ESP_OK) {
            nvs_close(handle);
            err = nvs_open(state->namespace_name, NVS_READWRITE_PURGE, &handle);
            if (err == ESP_OK) {
                err = nvs_erase_all(handle);
                if (err == ESP_OK) {
                    err = nvs_commit(handle);
                }
                state->result = err == ESP_OK;
                nvs_close(handle);
            }
        }
    }
    xSemaphoreGive(s_nvs_worker_lock);
    state->err = err;
    state->completed = true;
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
}

static bool nvs_future_start(JSContext *ctx,
                             esp32_mquickjs_runtime_t *runtime,
                             esp32_mquickjs_future_token_t token,
                             esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    if (!esp32_mquickjs_future_submit_worker(
            runtime, token, nvs_future_worker, state)) {
        JS_ThrowInternalError(ctx, "NVS Future worker queue is busy");
        return false;
    }
    return true;
}

static esp32_mquickjs_future_poll_t nvs_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && state->completed
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue nvs_future_finish(JSContext *ctx,
                                 esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "NVS operation cancelled");
    }
    if (state->err != ESP_OK) {
        if (state->kind == NVS_FUTURE_GET_STRING &&
            state->err == ESP_ERR_NVS_INVALID_LENGTH) {
            return JS_ThrowRangeError(ctx,
                                      "nvs.getString() value exceeds %u bytes",
                                      (unsigned)ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES);
        }
        return nvs_throw_error(ctx,
                               state->kind == NVS_FUTURE_GET_STRING ? "getString" :
                               state->kind == NVS_FUTURE_SET_STRING ? "setString" :
                               state->kind == NVS_FUTURE_ERASE ? "erase" : "clear",
                               state->err);
    }
    if (state->kind == NVS_FUTURE_GET_STRING) {
        return state->found
            ? JS_NewStringLen(ctx, state->value, state->value_length)
            : JS_NULL;
    }
    if (state->kind == NVS_FUTURE_SET_STRING) {
        return JS_NewInt32(ctx, (int32_t)state->value_length);
    }
    return JS_NewBool(state->result);
}

static bool nvs_future_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->cancelled) {
        return false;
    }
    state->cancelled = true;
    return true;
}

static void nvs_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    nvs_future_release(state);
}

#define NVS_FUTURE_DRIVER(name, prepare_fn) \
    static const esp32_mquickjs_future_driver_t name = { \
        .prepare = prepare_fn, \
        .start = nvs_future_start, \
        .poll = nvs_future_poll, \
        .finish = nvs_future_finish, \
        .cancel = nvs_future_cancel, \
        .destroy = nvs_future_destroy, \
    }

NVS_FUTURE_DRIVER(s_nvs_get_driver, nvs_get_future_prepare);
NVS_FUTURE_DRIVER(s_nvs_set_driver, nvs_set_future_prepare);
NVS_FUTURE_DRIVER(s_nvs_erase_driver, nvs_erase_future_prepare);
NVS_FUTURE_DRIVER(s_nvs_clear_driver, nvs_clear_future_prepare);

#undef NVS_FUTURE_DRIVER

static bool nvs_register_future_drivers(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime)
{
    static const char *names[] = { "getString", "setString", "erase", "clear" };
    static const esp32_mquickjs_future_driver_t *drivers[] = {
        &s_nvs_get_driver,
        &s_nvs_set_driver,
        &s_nvs_erase_driver,
        &s_nvs_clear_driver,
    };
    JSGCRef global_ref;
    JSGCRef nvs_ref;
    JSValue *global_obj = JS_PushGCRef(ctx, &global_ref);
    JSValue *nvs_obj = JS_PushGCRef(ctx, &nvs_ref);
    size_t index;
    bool result = true;

    *global_obj = JS_GetGlobalObject(ctx);
    *nvs_obj = JS_IsException(*global_obj)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global_obj, "nvs");
    for (index = 0; result && index < sizeof(names) / sizeof(names[0]); ++index) {
        JSGCRef method_ref;
        JSValue *method = JS_PushGCRef(ctx, &method_ref);

        *method = JS_IsException(*nvs_obj)
            ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *nvs_obj, names[index]);
        result = !JS_IsException(*method) &&
                 esp32_mquickjs_future_register_driver(ctx, runtime,
                                                       *method, drivers[index]);
        JS_PopGCRef(ctx, &method_ref);
    }
    if (!result && !JS_IsException(*nvs_obj)) {
        JS_ThrowInternalError(ctx, "failed to register NVS Future drivers");
    }
    JS_PopGCRef(ctx, &nvs_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

static JSValue nvs_future_call_and_wait(JSContext *ctx,
                                        JSValue receiver,
                                        const char *method_name,
                                        int argc,
                                        JSValue *argv)
{
    JSGCRef receiver_ref;
    JSGCRef method_ref;
    JSValue *rooted_receiver = JS_PushGCRef(ctx, &receiver_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *rooted_receiver = receiver;
    *method = JS_GetPropertyStr(ctx, *rooted_receiver, method_name);
    result = JS_IsException(*method)
        ? JS_EXCEPTION
        : esp32_mquickjs_future_call_and_wait(ctx,
                                              esp32_mquickjs_get_active_runtime(),
                                              *method,
                                              *rooted_receiver,
                                              argc,
                                              argv);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &receiver_ref);
    return result;
}

JSValue js_nvs_getString(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return nvs_future_call_and_wait(ctx, *this_val, "getString", argc, argv);
}

JSValue js_nvs_setString(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return nvs_future_call_and_wait(ctx, *this_val, "setString", argc, argv);
}

JSValue js_nvs_erase(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return nvs_future_call_and_wait(ctx, *this_val, "erase", argc, argv);
}

JSValue js_nvs_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return nvs_future_call_and_wait(ctx, *this_val, "clear", argc, argv);
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
