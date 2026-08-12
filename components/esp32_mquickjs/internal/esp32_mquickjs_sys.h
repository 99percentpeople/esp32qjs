#pragma once

#include "esp32_mquickjs_types.h"

bool esp32_mquickjs_init_secure_random(JSContext *ctx);
JSValue js_sys_info(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_millis(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_micros(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_freeHeap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_randomHex(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_withTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
