#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_UART

bool esp32_mquickjs_init_uart_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_uart_runtime(void);

JSValue js_uart_port_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_uart_port_finalizer(JSContext *ctx, void *opaque);
JSValue js_uart_port_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_port_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_port_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_port_write_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_port_write_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_port_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_port_available(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_port_flush(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_port_clear_rx(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_uart_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_get_default_port(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_get_default_tx(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_get_default_rx(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_get_default_baud(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_get_default_rx_buffer_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_get_default_tx_buffer_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_uart_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
