#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI

bool esp32_mquickjs_init_spi_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_spi_runtime(void);

JSValue js_spi_bus_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_spi_bus_finalizer(JSContext *ctx, void *opaque);
JSValue js_spi_bus_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_bus_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_bus_open_device(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_spi_device_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_spi_device_finalizer(JSContext *ctx, void *opaque);
JSValue js_spi_device_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_device_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_device_transfer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_device_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_device_write_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_device_write_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_device_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_spi_open_bus(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_get_host_2(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_get_host_3(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_get_default_host(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_get_default_sclk(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_get_default_mosi(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_get_default_miso(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_get_default_cs(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_get_default_freq_hz(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_get_default_queue_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_spi_get_default_max_transfer_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
