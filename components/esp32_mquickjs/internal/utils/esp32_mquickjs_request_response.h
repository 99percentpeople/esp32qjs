#pragma once

#include "esp32_mquickjs_types.h"

bool esp32_mquickjs_is_headers_object(JSContext *ctx, JSValue value);
bool esp32_mquickjs_is_request_object(JSContext *ctx, JSValue value);
bool esp32_mquickjs_is_response_object(JSContext *ctx, JSValue value);

JSValue esp32_mquickjs_make_headers_object(JSContext *ctx,
                                           JSValue global_obj,
                                           JSValue init_value);

JSValue esp32_mquickjs_headers_to_plain_object(JSContext *ctx, JSValue headers_value);

JSValue esp32_mquickjs_make_request_object(JSContext *ctx,
                                           JSValue global_obj,
                                           const char *method,
                                           const char *url,
                                           const char *path,
                                           const char *route,
                                           const char *mount_path,
                                           const char *relative_path,
                                           const char *query_string,
                                           JSValue query_value,
                                           JSValue headers_init,
                                           JSValue body_stream);

JSValue esp32_mquickjs_make_response_object(JSContext *ctx,
                                            JSValue global_obj,
                                            int32_t status,
                                            const char *status_text,
                                            const char *url,
                                            JSValue headers_init,
                                            JSValue body_stream);

JSValue esp32_mquickjs_make_text_body_stream(JSContext *ctx,
                                             JSValue global_obj,
                                             const char *text);

JSValue js_headers_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_headers_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_headers_set(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_headers_has(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_headers_delete(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_headers_entries(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_headers_toObject(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_request_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_request_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_request_json(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_response_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_response_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_response_json(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_response_make_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_response_make_json(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_response_make_stream(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
