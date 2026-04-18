#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mquickjs.h"

#define ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS 250U

typedef struct {
    uint32_t eval_timeout_ms;
    uint64_t deadline_us;
    void *timer_state;
} esp32_mquickjs_runtime_t;

JSContext *esp32_mquickjs_create(void *mem_start,
                                 size_t mem_size,
                                 esp32_mquickjs_runtime_t *runtime,
                                 uint32_t eval_timeout_ms);

void esp32_mquickjs_set_eval_timeout(esp32_mquickjs_runtime_t *runtime,
                                     uint32_t eval_timeout_ms);

JSValue esp32_mquickjs_eval(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            const char *source,
                            const char *filename,
                            int eval_flags);

void esp32_mquickjs_print_exception(JSContext *ctx);

bool esp32_mquickjs_install_globals(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime);

bool esp32_mquickjs_poll(JSContext *ctx,
                         esp32_mquickjs_runtime_t *runtime);
