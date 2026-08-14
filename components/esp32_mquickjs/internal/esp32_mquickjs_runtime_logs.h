#pragma once

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_log_ring.h"

bool esp32_mquickjs_init_runtime_logs(esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_runtime_logs(esp32_mquickjs_runtime_t *runtime);

void esp32_mquickjs_runtime_logs_console_begin(esp32_mquickjs_runtime_t *runtime,
                                               esp32_mquickjs_log_source_t source);
void esp32_mquickjs_runtime_logs_console_write(esp32_mquickjs_runtime_t *runtime,
                                               const void *data,
                                               size_t data_length);
void esp32_mquickjs_runtime_logs_console_end(esp32_mquickjs_runtime_t *runtime);

JSValue js_runtime_logs_read(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv);
