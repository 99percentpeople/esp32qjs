#pragma once

#include "esp32_mquickjs_types.h"

bool esp32_mquickjs_init_secure_random(JSContext *ctx);
JSValue js_esp32_info(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_esp32_millis(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_esp32_micros(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_esp32_freeHeap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_esp32_randomHex(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_esp32_withTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
