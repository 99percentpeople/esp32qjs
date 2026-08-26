#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET

bool esp32_mquickjs_init_socket_runtime(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_socket_runtime(JSContext *ctx);

JSValue js_socket_open_tcp(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_listen_tcp(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_open_udp(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_handle_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_socket_handle_finalizer(JSContext *ctx, void *opaque);
JSValue js_socket_handle_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_handle_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_get_max_transfer_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_tcp_connect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_tcp_accept(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_tcp_send(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_tcp_recv(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_udp_send_to(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_udp_receive_from(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
