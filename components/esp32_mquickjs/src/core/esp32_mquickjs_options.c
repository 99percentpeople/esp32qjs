#include "esp32_mquickjs_options.h"

#include "mquickjs_priv.h"

#include <math.h>
#include <string.h>

JSValue esp32_mquickjs_own_property_keys(JSContext *ctx, JSValue value)
{
    JSGCRef value_ref;
    JSValue *rooted_value = JS_PushGCRef(ctx, &value_ref);
    JSValue result;

    *rooted_value = value;
    result = js_object_keys(ctx, NULL, 1, rooted_value);
    JS_PopGCRef(ctx, &value_ref);
    return result;
}

bool esp32_mquickjs_validate_plain_options(
    JSContext *ctx,
    JSValue options,
    const char *api_name,
    const char *const *allowed_keys,
    size_t allowed_key_count)
{
    JSGCRef keys_ref;
    JSGCRef length_ref;
    JSValue *keys = JS_PushGCRef(ctx, &keys_ref);
    JSValue *length_value = JS_PushGCRef(ctx, &length_ref);
    uint32_t length = 0;
    uint32_t index;
    bool valid = false;

    if (api_name == NULL || allowed_keys == NULL ||
        JS_GetClassID(ctx, options) != JS_CLASS_OBJECT) {
        JS_ThrowTypeError(ctx, "%s expects a plain options object",
                          api_name != NULL ? api_name : "native API");
        goto done;
    }
    *keys = esp32_mquickjs_own_property_keys(ctx, options);
    *length_value = JS_IsException(*keys)
                        ? JS_EXCEPTION
                        : JS_GetPropertyStr(ctx, *keys, "length");
    if (JS_IsException(*length_value) ||
        JS_ToUint32(ctx, &length, *length_value) != 0) {
        goto done;
    }
    for (index = 0; index < length; ++index) {
        JSGCRef key_ref;
        JSValue *key_value = JS_PushGCRef(ctx, &key_ref);
        JSCStringBuf key_buffer;
        const char *key;
        size_t allowed_index;
        bool matched = false;

        *key_value = JS_GetPropertyUint32(ctx, *keys, index);
        key = JS_IsException(*key_value)
                  ? NULL
                  : JS_ToCString(ctx, *key_value, &key_buffer);
        if (key != NULL) {
            for (allowed_index = 0; allowed_index < allowed_key_count;
                 ++allowed_index) {
                if (strcmp(key, allowed_keys[allowed_index]) == 0) {
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            if (key != NULL) {
                JS_ThrowTypeError(ctx, "%s received unknown option '%s'",
                                  api_name, key);
            }
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }
        JS_PopGCRef(ctx, &key_ref);
    }
    valid = true;

done:
    JS_PopGCRef(ctx, &length_ref);
    JS_PopGCRef(ctx, &keys_ref);
    return valid;
}

bool esp32_mquickjs_value_to_bounded_u32(JSContext *ctx,
                                         JSValue value,
                                         uint32_t minimum,
                                         uint32_t maximum,
                                         uint32_t *out)
{
    double number;

    if (out == NULL || minimum > maximum || !JS_IsNumber(ctx, value) ||
        JS_ToNumber(ctx, &number, value) != 0 || !isfinite(number) ||
        number < (double)minimum || number > (double)maximum ||
        (double)(uint32_t)number != number) {
        return false;
    }
    *out = (uint32_t)number;
    return true;
}

bool esp32_mquickjs_value_to_bounded_i32(JSContext *ctx,
                                         JSValue value,
                                         int32_t minimum,
                                         int32_t maximum,
                                         int32_t *out)
{
    double number;

    if (out == NULL || minimum > maximum || !JS_IsNumber(ctx, value) ||
        JS_ToNumber(ctx, &number, value) != 0 || !isfinite(number) ||
        number < (double)minimum || number > (double)maximum ||
        (double)(int32_t)number != number) {
        return false;
    }
    *out = (int32_t)number;
    return true;
}

bool esp32_mquickjs_value_to_enum(JSContext *ctx,
                                  JSValue value,
                                  const char *const *choices,
                                  size_t choice_count,
                                  size_t *out_index)
{
    JSCStringBuf buffer;
    const char *text;
    size_t index;

    if (choices == NULL || out_index == NULL || !JS_IsString(ctx, value) ||
        (text = JS_ToCString(ctx, value, &buffer)) == NULL) {
        return false;
    }
    for (index = 0; index < choice_count; ++index) {
        if (strcmp(text, choices[index]) == 0) {
            *out_index = index;
            return true;
        }
    }
    return false;
}
