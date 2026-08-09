#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_DAC

void esp32_mquickjs_init_dac_runtime(void);
void esp32_mquickjs_deinit_dac_runtime(void);

JSValue js_dac_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_dac_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_dac_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_dac_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_dac_ioToChannel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_dac_channelToIo(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_dac_get_channel_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_dac_get_resolution_bits(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_dac_get_max_value(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
