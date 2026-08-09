#include "esp32qjs_runtime.h"

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if CONFIG_ESP32QJS_ENABLE_REPL
#include "esp32qjs_interactive.h"
#endif

#define ESP32QJS_RUNTIME_STARTUP_PATH_MAX 256
#define ESP32QJS_RUNTIME_TASK_NAME_MAX 24

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
    bool littlefs_mounted;
    bool watchdog_registered;
    char startup_script[ESP32QJS_RUNTIME_STARTUP_PATH_MAX];
    char task_name[ESP32QJS_RUNTIME_TASK_NAME_MAX];
#if CONFIG_ESP32QJS_ENABLE_REPL
    esp32qjs_interactive_host_t interactive_host;
    esp32qjs_interactive_banner_t banner;
#endif
};

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
#ifdef CONFIG_ESP32QJS_JS_HEAP_PREFER_PSRAM
    config->prefer_psram = true;
#endif
#ifdef CONFIG_ESP32_MQUICKJS_FEATURE_FS
    config->mount_littlefs = true;
#endif
    config->require_littlefs = false;
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
#if CONFIG_ESP32QJS_ENABLE_REPL
    if (runtime->config.enable_repl) {
        esp32qjs_interactive_uninstall_log_bridge();
        esp32qjs_interactive_console_init(NULL);
    }
#endif
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
    char command[ESP32QJS_RUNTIME_STARTUP_PATH_MAX + 16];
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

    snprintf(command, sizeof(command), "load('%s')", runtime->startup_script);
    result = esp32_mquickjs_eval(runtime->ctx,
                                 &runtime->engine,
                                 command,
                                 "<startup>",
                                 0);
    if (JS_IsException(result)) {
        esp32_mquickjs_print_exception(runtime->ctx);
    }
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
    runtime_run_startup(opaque);
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
    return !runtime->stop_requested;
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

    return runtime != NULL && esp32_mquickjs_wait_for_activity(&runtime->engine, timeout_ms);
}

#if CONFIG_ESP32QJS_ENABLE_REPL
static bool runtime_should_stop(void *opaque)
{
    esp32qjs_runtime_t *runtime = opaque;

    return runtime == NULL || runtime->stop_requested;
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

static void runtime_task(void *opaque)
{
    esp32qjs_runtime_t *runtime = opaque;

    runtime->running = true;
    runtime_attach_current_task(runtime);
    if (runtime->config.task_watchdog && esp_task_wdt_add(NULL) == ESP_OK) {
        runtime->watchdog_registered = true;
    }

#if CONFIG_ESP32QJS_ENABLE_REPL
    if (runtime->config.enable_repl) {
        esp32qjs_interactive_run(&runtime->interactive_host, &runtime->banner);
    } else
#endif
    {
        runtime_run_startup(runtime);
        while (!runtime->stop_requested) {
            if (runtime_poll_engine(runtime) == ESP32_MQUICKJS_POLL_NONE) {
                runtime_wait_for_activity(runtime, UINT32_MAX);
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
    xSemaphoreGive(runtime->stopped);
    vTaskDelete(NULL);
}

esp_err_t esp32qjs_runtime_create(const esp32qjs_runtime_config_t *config,
                                  esp32qjs_runtime_t **out_runtime)
{
    esp32qjs_runtime_t *runtime;

    if (config == NULL || out_runtime == NULL || config->js_heap_size == 0 ||
        config->task_stack_size == 0 || config->task_priority == 0) {
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
    runtime_copy_string(runtime->startup_script,
                        sizeof(runtime->startup_script),
                        config->startup_script,
                        "index.js");
    runtime_copy_string(runtime->task_name,
                        sizeof(runtime->task_name),
                        config->task_name,
                        "js_runtime");
    runtime->config.startup_script = runtime->startup_script;
    runtime->config.task_name = runtime->task_name;
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

    runtime->ctx = esp32_mquickjs_create(runtime->js_heap,
                                          config->js_heap_size,
                                          &runtime->engine,
                                          config->eval_timeout_ms);
    if (runtime->ctx == NULL) {
        runtime_release_unstarted(runtime);
        return ESP_FAIL;
    }
    esp32_mquickjs_set_cooperate_hook(&runtime->engine, runtime_cooperate, runtime);
    runtime->engine.js_heap_size = config->js_heap_size;
    runtime->engine.js_heap_in_psram = esp_ptr_external_ram(runtime->js_heap);
    runtime->engine.repl_enabled = config->enable_repl;
    runtime->engine.auto_run_startup_script = config->autorun_startup_script;
    runtime->engine.format_littlefs_on_mount_fail = config->format_littlefs_on_mount_fail;

    if (!esp32_mquickjs_install_globals(runtime->ctx, &runtime->engine)) {
        runtime_release_unstarted(runtime);
        return ESP_FAIL;
    }
    if (config->mount_littlefs) {
        runtime->littlefs_mounted =
            esp32_mquickjs_mount_littlefs(config->format_littlefs_on_mount_fail);
        runtime->engine.littlefs_mounted = runtime->littlefs_mounted;
        if (!runtime->littlefs_mounted && config->require_littlefs) {
            runtime_release_unstarted(runtime);
            return ESP_FAIL;
        }
    }
    if (config->install_globals != NULL &&
        !config->install_globals(runtime->ctx, &runtime->engine, config->opaque)) {
        ESP_LOGE(TAG, "Application global installer failed");
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
    runtime->banner.title = "mquickjs REPL on " CONFIG_ESP32_MQUICKJS_BOARD_NAME;
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
    xSemaphoreTake(runtime->stopped, 0);
    created = xTaskCreate(runtime_task,
                          runtime->task_name,
                          runtime->config.task_stack_size,
                          runtime,
                          runtime->config.task_priority,
                          &runtime->task);
    if (created != pdPASS) {
        runtime->task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void esp32qjs_runtime_request_stop(esp32qjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    runtime->stop_requested = true;
    esp32_mquickjs_notify_activity(&runtime->engine);
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
