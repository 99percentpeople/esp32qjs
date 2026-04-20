#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "mquickjs.h"

#define ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS ((uint32_t)CONFIG_ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS)
#define ESP32_MQUICKJS_LITTLEFS_BASE_PATH "/littlefs"
#define ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL "storage"

typedef struct esp32_mquickjs_runtime esp32_mquickjs_runtime_t;
typedef uint32_t esp32_mquickjs_poll_result_t;

typedef bool (*esp32_mquickjs_async_poller_t)(JSContext *ctx,
                                              esp32_mquickjs_runtime_t *runtime,
                                              void *opaque);

#define ESP32_MQUICKJS_POLL_NONE   ((esp32_mquickjs_poll_result_t)0U)
#define ESP32_MQUICKJS_POLL_ASYNC  ((esp32_mquickjs_poll_result_t)(1U << 0))
#define ESP32_MQUICKJS_POLL_OUTPUT ((esp32_mquickjs_poll_result_t)(1U << 1))

struct esp32_mquickjs_runtime {
    uint32_t eval_timeout_ms;
    uint32_t async_generation;
    uint32_t output_generation;
    size_t js_heap_size;
    bool js_heap_in_psram;
    bool littlefs_mounted;
    uint64_t deadline_us;
    void (*prepare_output)(void *opaque);
    void *prepare_output_opaque;
    void *timer_state;
    void *async_state;
};

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

esp32_mquickjs_poll_result_t esp32_mquickjs_poll(JSContext *ctx,
                                                 esp32_mquickjs_runtime_t *runtime);

bool esp32_mquickjs_register_async_poller(esp32_mquickjs_runtime_t *runtime,
                                          esp32_mquickjs_async_poller_t poller,
                                          void *opaque);

void esp32_mquickjs_attach_current_task(esp32_mquickjs_runtime_t *runtime);

void esp32_mquickjs_notify_activity(esp32_mquickjs_runtime_t *runtime);

void esp32_mquickjs_notify_active_runtime_from_isr(int *task_woken);

bool esp32_mquickjs_wait_for_activity(esp32_mquickjs_runtime_t *runtime,
                                      uint32_t timeout_ms);
