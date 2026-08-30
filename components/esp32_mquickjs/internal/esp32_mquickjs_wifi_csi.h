#pragma once

#include "sdkconfig.h"
#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI

bool esp32_mquickjs_init_wifi_csi_runtime(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_wifi_csi_runtime(JSContext *ctx);

JSValue js_wifi_csi_capabilities(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv);
JSValue js_wifi_csi_open(JSContext *ctx, JSValue *this_val,
                         int argc, JSValue *argv);

JSValue js_wifi_csi_session_constructor(JSContext *ctx, JSValue *this_val,
                                        int argc, JSValue *argv);
void js_wifi_csi_session_finalizer(JSContext *ctx, void *opaque);
JSValue js_wifi_csi_session_status(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv);
JSValue js_wifi_csi_session_stats(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv);
JSValue js_wifi_csi_session_receive(JSContext *ctx, JSValue *this_val,
                                    int argc, JSValue *argv);
JSValue js_wifi_csi_session_receive_batch(JSContext *ctx, JSValue *this_val,
                                          int argc, JSValue *argv);
JSValue js_wifi_csi_session_stop(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv);
JSValue js_wifi_csi_session_configure(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv);
JSValue js_wifi_csi_session_start(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv);
JSValue js_wifi_csi_session_close(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv);

JSValue js_wifi_csi_frame_constructor(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv);
void js_wifi_csi_frame_finalizer(JSContext *ctx, void *opaque);
JSValue js_wifi_csi_frame_samples(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv);
JSValue js_wifi_csi_frame_copy_samples(JSContext *ctx, JSValue *this_val,
                                       int argc, JSValue *argv);
JSValue js_wifi_csi_frame_source(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv);
JSValue js_wifi_csi_frame_close(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv);

JSValue js_wifi_csi_batch_constructor(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv);
void js_wifi_csi_batch_finalizer(JSContext *ctx, void *opaque);
JSValue js_wifi_csi_batch_info(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv);
JSValue js_wifi_csi_batch_samples(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv);
JSValue js_wifi_csi_batch_source(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv);
JSValue js_wifi_csi_batch_close(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv);

#endif
