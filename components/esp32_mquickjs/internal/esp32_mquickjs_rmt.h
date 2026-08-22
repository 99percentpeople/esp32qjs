#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_RMT

bool esp32_mquickjs_init_rmt_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_rmt_runtime(void);

JSValue js_rmt_symbol_buffer_constructor(JSContext *ctx, JSValue *this_val,
                                         int argc, JSValue *argv);
void js_rmt_symbol_buffer_finalizer(JSContext *ctx, void *opaque);
JSValue js_rmt_symbol_buffer_get_capacity(JSContext *ctx, JSValue *this_val,
                                          int argc, JSValue *argv);
JSValue js_rmt_symbol_buffer_get_length(JSContext *ctx, JSValue *this_val,
                                        int argc, JSValue *argv);
JSValue js_rmt_symbol_buffer_push(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv);
JSValue js_rmt_symbol_buffer_get(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv);
JSValue js_rmt_symbol_buffer_set(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv);
JSValue js_rmt_symbol_buffer_clear(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv);
JSValue js_rmt_symbol_buffer_close(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv);

JSValue js_rmt_channel_constructor(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv);
void js_rmt_channel_finalizer(JSContext *ctx, void *opaque);
JSValue js_rmt_channel_start(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv);
JSValue js_rmt_channel_stop(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv);
JSValue js_rmt_channel_transmit(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv);
JSValue js_rmt_channel_receive(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv);
JSValue js_rmt_channel_status(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv);
JSValue js_rmt_channel_close(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv);

JSValue js_rmt_capabilities(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv);
JSValue js_rmt_create_symbols(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv);
JSValue js_rmt_open(JSContext *ctx, JSValue *this_val,
                    int argc, JSValue *argv);

#endif
