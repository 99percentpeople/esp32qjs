#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_LEDC

void esp32_mquickjs_init_ledc_runtime(void);
void esp32_mquickjs_deinit_ledc_runtime(void);

JSValue js_ledc_timerConfig(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_channelConfig(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_setDuty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_setDutyWithHpoint(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_setDutyAndUpdate(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_getDuty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_getHpoint(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_updateDuty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_setFreq(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_getFreq(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_bindChannelTimer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_timerPause(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_timerResume(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_timerStatus(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_channelStatus(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_get_channel_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_get_timer_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_ledc_get_max_duty_resolution_bits(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
