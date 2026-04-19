#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "mquickjs.h"

#define ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS ((uint32_t)CONFIG_ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS)
#define ESP32_MQUICKJS_LITTLEFS_BASE_PATH "/littlefs"
#define ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL "storage"

typedef struct {
    uint32_t eval_timeout_ms;
    uint32_t output_generation;
    size_t js_heap_size;
    bool prompt_needs_redraw;
    bool js_heap_in_psram;
    bool littlefs_mounted;
    uint64_t deadline_us;
    void (*before_output)(void *opaque);
    void *before_output_opaque;
    void (*before_async_output)(void *opaque, uint32_t lines);
    void *before_async_output_opaque;
    void (*after_async_output)(void *opaque);
    void *after_async_output_opaque;
    void *timer_state;
} esp32_mquickjs_runtime_t;

JSContext *esp32_mquickjs_create(void *mem_start,
                                 size_t mem_size,
                                 esp32_mquickjs_runtime_t *runtime,
                                 uint32_t eval_timeout_ms);

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed);

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
