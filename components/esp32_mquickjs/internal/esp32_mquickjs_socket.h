#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET

bool esp32_mquickjs_init_socket_runtime(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_socket_runtime(JSContext *ctx);

JSValue js_socket_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_get_max_message_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_tcp_connect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_tcp_listen(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_tcp_accept(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_tcp_send(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_tcp_recv(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_udp_sendto(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_socket_udp_recvfrom(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
