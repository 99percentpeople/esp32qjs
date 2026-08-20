#include "esp32qjs_runtime.h"

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if CONFIG_ESP32QJS_ENABLE_REPL
#include "esp32qjs_interactive.h"
#endif

#define ESP32QJS_REBOOT_MARKER_MAGIC UINT32_C(0x51534a52)

typedef struct {
    uint32_t magic;
    uint32_t checksum;
    char reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];
} esp32qjs_reboot_marker_t;

static RTC_NOINIT_ATTR esp32qjs_reboot_marker_t s_reboot_marker;

static const char *TAG = "esp32qjs_runtime";
static esp32qjs_runtime_t *s_active_runtime;

struct esp32qjs_runtime {
    esp32qjs_runtime_config_t config;
    esp32_mquickjs_runtime_t engine;
    JSContext *ctx;
    void *js_heap;
    TaskHandle_t task;
    SemaphoreHandle_t stopped;
    volatile bool stop_requested;
    volatile bool running;
    portMUX_TYPE lifecycle_lock;
    esp32_mquickjs_runtime_state_t state;
    uint32_t generation;
    uint32_t restart_count;
    uint64_t generation_started_us;
    char last_restart_reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];
    bool pending_control;
    esp32_mquickjs_control_action_t pending_action;
    char pending_reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];
    uint64_t pending_requested_at_ms;
    uint64_t pending_due_at_ms;
    char software_reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];
    bool software_reason_available;
    bool littlefs_mounted;
    bool secondary_littlefs_mounted;
    bool watchdog_registered;
    char startup_script[ESP32_MQUICKJS_HOST_STARTUP_PATH_MAX];
    char task_name[ESP32_MQUICKJS_HOST_TASK_NAME_MAX];
    char secondary_littlefs_partition_label[ESP32_MQUICKJS_HOST_PARTITION_LABEL_MAX];
    char secondary_littlefs_base_path[ESP32_MQUICKJS_FS_ROOT_MAX];
#if CONFIG_ESP32QJS_ENABLE_REPL
    esp32qjs_interactive_host_t interactive_host;
    esp32qjs_interactive_banner_t banner;
#endif
};

static bool runtime_cooperate(void *opaque);

static void runtime_copy_string(char *target,
                                size_t target_size,
                                const char *source,
                                const char *fallback)
{
    const char *value = source != NULL && source[0] != '\0' ? source : fallback;

    if (target_size == 0) {
        return;
    }
    snprintf(target, target_size, "%s", value != NULL ? value : "");
}

static uint32_t runtime_reason_checksum(const char *reason)
{
    const unsigned char *cursor = (const unsigned char *)reason;
    uint32_t checksum = UINT32_C(2166136261);

    while (cursor != NULL && *cursor != '\0') {
        checksum ^= *cursor++;
        checksum *= UINT32_C(16777619);
    }
    return checksum ^ ESP32QJS_REBOOT_MARKER_MAGIC;
}

static bool runtime_reason_is_valid(const char *reason)
{
    const unsigned char *cursor = (const unsigned char *)reason;
    size_t length = 0;

    if (cursor == NULL || *cursor == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (*cursor < 0x20U || *cursor == 0x7fU ||
            ++length > ESP32_MQUICKJS_CONTROL_REASON_MAX) {
            return false;
        }
        cursor++;
    }
    return true;
}

static void runtime_load_software_reason(esp32qjs_runtime_t *runtime)
{
    if (runtime != NULL && esp_reset_reason() == ESP_RST_SW &&
        s_reboot_marker.magic == ESP32QJS_REBOOT_MARKER_MAGIC &&
        runtime_reason_is_valid(s_reboot_marker.reason) &&
        s_reboot_marker.checksum == runtime_reason_checksum(s_reboot_marker.reason)) {
        runtime_copy_string(runtime->software_reason,
                            sizeof(runtime->software_reason),
                            s_reboot_marker.reason,
                            NULL);
        runtime->software_reason_available = true;
    }
    memset(&s_reboot_marker, 0, sizeof(s_reboot_marker));
}

static void runtime_store_software_reason(const char *reason)
{
    memset(&s_reboot_marker, 0, sizeof(s_reboot_marker));
    runtime_copy_string(s_reboot_marker.reason,
                        sizeof(s_reboot_marker.reason),
                        reason,
                        "javascript");
    s_reboot_marker.checksum = runtime_reason_checksum(s_reboot_marker.reason);
    s_reboot_marker.magic = ESP32QJS_REBOOT_MARKER_MAGIC;
}

static void runtime_clear_software_reason(void)
{
    memset(&s_reboot_marker, 0, sizeof(s_reboot_marker));
}

static void runtime_set_state(esp32qjs_runtime_t *runtime,
                              esp32_mquickjs_runtime_state_t state)
{
    if (runtime == NULL) {
        return;
    }
    portENTER_CRITICAL(&runtime->lifecycle_lock);
    runtime->state = state;
    portEXIT_CRITICAL(&runtime->lifecycle_lock);
}

static bool runtime_control_due(esp32qjs_runtime_t *runtime)
{
    bool due = false;
    uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000ULL;

    if (runtime == NULL) {
        return false;
    }
    portENTER_CRITICAL(&runtime->lifecycle_lock);
    due = runtime->pending_control && now_ms >= runtime->pending_due_at_ms;
    portEXIT_CRITICAL(&runtime->lifecycle_lock);
    return due;
}

static uint32_t runtime_control_wait_ms(esp32qjs_runtime_t *runtime,
                                        uint32_t requested_ms)
{
    uint64_t due_at_ms = 0;
    uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    bool pending = false;

    if (runtime == NULL) {
        return requested_ms;
    }
    portENTER_CRITICAL(&runtime->lifecycle_lock);
    pending = runtime->pending_control;
    due_at_ms = runtime->pending_due_at_ms;
    portEXIT_CRITICAL(&runtime->lifecycle_lock);
    if (!pending) {
        return requested_ms;
    }
    if (due_at_ms <= now_ms) {
        return 0;
    }
    if (due_at_ms - now_ms < requested_ms || requested_ms == UINT32_MAX) {
        uint64_t remaining = due_at_ms - now_ms;

        return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
    }
    return requested_ms;
}

static bool runtime_host_status(void *opaque,
                                esp32_mquickjs_host_status_t *status)
{
    esp32qjs_runtime_t *runtime = opaque;

    if (runtime == NULL || status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    status->managed = true;
    portENTER_CRITICAL(&runtime->lifecycle_lock);
    status->state = runtime->state;
    status->generation = runtime->generation;
    status->restart_count = runtime->restart_count;
    status->generation_started_us = runtime->generation_started_us;
    runtime_copy_string(status->last_restart_reason,
                        sizeof(status->last_restart_reason),
                        runtime->last_restart_reason,
                        NULL);
    status->pending_control = runtime->pending_control;
    status->pending_action = runtime->pending_action;
    runtime_copy_string(status->pending_reason,
                        sizeof(status->pending_reason),
                        runtime->pending_reason,
                        NULL);
    status->pending_requested_at_ms = runtime->pending_requested_at_ms;
    status->pending_due_at_ms = runtime->pending_due_at_ms;
    portEXIT_CRITICAL(&runtime->lifecycle_lock);

    runtime_copy_string(status->task_name,
                        sizeof(status->task_name),
                        runtime->task_name,
                        "js_runtime");
    status->task_stack_size = runtime->config.task_stack_size;
    status->task_priority = runtime->config.task_priority;
    status->task_watchdog_enabled = runtime->config.task_watchdog;
    status->task_watchdog_registered = runtime->watchdog_registered;
    runtime_copy_string(status->startup_script,
                        sizeof(status->startup_script),
                        runtime->startup_script,
                        "index.js");
    status->autorun_startup_script = runtime->config.autorun_startup_script;
    status->repl_enabled = runtime->config.enable_repl;
    status->mount_littlefs = runtime->config.mount_littlefs;
    status->require_littlefs = runtime->config.require_littlefs;
    status->format_littlefs_on_mount_fail =
        runtime->config.format_littlefs_on_mount_fail;
    status->littlefs_mounted = runtime->littlefs_mounted;
    runtime_copy_string(status->fs_root,
                        sizeof(status->fs_root),
                        runtime->engine.fs_root,
                        ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    status->mount_secondary_littlefs = runtime->config.mount_secondary_littlefs;
    status->require_secondary_littlefs = runtime->config.require_secondary_littlefs;
    status->secondary_littlefs_mounted = runtime->secondary_littlefs_mounted;
    runtime_copy_string(status->secondary_partition,
                        sizeof(status->secondary_partition),
                        runtime->secondary_littlefs_partition_label,
                        "data");
    runtime_copy_string(status->secondary_root,
                        sizeof(status->secondary_root),
                        runtime->secondary_littlefs_base_path,
                        "/data");
    status->restart_runtime_available = true;
    status->reboot_available = true;
    status->restart_timeout_ms = runtime->config.restart_timeout_ms;
    status->restart_failure_action = runtime->config.restart_failure_action;
    status->software_reason_available = runtime->software_reason_available;
    runtime_copy_string(status->software_reason,
                        sizeof(status->software_reason),
                        runtime->software_reason,
                        NULL);
    return true;
}

static esp32_mquickjs_control_result_t runtime_request_control_hook(
    void *opaque,
    esp32_mquickjs_control_action_t action,
    const char *reason,
    uint32_t delay_ms,
    esp32_mquickjs_control_receipt_t *receipt)
{
    esp32qjs_runtime_t *runtime = opaque;
    uint64_t requested_at_ms = (uint64_t)esp_timer_get_time() / 1000ULL;

    if (runtime == NULL || !runtime_reason_is_valid(reason) || receipt == NULL ||
        delay_ms > 60000U ||
        (action != ESP32_MQUICKJS_CONTROL_RESTART_RUNTIME &&
         action != ESP32_MQUICKJS_CONTROL_REBOOT)) {
        return ESP32_MQUICKJS_CONTROL_INVALID_STATE;
    }
    portENTER_CRITICAL(&runtime->lifecycle_lock);
    if (runtime->pending_control) {
        portEXIT_CRITICAL(&runtime->lifecycle_lock);
        return ESP32_MQUICKJS_CONTROL_ALREADY_PENDING;
    }
    if (runtime->state != ESP32_MQUICKJS_RUNTIME_RUNNING &&
        runtime->state != ESP32_MQUICKJS_RUNTIME_STARTING) {
        portEXIT_CRITICAL(&runtime->lifecycle_lock);
        return ESP32_MQUICKJS_CONTROL_INVALID_STATE;
    }
    runtime->pending_control = true;
    runtime->pending_action = action;
    runtime_copy_string(runtime->pending_reason,
                        sizeof(runtime->pending_reason),
                        reason,
                        "javascript");
    runtime->pending_requested_at_ms = requested_at_ms;
    runtime->pending_due_at_ms = requested_at_ms + delay_ms;
    receipt->action = action;
    receipt->generation = runtime->generation;
    receipt->requested_at_ms = requested_at_ms;
    receipt->due_at_ms = runtime->pending_due_at_ms;
    portEXIT_CRITICAL(&runtime->lifecycle_lock);
    esp32_mquickjs_notify_activity(&runtime->engine);
    return ESP32_MQUICKJS_CONTROL_ACCEPTED;
}

void esp32qjs_runtime_default_config(esp32qjs_runtime_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->js_heap_size = (size_t)CONFIG_ESP32QJS_JS_HEAP_SIZE;
    config->eval_timeout_ms = ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS;
    config->task_stack_size = (uint32_t)CONFIG_ESP32QJS_RUNTIME_TASK_STACK_SIZE;
    config->task_priority = (uint32_t)CONFIG_ESP32QJS_RUNTIME_TASK_PRIORITY;
    config->stop_timeout_ms = (uint32_t)CONFIG_ESP32QJS_RUNTIME_STOP_TIMEOUT_MS;
    config->restart_timeout_ms =
        (uint32_t)CONFIG_ESP32QJS_RUNTIME_RESTART_TIMEOUT_MS;
#ifdef CONFIG_ESP32QJS_RUNTIME_RESTART_FAILURE_STOP
    config->restart_failure_action = ESP32_MQUICKJS_RESTART_FAILURE_STOP;
#else
    config->restart_failure_action = ESP32_MQUICKJS_RESTART_FAILURE_REBOOT;
#endif
#ifdef CONFIG_ESP32QJS_JS_HEAP_PREFER_PSRAM
    config->prefer_psram = true;
#endif
#ifdef CONFIG_ESP32_MQUICKJS_FEATURE_FS
    config->mount_littlefs = true;
#endif
    config->require_littlefs = false;
#ifdef CONFIG_ESP32QJS_SECONDARY_LITTLEFS
    config->require_littlefs = true;
    config->mount_secondary_littlefs = true;
    config->require_secondary_littlefs = true;
    config->secondary_littlefs_partition_label =
        CONFIG_ESP32QJS_SECONDARY_LITTLEFS_PARTITION_LABEL;
    config->secondary_littlefs_base_path =
        CONFIG_ESP32QJS_SECONDARY_LITTLEFS_BASE_PATH;
#endif
#ifdef CONFIG_ESP32QJS_LITTLEFS_FORMAT_ON_MOUNT_FAIL
    config->format_littlefs_on_mount_fail = true;
#endif
#ifdef CONFIG_ESP32QJS_AUTORUN_INDEX_JS
    config->autorun_startup_script = true;
#endif
#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    config->enable_repl = true;
#endif
#ifdef CONFIG_ESP32QJS_RUNTIME_TASK_WATCHDOG
    config->task_watchdog = true;
#endif
    config->startup_script = "index.js";
    config->task_name = "js_runtime";
}

static bool runtime_release_unstarted(esp32qjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return false;
    }
    if (runtime->ctx != NULL) {
        if (!esp32_mquickjs_destroy(runtime->ctx, &runtime->engine)) {
            return false;
        }
        runtime->ctx = NULL;
    }
    esp32_mquickjs_release_persistent_state(&runtime->engine);
#if CONFIG_ESP32QJS_ENABLE_REPL
    if (runtime->config.enable_repl) {
        esp32qjs_interactive_uninstall_log_bridge();
        esp32qjs_interactive_console_init(NULL);
    }
#endif
    if (runtime->secondary_littlefs_mounted) {
        esp32_mquickjs_unmount_littlefs_partition(
            runtime->secondary_littlefs_partition_label);
        runtime->secondary_littlefs_mounted = false;
    }
    if (runtime->littlefs_mounted) {
        esp32_mquickjs_unmount_littlefs();
        runtime->littlefs_mounted = false;
    }
    heap_caps_free(runtime->js_heap);
    runtime->js_heap = NULL;
    if (runtime->stopped != NULL) {
        vSemaphoreDelete(runtime->stopped);
    }
    heap_caps_free(runtime);
    return true;
}

static bool runtime_startup_path_is_safe(const char *path)
{
    const unsigned char *cursor = (const unsigned char *)path;

    if (path == NULL || path[0] == '\0' || path[0] == '/') {
        return false;
    }
    while (*cursor != '\0') {
        if (!( (*cursor >= 'a' && *cursor <= 'z') ||
               (*cursor >= 'A' && *cursor <= 'Z') ||
               (*cursor >= '0' && *cursor <= '9') ||
               *cursor == '/' || *cursor == '_' || *cursor == '-' || *cursor == '.')) {
            return false;
        }
        cursor++;
    }
    return strstr(path, "..") == NULL;
}

static void runtime_run_startup(esp32qjs_runtime_t *runtime)
{
    JSValue result;

    if (runtime == NULL || runtime->ctx == NULL ||
        !runtime->config.autorun_startup_script ||
        !runtime->littlefs_mounted) {
        return;
    }
    if (!runtime_startup_path_is_safe(runtime->startup_script)) {
        ESP_LOGE(TAG, "Unsafe startup script path: %s", runtime->startup_script);
        return;
    }

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
    result = esp32_mquickjs_load_startup_from_active_fs(runtime->ctx,
                                                        &runtime->engine,
                                                        runtime->startup_script);
#else
    return;
#endif
    if (JS_IsException(result)) {
        esp32_mquickjs_print_exception(runtime->ctx);
    }
}

static bool runtime_create_generation(esp32qjs_runtime_t *runtime,
                                      const char *active_fs_root)
{
    if (runtime == NULL || runtime->ctx != NULL) {
        return false;
    }
    runtime->ctx = esp32_mquickjs_create(runtime->js_heap,
                                          runtime->config.js_heap_size,
                                          &runtime->engine,
                                          runtime->config.eval_timeout_ms);
    if (runtime->ctx == NULL) {
        return false;
    }
    esp32_mquickjs_set_system_hooks(&runtime->engine,
                                     runtime_host_status,
                                     runtime_request_control_hook,
                                     runtime);
    esp32_mquickjs_set_cooperate_hook(&runtime->engine, runtime_cooperate, runtime);
    runtime->engine.js_heap_size = runtime->config.js_heap_size;
    runtime->engine.js_heap_in_psram = esp_ptr_external_ram(runtime->js_heap);
    runtime->engine.repl_enabled = runtime->config.enable_repl;
    runtime->engine.auto_run_startup_script =
        runtime->config.autorun_startup_script;
    runtime->engine.format_littlefs_on_mount_fail =
        runtime->config.format_littlefs_on_mount_fail;
    runtime->engine.littlefs_mounted = runtime->littlefs_mounted;
    runtime_copy_string(runtime->engine.startup_fs_root,
                        sizeof(runtime->engine.startup_fs_root),
                        ESP32_MQUICKJS_LITTLEFS_BASE_PATH,
                        ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    if (active_fs_root != NULL && active_fs_root[0] != '\0') {
        runtime_copy_string(runtime->engine.fs_root,
                            sizeof(runtime->engine.fs_root),
                            active_fs_root,
                            ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    }

#if CONFIG_ESP32QJS_ENABLE_REPL
    if (runtime->config.enable_repl) {
        runtime->engine.prepare_output = esp32qjs_interactive_prepare_output;
        runtime->engine.prepare_output_opaque = NULL;
    }
#endif
    if (!esp32_mquickjs_install_globals(runtime->ctx, &runtime->engine)) {
        if (esp32_mquickjs_destroy_generation(runtime->ctx, &runtime->engine)) {
            runtime->ctx = NULL;
        }
        return false;
    }
    if (runtime->config.install_globals != NULL &&
        !runtime->config.install_globals(runtime->ctx,
                                         &runtime->engine,
                                         runtime->config.opaque)) {
        ESP_LOGE(TAG, "Application global installer failed");
        if (esp32_mquickjs_destroy_generation(runtime->ctx, &runtime->engine)) {
            runtime->ctx = NULL;
        }
        return false;
    }
    runtime->generation_started_us = (uint64_t)esp_timer_get_time();
    return true;
}

static void runtime_attach_current_task(void *opaque)
{
    esp32qjs_runtime_t *runtime = opaque;

    if (runtime != NULL) {
        esp32_mquickjs_attach_current_task(&runtime->engine);
    }
}

#if CONFIG_ESP32QJS_ENABLE_REPL
static uint32_t runtime_output_generation(void *opaque)
{
    esp32qjs_runtime_t *runtime = opaque;

    return runtime != NULL ? runtime->engine.output_generation : 0;
}

static void runtime_startup_callback(void *opaque)
{
    esp32qjs_runtime_t *runtime = opaque;

    runtime_run_startup(runtime);
    runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_RUNNING);
}
#endif

static bool runtime_cooperate(void *opaque)
{
    esp32qjs_runtime_t *runtime = opaque;

    if (runtime == NULL) {
        return false;
    }
    if (runtime->watchdog_registered) {
        esp_task_wdt_reset();
    }
    return !runtime->stop_requested && !runtime_control_due(runtime);
}

static esp32_mquickjs_poll_result_t runtime_poll_engine(esp32qjs_runtime_t *runtime)
{
    esp32_mquickjs_poll_result_t result;

    if (runtime == NULL || runtime->ctx == NULL) {
        return ESP32_MQUICKJS_POLL_NONE;
    }
    if (runtime->watchdog_registered) {
        esp_task_wdt_reset();
    }
    result = esp32_mquickjs_poll(runtime->ctx, &runtime->engine);
    if (runtime->watchdog_registered) {
        esp_task_wdt_reset();
    }
    return result;
}

#if CONFIG_ESP32QJS_ENABLE_REPL
static esp32qjs_interactive_poll_result_t runtime_interactive_poll(void *opaque)
{
    return (esp32qjs_interactive_poll_result_t)runtime_poll_engine(opaque);
}

static void runtime_handle_line(void *opaque, const char *line)
{
    esp32qjs_runtime_t *runtime = opaque;
    JSValue result;

    if (runtime == NULL || runtime->ctx == NULL) {
        return;
    }
    result = esp32_mquickjs_eval(runtime->ctx,
                                 &runtime->engine,
                                 line,
                                 "<repl>",
                                 JS_EVAL_REPL | JS_EVAL_RETVAL);
    if (JS_IsException(result)) {
        esp32_mquickjs_print_exception(runtime->ctx);
    } else if (!JS_IsUndefined(result)) {
        JS_PrintValueF(runtime->ctx, result, JS_DUMP_LONG);
        printf("\n");
    }
}
#endif

static bool runtime_wait_for_activity(void *opaque, uint32_t timeout_ms)
{
    esp32qjs_runtime_t *runtime = opaque;

    return runtime != NULL &&
           esp32_mquickjs_wait_for_activity(
               &runtime->engine,
               runtime_control_wait_ms(runtime, timeout_ms));
}

#if CONFIG_ESP32QJS_ENABLE_REPL
static bool runtime_should_stop(void *opaque)
{
    esp32qjs_runtime_t *runtime = opaque;

    return runtime == NULL || runtime->stop_requested || runtime_control_due(runtime);
}

static void runtime_notify_activity(void *opaque)
{
    esp32qjs_runtime_t *runtime = opaque;

    if (runtime != NULL) {
        esp32_mquickjs_notify_activity(&runtime->engine);
    }
}

static void runtime_notify_activity_from_isr(void *opaque, int *task_woken)
{
    (void)opaque;
    esp32_mquickjs_notify_active_runtime_from_isr(task_woken);
}
#endif

static bool runtime_take_due_control(esp32qjs_runtime_t *runtime,
                                     esp32_mquickjs_control_action_t *action,
                                     char reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U])
{
    uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    bool taken = false;

    if (runtime == NULL || action == NULL || reason == NULL) {
        return false;
    }
    portENTER_CRITICAL(&runtime->lifecycle_lock);
    if (runtime->pending_control && now_ms >= runtime->pending_due_at_ms) {
        *action = runtime->pending_action;
        runtime_copy_string(reason,
                            ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U,
                            runtime->pending_reason,
                            "javascript");
        runtime->pending_control = false;
        runtime->pending_reason[0] = '\0';
        runtime->pending_requested_at_ms = 0;
        runtime->pending_due_at_ms = 0;
        taken = true;
    }
    portEXIT_CRITICAL(&runtime->lifecycle_lock);
    return taken;
}

static bool runtime_destroy_generation(esp32qjs_runtime_t *runtime,
                                       uint64_t deadline_us)
{
    if (runtime == NULL || runtime->ctx == NULL) {
        return true;
    }
    esp32_mquickjs_detach_current_task(&runtime->engine);
    for (;;) {
        if (esp32_mquickjs_destroy_generation(runtime->ctx, &runtime->engine)) {
            runtime->ctx = NULL;
            return true;
        }
        if ((uint64_t)esp_timer_get_time() >= deadline_us) {
            return false;
        }
        if (runtime->watchdog_registered) {
            esp_task_wdt_reset();
        }
        vTaskDelay(1);
    }
}

static bool runtime_restart_generation(esp32qjs_runtime_t *runtime,
                                       const char *reason)
{
    char active_fs_root[ESP32_MQUICKJS_FS_ROOT_MAX];
    char previous_reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];
    uint32_t previous_generation;
    uint32_t previous_restart_count;
    uint64_t deadline_us;

    if (runtime == NULL || reason == NULL) {
        return false;
    }
    runtime_copy_string(active_fs_root,
                        sizeof(active_fs_root),
                        runtime->engine.fs_root,
                        ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_QUIESCING);
    deadline_us = (uint64_t)esp_timer_get_time() +
                  ((uint64_t)runtime->config.restart_timeout_ms * 1000ULL);
    if (!runtime_destroy_generation(runtime, deadline_us)) {
        return false;
    }

    runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_RESTARTING);
    portENTER_CRITICAL(&runtime->lifecycle_lock);
    previous_generation = runtime->generation;
    previous_restart_count = runtime->restart_count;
    runtime_copy_string(previous_reason,
                        sizeof(previous_reason),
                        runtime->last_restart_reason,
                        NULL);
    runtime->generation++;
    runtime->restart_count++;
    runtime_copy_string(runtime->last_restart_reason,
                        sizeof(runtime->last_restart_reason),
                        reason,
                        "javascript");
    portEXIT_CRITICAL(&runtime->lifecycle_lock);

    runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_STARTING);
    if (!runtime_create_generation(runtime, active_fs_root)) {
        portENTER_CRITICAL(&runtime->lifecycle_lock);
        runtime->generation = previous_generation;
        runtime->restart_count = previous_restart_count;
        runtime_copy_string(runtime->last_restart_reason,
                            sizeof(runtime->last_restart_reason),
                            previous_reason,
                            NULL);
        portEXIT_CRITICAL(&runtime->lifecycle_lock);
        return false;
    }
    runtime_attach_current_task(runtime);
    ESP_LOGI(TAG,
             "JavaScript runtime generation %lu ready (restartCount=%lu, reason=%s)",
             (unsigned long)runtime->generation,
             (unsigned long)runtime->restart_count,
             reason);
    return true;
}

static bool runtime_handle_due_control(esp32qjs_runtime_t *runtime)
{
    esp32_mquickjs_control_action_t action;
    char reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];

    if (!runtime_take_due_control(runtime, &action, reason)) {
        return true;
    }
    if (action == ESP32_MQUICKJS_CONTROL_REBOOT) {
        runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_STOPPING);
        runtime_store_software_reason(reason);
        ESP_LOGW(TAG, "software reboot requested: %s", reason);
        esp_restart();
        return false;
    }
    ESP_LOGI(TAG, "restarting JavaScript runtime: %s", reason);
    runtime_store_software_reason(reason);
    if (runtime_restart_generation(runtime, reason)) {
        runtime_clear_software_reason();
        return true;
    }

    ESP_LOGE(TAG, "JavaScript runtime restart failed");
    runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_FAILED);
    if (runtime->config.restart_failure_action ==
        ESP32_MQUICKJS_RESTART_FAILURE_REBOOT) {
        runtime_store_software_reason("runtime-restart-failed");
        esp_restart();
    } else {
        runtime_clear_software_reason();
    }
    return false;
}

static void runtime_task(void *opaque)
{
    esp32qjs_runtime_t *runtime = opaque;
    bool failed = false;

    runtime->running = true;
    runtime_attach_current_task(runtime);
    if (runtime->config.task_watchdog && esp_task_wdt_add(NULL) == ESP_OK) {
        runtime->watchdog_registered = true;
    }

    while (!runtime->stop_requested) {
        if (!runtime_handle_due_control(runtime)) {
            failed = runtime->state == ESP32_MQUICKJS_RUNTIME_FAILED;
            break;
        }
#if CONFIG_ESP32QJS_ENABLE_REPL
        if (runtime->config.enable_repl) {
            esp32qjs_interactive_run(&runtime->interactive_host, &runtime->banner);
        } else
#endif
        {
            runtime_run_startup(runtime);
            runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_RUNNING);
            while (!runtime->stop_requested && !runtime_control_due(runtime)) {
                if (runtime_poll_engine(runtime) == ESP32_MQUICKJS_POLL_NONE) {
                    runtime_wait_for_activity(runtime, UINT32_MAX);
                }
            }
        }
    }

    if (runtime->watchdog_registered) {
        esp_task_wdt_delete(NULL);
        runtime->watchdog_registered = false;
    }
#if CONFIG_ESP32QJS_ENABLE_REPL
    if (runtime->config.enable_repl) {
        esp32qjs_interactive_uninstall_log_bridge();
    }
#endif
    esp32_mquickjs_detach_current_task(&runtime->engine);
    runtime->running = false;
    runtime->task = NULL;
    if (!failed) {
        runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_STOPPED);
    }
    xSemaphoreGive(runtime->stopped);
    vTaskDelete(NULL);
}

esp_err_t esp32qjs_runtime_create(const esp32qjs_runtime_config_t *config,
                                  esp32qjs_runtime_t **out_runtime)
{
    esp32qjs_runtime_t *runtime;

    if (config == NULL || out_runtime == NULL || config->js_heap_size == 0 ||
        config->task_stack_size == 0 || config->task_priority == 0 ||
        config->restart_timeout_ms == 0 ||
        (config->restart_failure_action != ESP32_MQUICKJS_RESTART_FAILURE_REBOOT &&
         config->restart_failure_action != ESP32_MQUICKJS_RESTART_FAILURE_STOP)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_runtime = NULL;
    if (s_active_runtime != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
#if !CONFIG_ESP32QJS_ENABLE_REPL
    if (config->enable_repl) {
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif

    runtime = heap_caps_calloc(1, sizeof(*runtime), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (runtime == NULL) {
        return ESP_ERR_NO_MEM;
    }
    runtime->config = *config;
    runtime->lifecycle_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    runtime->state = ESP32_MQUICKJS_RUNTIME_CREATED;
    runtime->generation = 1U;
    runtime_load_software_reason(runtime);
    runtime_copy_string(runtime->startup_script,
                        sizeof(runtime->startup_script),
                        config->startup_script,
                        "index.js");
    runtime_copy_string(runtime->task_name,
                        sizeof(runtime->task_name),
                        config->task_name,
                        "js_runtime");
    runtime_copy_string(runtime->secondary_littlefs_partition_label,
                        sizeof(runtime->secondary_littlefs_partition_label),
                        config->secondary_littlefs_partition_label,
                        "data");
    runtime_copy_string(runtime->secondary_littlefs_base_path,
                        sizeof(runtime->secondary_littlefs_base_path),
                        config->secondary_littlefs_base_path,
                        "/data");
    runtime->config.startup_script = runtime->startup_script;
    runtime->config.task_name = runtime->task_name;
    runtime->config.secondary_littlefs_partition_label =
        runtime->secondary_littlefs_partition_label;
    runtime->config.secondary_littlefs_base_path =
        runtime->secondary_littlefs_base_path;
    runtime->stopped = xSemaphoreCreateBinary();
    if (runtime->stopped == NULL) {
        runtime_release_unstarted(runtime);
        return ESP_ERR_NO_MEM;
    }

    if (config->prefer_psram) {
        runtime->js_heap = heap_caps_malloc_prefer(config->js_heap_size,
                                                    2,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    } else {
        runtime->js_heap = heap_caps_malloc(config->js_heap_size,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (runtime->js_heap == NULL) {
        runtime_release_unstarted(runtime);
        return ESP_ERR_NO_MEM;
    }

    if (config->mount_littlefs) {
        runtime->littlefs_mounted =
            esp32_mquickjs_mount_littlefs(config->format_littlefs_on_mount_fail);
        if (!runtime->littlefs_mounted && config->require_littlefs) {
            runtime_release_unstarted(runtime);
            return ESP_FAIL;
        }
    }
    if (config->mount_secondary_littlefs) {
        runtime->secondary_littlefs_mounted =
            esp32_mquickjs_mount_littlefs_partition(
                runtime->secondary_littlefs_partition_label,
                runtime->secondary_littlefs_base_path,
                false);
        if (!runtime->secondary_littlefs_mounted &&
            config->require_secondary_littlefs) {
            runtime_release_unstarted(runtime);
            return ESP_FAIL;
        }
    }

    runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_STARTING);
    if (!runtime_create_generation(runtime, ESP32_MQUICKJS_LITTLEFS_BASE_PATH)) {
        runtime_release_unstarted(runtime);
        return ESP_FAIL;
    }

#if CONFIG_ESP32QJS_ENABLE_REPL
    runtime->interactive_host.attach_current_task = runtime_attach_current_task;
    runtime->interactive_host.output_generation = runtime_output_generation;
    runtime->interactive_host.run_startup = runtime_startup_callback;
    runtime->interactive_host.poll = runtime_interactive_poll;
    runtime->interactive_host.handle_line = runtime_handle_line;
    runtime->interactive_host.wait_for_activity = runtime_wait_for_activity;
    runtime->interactive_host.should_stop = runtime_should_stop;
    runtime->interactive_host.notify_activity = runtime_notify_activity;
    runtime->interactive_host.notify_activity_from_isr = runtime_notify_activity_from_isr;
    runtime->interactive_host.opaque = runtime;
    runtime->banner.title = "mquickjs REPL on " CONFIG_ESP32_MQUICKJS_MCU_NAME;
    runtime->banner.subtitle = "Type JavaScript and press Enter.";
    runtime->banner.hint = "Run help() for usage.";
    if (config->enable_repl) {
        runtime->engine.prepare_output = esp32qjs_interactive_prepare_output;
        runtime->engine.prepare_output_opaque = NULL;
        esp32qjs_interactive_console_init(&runtime->interactive_host);
        esp32qjs_interactive_install_log_bridge(&runtime->interactive_host);
    }
#endif

    ESP_LOGI(TAG,
             "runtime ready: js_heap=%u bytes (%s), free_heap=%u bytes",
             (unsigned)config->js_heap_size,
             runtime->engine.js_heap_in_psram ? "psram" : "internal",
             (unsigned)esp_get_free_heap_size());
    s_active_runtime = runtime;
    *out_runtime = runtime;
    return ESP_OK;
}

esp_err_t esp32qjs_runtime_start(esp32qjs_runtime_t *runtime)
{
    BaseType_t created;

    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (runtime->task != NULL || runtime->running) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime->stop_requested = false;
    runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_STARTING);
    xSemaphoreTake(runtime->stopped, 0);
    created = xTaskCreate(runtime_task,
                          runtime->task_name,
                          runtime->config.task_stack_size,
                          runtime,
                          runtime->config.task_priority,
                          &runtime->task);
    if (created != pdPASS) {
        runtime->task = NULL;
        runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_CREATED);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void esp32qjs_runtime_request_stop(esp32qjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    if (runtime->state != ESP32_MQUICKJS_RUNTIME_FAILED) {
        runtime_set_state(runtime, ESP32_MQUICKJS_RUNTIME_STOPPING);
    }
    runtime->stop_requested = true;
    esp32_mquickjs_notify_activity(&runtime->engine);
}

esp_err_t esp32qjs_runtime_request_control(
    esp32qjs_runtime_t *runtime,
    esp32_mquickjs_control_action_t action,
    const char *reason,
    uint32_t delay_ms,
    esp32_mquickjs_control_receipt_t *receipt)
{
    esp32_mquickjs_control_result_t result;

    result = runtime_request_control_hook(runtime,
                                          action,
                                          reason,
                                          delay_ms,
                                          receipt);
    switch (result) {
    case ESP32_MQUICKJS_CONTROL_ACCEPTED:
        return ESP_OK;
    case ESP32_MQUICKJS_CONTROL_ALREADY_PENDING:
    case ESP32_MQUICKJS_CONTROL_INVALID_STATE:
        return ESP_ERR_INVALID_STATE;
    case ESP32_MQUICKJS_CONTROL_UNAVAILABLE:
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t esp32qjs_runtime_stop(esp32qjs_runtime_t *runtime,
                                uint32_t timeout_ms)
{
    TickType_t timeout_ticks;

    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (runtime->task == NULL && !runtime->running) {
        return ESP_OK;
    }
    if (xTaskGetCurrentTaskHandle() == runtime->task) {
        return ESP_ERR_INVALID_STATE;
    }
    esp32qjs_runtime_request_stop(runtime);
    if (timeout_ms == 0) {
        timeout_ms = runtime->config.stop_timeout_ms;
    }
    timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ticks == 0) {
        timeout_ticks = 1;
    }
    return xSemaphoreTake(runtime->stopped, timeout_ticks) == pdTRUE
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

esp_err_t esp32qjs_runtime_destroy(esp32qjs_runtime_t *runtime)
{
    bool was_active;

    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (runtime->task != NULL || runtime->running) {
        return ESP_ERR_INVALID_STATE;
    }
    was_active = s_active_runtime == runtime;
    if (!runtime_release_unstarted(runtime)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (was_active) {
        s_active_runtime = NULL;
    }
    return ESP_OK;
}

bool esp32qjs_runtime_is_running(const esp32qjs_runtime_t *runtime)
{
    return runtime != NULL && runtime->running;
}

JSContext *esp32qjs_runtime_context(esp32qjs_runtime_t *runtime)
{
    return runtime != NULL ? runtime->ctx : NULL;
}

esp32_mquickjs_runtime_t *esp32qjs_runtime_engine(esp32qjs_runtime_t *runtime)
{
    return runtime != NULL ? &runtime->engine : NULL;
}
