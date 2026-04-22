#include <stdarg.h>
#include <stdio.h>

#include "sdkconfig.h"
#include "esp_log_write.h"
#include "esp32_mquickjs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "runtime.h"

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
#include "repl_input.h"
#endif

static const char *TAG = "esp32qjs";

#define JS_HEAP_SIZE CONFIG_ESP32QJS_JS_HEAP_SIZE
#define JS_RUNTIME_TASK_STACK_SIZE CONFIG_ESP32QJS_REPL_TASK_STACK_SIZE

static esp32_mquickjs_runtime_t s_js_runtime;
static void *s_js_heap;
static JSContext *s_js_ctx;

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
static vprintf_like_t s_log_vprintf;

static int esp32qjs_log_vprintf(const char *fmt, va_list args)
{
    int written;

    if (s_js_runtime.prepare_output != NULL) {
        s_js_runtime.prepare_output(s_js_runtime.prepare_output_opaque);
    }

    if (s_log_vprintf != NULL) {
        written = s_log_vprintf(fmt, args);
    } else {
        written = vprintf(fmt, args);
    }
    esp32qjs_repl_note_external_output();
    esp32_mquickjs_notify_activity(&s_js_runtime);
    return written;
}
#endif

static void esp32qjs_runtime_task(void *opaque)
{
    (void)opaque;
    esp32qjs_runtime_run(s_js_ctx, &s_js_runtime);
    vTaskDelete(NULL);
}

void app_main(void)
{
    UBaseType_t task_priority;
    const char *js_heap_region;
    bool littlefs_mounted;

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    esp32qjs_console_init();
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

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    s_js_runtime.prepare_output = esp32qjs_repl_prepare_output;
    s_js_runtime.prepare_output_opaque = NULL;
    s_log_vprintf = esp_log_set_vprintf(esp32qjs_log_vprintf);
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
                    JS_RUNTIME_TASK_STACK_SIZE,
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
