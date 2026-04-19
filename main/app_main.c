#include <stdio.h>

#include "esp32_mquickjs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "repl.h"
#include "repl_input.h"

static const char *TAG = "esp32qjs";

#define JS_HEAP_SIZE (160 * 1024)
#define JS_REPL_TASK_STACK_SIZE 8192

static esp32_mquickjs_runtime_t s_js_runtime;
static void *s_js_heap;
static JSContext *s_js_ctx;

static void esp32qjs_repl_task(void *opaque)
{
    (void)opaque;
    esp32qjs_repl_run(s_js_ctx, &s_js_runtime);
    vTaskDelete(NULL);
}

void app_main(void)
{
    UBaseType_t task_priority;

    esp32qjs_console_init();
    if (!esp32_mquickjs_mount_littlefs(true)) {
        ESP_LOGW(TAG, "Continuing without LittleFS-backed script loading");
    }

    s_js_heap = heap_caps_malloc(JS_HEAP_SIZE, MALLOC_CAP_8BIT);
    if (s_js_heap == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for JS heap", (unsigned)JS_HEAP_SIZE);
        return;
    }

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

    if (!esp32_mquickjs_install_globals(s_js_ctx, &s_js_runtime)) {
        ESP_LOGE(TAG, "Failed to install ESP32 host globals");
        heap_caps_free(s_js_heap);
        s_js_heap = NULL;
        return;
    }

    s_js_runtime.before_output = esp32qjs_repl_prepare_async_output;
    s_js_runtime.before_output_opaque = NULL;
    s_js_runtime.before_async_output = esp32qjs_repl_begin_async_output;
    s_js_runtime.before_async_output_opaque = NULL;
    s_js_runtime.after_async_output = esp32qjs_repl_end_async_output;
    s_js_runtime.after_async_output_opaque = NULL;

    ESP_LOGI(TAG, "mquickjs runtime ready");
    ESP_LOGI(TAG, "js_heap=%u bytes, free_heap=%u bytes",
             (unsigned)JS_HEAP_SIZE,
             (unsigned)esp_get_free_heap_size());

    task_priority = uxTaskPriorityGet(NULL);
    if (xTaskCreate(esp32qjs_repl_task,
                    "js_repl",
                    JS_REPL_TASK_STACK_SIZE,
                    NULL,
                    task_priority,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start js_repl task");
        heap_caps_free(s_js_heap);
        s_js_heap = NULL;
        return;
    }

    vTaskDelete(NULL);
}
