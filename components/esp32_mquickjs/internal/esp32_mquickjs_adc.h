#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_ADC

void esp32_mquickjs_init_adc_runtime(void);
void esp32_mquickjs_deinit_adc_runtime(void);

JSValue js_adc_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_adc_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_adc_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_adc_configure(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_adc_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_adc_readMilliVolts(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_adc_ioToChannel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_adc_channelToIo(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_adc_get_unit_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_adc_get_max_channel_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
