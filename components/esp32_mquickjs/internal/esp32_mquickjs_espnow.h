#pragma once

#include "sdkconfig.h"
#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW

bool esp32_mquickjs_init_espnow_runtime(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_espnow_runtime(JSContext *ctx);

JSValue js_espnow_session_constructor(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv);
void js_espnow_session_finalizer(JSContext *ctx, void *opaque);
JSValue js_espnow_session_receive(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv);
JSValue js_espnow_session_stats(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv);
JSValue js_espnow_session_status(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv);
JSValue js_espnow_session_add_peer(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv);
JSValue js_espnow_session_peer(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv);
JSValue js_espnow_session_peers(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv);
JSValue js_espnow_session_broadcast(JSContext *ctx, JSValue *this_val,
                                    int argc, JSValue *argv);
JSValue js_espnow_session_set_power_save(JSContext *ctx, JSValue *this_val,
                                         int argc, JSValue *argv);
JSValue js_espnow_session_close(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv);

JSValue js_espnow_peer_constructor(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv);
void js_espnow_peer_finalizer(JSContext *ctx, void *opaque);
JSValue js_espnow_peer_status(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv);
JSValue js_espnow_peer_send(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv);
JSValue js_espnow_peer_update(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv);
JSValue js_espnow_peer_close(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv);

JSValue js_espnow_capabilities(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv);
JSValue js_espnow_open(JSContext *ctx, JSValue *this_val,
                       int argc, JSValue *argv);

#endif
