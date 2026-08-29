#pragma once

#include "esp32_mquickjs_types.h"

/*
 * Enumerate own enumerable property names through the engine intrinsic. This
 * never resolves Object.keys from the mutable JavaScript global object.
 */
JSValue esp32_mquickjs_own_property_keys(JSContext *ctx, JSValue value);

/*
 * Validate one plain options object without invoking application JavaScript.
 * The caller remains responsible for optional/null handling before calling.
 */
bool esp32_mquickjs_validate_plain_options(
    JSContext *ctx,
    JSValue options,
    const char *api_name,
    const char *const *allowed_keys,
    size_t allowed_key_count);

bool esp32_mquickjs_value_to_bounded_u32(JSContext *ctx,
                                         JSValue value,
                                         uint32_t minimum,
                                         uint32_t maximum,
                                         uint32_t *out);

bool esp32_mquickjs_value_to_bounded_i32(JSContext *ctx,
                                         JSValue value,
                                         int32_t minimum,
                                         int32_t maximum,
                                         int32_t *out);

bool esp32_mquickjs_value_to_enum(JSContext *ctx,
                                  JSValue value,
                                  const char *const *choices,
                                  size_t choice_count,
                                  size_t *out_index);
