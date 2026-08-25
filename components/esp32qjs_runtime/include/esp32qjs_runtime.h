#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp32_mquickjs.h"

typedef struct esp32qjs_runtime esp32qjs_runtime_t;

typedef bool (*esp32qjs_runtime_install_globals_fn)(JSContext *ctx,
                                                     esp32_mquickjs_runtime_t *engine,
                                                     void *opaque);

typedef struct {
    size_t js_heap_size;
    uint32_t eval_timeout_ms;
    uint32_t task_stack_size;
    uint32_t task_priority;
    uint32_t stop_timeout_ms;
    uint32_t restart_timeout_ms;
    esp32_mquickjs_restart_failure_action_t restart_failure_action;
    bool prefer_psram;
    bool mount_littlefs;
    bool require_littlefs;
    bool mount_secondary_littlefs;
    bool require_secondary_littlefs;
    bool format_littlefs_on_mount_fail;
    bool autorun_startup_script;
    bool enable_repl;
    bool task_watchdog;
    bool js_watchdog;
    bool startup_guard;
    uint32_t startup_failure_limit;
    uint32_t startup_healthy_ms;
    const char *startup_script;
    const char *task_name;
    const char *secondary_littlefs_partition_label;
    const char *secondary_littlefs_base_path;
    esp32qjs_runtime_install_globals_fn install_globals;
    void *opaque;
} esp32qjs_runtime_config_t;

void esp32qjs_runtime_default_config(esp32qjs_runtime_config_t *config);

esp_err_t esp32qjs_runtime_create(const esp32qjs_runtime_config_t *config,
                                  esp32qjs_runtime_t **out_runtime);

esp_err_t esp32qjs_runtime_start(esp32qjs_runtime_t *runtime);
void esp32qjs_runtime_request_stop(esp32qjs_runtime_t *runtime);
esp_err_t esp32qjs_runtime_request_control(
    esp32qjs_runtime_t *runtime,
    esp32_mquickjs_control_action_t action,
    const char *reason,
    uint32_t delay_ms,
    esp32_mquickjs_control_receipt_t *receipt);
esp_err_t esp32qjs_runtime_stop(esp32qjs_runtime_t *runtime,
                                uint32_t timeout_ms);
esp_err_t esp32qjs_runtime_destroy(esp32qjs_runtime_t *runtime);

bool esp32qjs_runtime_is_running(const esp32qjs_runtime_t *runtime);
JSContext *esp32qjs_runtime_context(esp32qjs_runtime_t *runtime);
esp32_mquickjs_runtime_t *esp32qjs_runtime_engine(esp32qjs_runtime_t *runtime);
