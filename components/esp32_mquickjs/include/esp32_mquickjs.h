#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "mquickjs.h"

#define ESP32QJS_VERSION "0.1.0"
#define ESP32QJS_HOST_API_VERSION ((uint32_t)1U)

#define ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS ((uint32_t)CONFIG_ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS)
#define ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS ((uint32_t)CONFIG_ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS)
#define ESP32_MQUICKJS_LITTLEFS_BASE_PATH "/littlefs"
#define ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL "storage"
#define ESP32_MQUICKJS_FS_ROOT_MAX 64U

typedef struct esp32_mquickjs_runtime esp32_mquickjs_runtime_t;
typedef uint32_t esp32_mquickjs_poll_result_t;

typedef bool (*esp32_mquickjs_async_poller_t)(JSContext *ctx,
                                              esp32_mquickjs_runtime_t *runtime,
                                              void *opaque);
typedef bool (*esp32_mquickjs_cooperate_fn)(void *opaque);

typedef struct {
    uint64_t saved_deadline_us;
    uint64_t started_us;
} esp32_mquickjs_native_wait_t;

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
    bool repl_enabled;
    bool auto_run_startup_script;
    bool format_littlefs_on_mount_fail;
    uint64_t deadline_us;
    void (*prepare_output)(void *opaque);
    void *prepare_output_opaque;
    esp32_mquickjs_cooperate_fn cooperate;
    void *cooperate_opaque;
    void *timer_state;
    void *async_state;
    void *future_state;
    uint64_t scoped_deadline_us;
    uint16_t load_root_depth;
    char fs_root[ESP32_MQUICKJS_FS_ROOT_MAX];
    char load_root[ESP32_MQUICKJS_FS_ROOT_MAX];
};

JSContext *esp32_mquickjs_create(void *mem_start,
                                 size_t mem_size,
                                 esp32_mquickjs_runtime_t *runtime,
                                 uint32_t eval_timeout_ms);

/* Returns false while an asynchronous native worker still owns JS state. */
bool esp32_mquickjs_destroy(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime);

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed);
void esp32_mquickjs_unmount_littlefs(void);
bool esp32_mquickjs_mount_littlefs_partition(const char *partition_label,
                                             const char *base_path,
                                             bool format_if_mount_failed);
void esp32_mquickjs_unmount_littlefs_partition(const char *partition_label);

void esp32_mquickjs_set_eval_timeout(esp32_mquickjs_runtime_t *runtime,
                                     uint32_t eval_timeout_ms);

void esp32_mquickjs_set_cooperate_hook(esp32_mquickjs_runtime_t *runtime,
                                       esp32_mquickjs_cooperate_fn cooperate,
                                       void *opaque);
bool esp32_mquickjs_cooperate(esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_native_wait_begin(esp32_mquickjs_runtime_t *runtime,
                                      esp32_mquickjs_native_wait_t *wait);
void esp32_mquickjs_native_wait_end(esp32_mquickjs_runtime_t *runtime,
                                    esp32_mquickjs_native_wait_t *wait);
bool esp32_mquickjs_cooperative_delay(esp32_mquickjs_runtime_t *runtime,
                                      uint32_t delay_ms);

JSValue esp32_mquickjs_eval(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            const char *source,
                            const char *filename,
                            int eval_flags);

JSValue esp32_mquickjs_call(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            JSValue function,
                            JSValue this_value,
                            int argc,
                            JSValue *argv);

void esp32_mquickjs_print_exception(JSContext *ctx);

bool esp32_mquickjs_install_globals(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime);

esp32_mquickjs_poll_result_t esp32_mquickjs_poll(JSContext *ctx,
                                                 esp32_mquickjs_runtime_t *runtime);

bool esp32_mquickjs_register_async_poller(esp32_mquickjs_runtime_t *runtime,
                                          esp32_mquickjs_async_poller_t poller,
                                          void *opaque);

void esp32_mquickjs_attach_current_task(esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_detach_current_task(esp32_mquickjs_runtime_t *runtime);

void esp32_mquickjs_notify_activity(esp32_mquickjs_runtime_t *runtime);

void esp32_mquickjs_notify_active_runtime_from_isr(int *task_woken);

bool esp32_mquickjs_wait_for_activity(esp32_mquickjs_runtime_t *runtime,
                                      uint32_t timeout_ms);
