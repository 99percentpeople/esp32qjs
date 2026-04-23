#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "sdkconfig.h"
#include "esp32_mquickjs.h"
#if CONFIG_ESP32QJS_ENABLE_REPL
#include "esp32qjs_interactive/esp32qjs_interactive.h"
#endif
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "esp32qjs";

#define JS_HEAP_SIZE CONFIG_ESP32QJS_JS_HEAP_SIZE
#if CONFIG_ESP32QJS_ENABLE_REPL
#define JS_REPL_TITLE "mquickjs REPL on " CONFIG_ESP32_MQUICKJS_BOARD_NAME
#endif

static esp32_mquickjs_runtime_t s_js_runtime;
static void *s_js_heap;
static JSContext *s_js_ctx;

typedef struct {
    JSContext *ctx;
    esp32_mquickjs_runtime_t *runtime;
} esp32qjs_js_backend_t;

static esp32qjs_js_backend_t s_js_backend = {
    .ctx = NULL,
    .runtime = &s_js_runtime,
};

#if CONFIG_ESP32QJS_ENABLE_REPL
static const esp32qjs_interactive_banner_t s_js_banner = {
    .title = JS_REPL_TITLE,
    .subtitle = "Type JavaScript and press Enter.",
    .hint = "Run help() for usage.",
};
#endif

static void js_backend_attach_current_task(void *opaque)
{
    esp32qjs_js_backend_t *backend = opaque;

    if (backend->runtime != NULL) {
        esp32_mquickjs_attach_current_task(backend->runtime);
    }
}

#if CONFIG_ESP32QJS_ENABLE_REPL
static uint32_t js_backend_output_generation(void *opaque)
{
    esp32qjs_js_backend_t *backend = opaque;

    if (backend->runtime == NULL) {
        return 0;
    }
    return backend->runtime->output_generation;
}
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
#define STARTUP_SCRIPT_PATH ESP32_MQUICKJS_LITTLEFS_BASE_PATH "/index.js"

static void js_backend_run_optional_script(esp32qjs_js_backend_t *backend,
                                           const char *absolute_path,
                                           const char *relative_path,
                                           const char *source_name)
{
    struct stat st;
    char command[96];
    JSValue result;

    if (backend->ctx == NULL ||
        backend->runtime == NULL ||
        stat(absolute_path, &st) != 0 ||
        !S_ISREG(st.st_mode)) {
        return;
    }

    snprintf(command, sizeof(command), "load('%s')", relative_path);
    result = esp32_mquickjs_eval(backend->ctx, backend->runtime, command, source_name, 0);
    if (JS_IsException(result)) {
        esp32_mquickjs_print_exception(backend->ctx);
    }
}
#endif

static void js_backend_run_startup(void *opaque)
{
#if CONFIG_ESP32_MQUICKJS_FEATURE_FS && CONFIG_ESP32QJS_AUTORUN_INDEX_JS
    js_backend_run_optional_script(opaque, STARTUP_SCRIPT_PATH, "index.js", "<startup>");
#else
    (void)opaque;
#endif
}

#if CONFIG_ESP32QJS_ENABLE_REPL
static esp32qjs_interactive_poll_result_t js_backend_poll(void *opaque)
{
    esp32qjs_js_backend_t *backend = opaque;
    esp32_mquickjs_poll_result_t poll_result;
    esp32qjs_interactive_poll_result_t interactive_result = ESP32QJS_INTERACTIVE_POLL_NONE;

    if (backend->ctx == NULL || backend->runtime == NULL) {
        return ESP32QJS_INTERACTIVE_POLL_NONE;
    }

    poll_result = esp32_mquickjs_poll(backend->ctx, backend->runtime);
    if ((poll_result & ESP32_MQUICKJS_POLL_OUTPUT) != 0) {
        interactive_result |= ESP32QJS_INTERACTIVE_POLL_OUTPUT;
    }
    return interactive_result;
}

static void js_backend_handle_line(void *opaque, const char *line)
{
    esp32qjs_js_backend_t *backend = opaque;
    JSValue result;

    if (backend->ctx == NULL || backend->runtime == NULL) {
        return;
    }

    result = esp32_mquickjs_eval(backend->ctx,
                                 backend->runtime,
                                 line,
                                 "<repl>",
                                 JS_EVAL_REPL | JS_EVAL_RETVAL);

    if (JS_IsException(result)) {
        esp32_mquickjs_print_exception(backend->ctx);
    } else if (!JS_IsUndefined(result)) {
        JS_PrintValueF(backend->ctx, result, JS_DUMP_LONG);
        printf("\n");
    }
}
#endif

#if CONFIG_ESP32QJS_ENABLE_REPL
static bool js_backend_wait_for_activity(void *opaque, uint32_t timeout_ms)
{
    esp32qjs_js_backend_t *backend = opaque;

    if (backend->runtime == NULL) {
        return false;
    }
    return esp32_mquickjs_wait_for_activity(backend->runtime, timeout_ms);
}

static void js_backend_notify_activity(void *opaque)
{
    esp32qjs_js_backend_t *backend = opaque;

    if (backend->runtime != NULL) {
        esp32_mquickjs_notify_activity(backend->runtime);
    }
}

static void js_backend_notify_activity_from_isr(void *opaque, int *task_woken)
{
    (void)opaque;
    esp32_mquickjs_notify_active_runtime_from_isr(task_woken);
}
#endif

#if CONFIG_ESP32QJS_ENABLE_REPL
static const esp32qjs_interactive_host_t s_interactive_host = {
    .attach_current_task = js_backend_attach_current_task,
    .output_generation = js_backend_output_generation,
    .run_startup = js_backend_run_startup,
    .poll = js_backend_poll,
    .handle_line = js_backend_handle_line,
    .wait_for_activity = js_backend_wait_for_activity,
    .notify_activity = js_backend_notify_activity,
    .notify_activity_from_isr = js_backend_notify_activity_from_isr,
    .opaque = &s_js_backend,
};
#endif

#if !CONFIG_ESP32QJS_ENABLE_REPL
static void js_backend_run_headless(esp32qjs_js_backend_t *backend)
{
    js_backend_attach_current_task(backend);
    js_backend_run_startup(backend);

    while (true) {
        if (backend->ctx == NULL || backend->runtime == NULL) {
            vTaskDelay(portMAX_DELAY);
            continue;
        }

        if (esp32_mquickjs_poll(backend->ctx, backend->runtime) == ESP32_MQUICKJS_POLL_NONE) {
            esp32_mquickjs_wait_for_activity(backend->runtime, UINT32_MAX);
        }
    }
}
#endif

static void esp32qjs_runtime_task(void *opaque)
{
    (void)opaque;
#if CONFIG_ESP32QJS_ENABLE_REPL
    esp32qjs_interactive_run(&s_interactive_host, &s_js_banner);
#else
    js_backend_run_headless(&s_js_backend);
#endif
    vTaskDelete(NULL);
}

void app_main(void)
{
    UBaseType_t task_priority;
    const char *js_heap_region;
    bool littlefs_mounted;

#if CONFIG_ESP32QJS_ENABLE_REPL
    esp32qjs_interactive_console_init(&s_interactive_host);
#endif
#ifdef CONFIG_ESP32QJS_JS_HEAP_PREFER_PSRAM
    s_js_heap = heap_caps_malloc_prefer(JS_HEAP_SIZE,
                                        2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    s_js_heap = heap_caps_malloc(JS_HEAP_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    if (s_js_heap == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for JS heap", (unsigned)JS_HEAP_SIZE);
        return;
    }
    s_js_runtime.js_heap_size = JS_HEAP_SIZE;
    s_js_runtime.js_heap_in_psram = esp_ptr_external_ram(s_js_heap);
#if CONFIG_ESP32QJS_ENABLE_REPL
    s_js_runtime.prepare_output = esp32qjs_interactive_prepare_output;
#else
    s_js_runtime.prepare_output = NULL;
#endif
    s_js_runtime.prepare_output_opaque = NULL;

    s_js_ctx = esp32_mquickjs_create(s_js_heap,
                                     JS_HEAP_SIZE,
                                     &s_js_runtime,
                                     ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS);
    if (s_js_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to initialize mquickjs");
        heap_caps_free(s_js_heap);
        s_js_heap = NULL;
        return;
    }
    s_js_backend.ctx = s_js_ctx;

    if (!esp32_mquickjs_install_globals(s_js_ctx, &s_js_runtime)) {
        ESP_LOGE(TAG, "Failed to install ESP32 host globals");
        heap_caps_free(s_js_heap);
        s_js_heap = NULL;
        return;
    }

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
    littlefs_mounted = esp32_mquickjs_mount_littlefs(CONFIG_ESP32QJS_LITTLEFS_FORMAT_ON_MOUNT_FAIL);
    s_js_runtime.littlefs_mounted = littlefs_mounted;
    if (!littlefs_mounted) {
        ESP_LOGW(TAG, "Continuing without LittleFS-backed script loading");
    }
#else
    littlefs_mounted = false;
    s_js_runtime.littlefs_mounted = false;
#endif

#if CONFIG_ESP32QJS_ENABLE_REPL
    esp32qjs_interactive_install_log_bridge(&s_interactive_host);
#endif

    ESP_LOGI(TAG, "mquickjs runtime ready");
    js_heap_region = s_js_runtime.js_heap_in_psram ? "psram" : "internal";
    ESP_LOGI(TAG, "js_heap=%u bytes (%s), free_heap=%u bytes",
             (unsigned)JS_HEAP_SIZE,
             js_heap_region,
             (unsigned)esp_get_free_heap_size());

    task_priority = uxTaskPriorityGet(NULL);
    if (xTaskCreate(esp32qjs_runtime_task,
                    "js_runtime",
                    (uint32_t)CONFIG_ESP32QJS_REPL_TASK_STACK_SIZE,
                    NULL,
                    task_priority,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start js_runtime task");
        heap_caps_free(s_js_heap);
        s_js_heap = NULL;
        return;
    }

    vTaskDelete(NULL);
}
