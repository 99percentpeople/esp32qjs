#include "esp32_mquickjs_internal.h"

JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
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

#include "mqjs_stdlib.h"
