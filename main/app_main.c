#include <stdio.h>

#include "esp32_mquickjs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "repl.h"
#include "repl_input.h"

static const char *TAG = "esp32qjs";

#define JS_HEAP_SIZE (160 * 1024)

void app_main(void)
{
    static esp32_mquickjs_runtime_t js_runtime;

    void *js_heap;
    JSContext *ctx;

    esp32qjs_console_init();

    js_heap = heap_caps_malloc(JS_HEAP_SIZE, MALLOC_CAP_8BIT);
    if (js_heap == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for JS heap", (unsigned)JS_HEAP_SIZE);
        return;
    }

    ctx = esp32_mquickjs_create(js_heap,
                                JS_HEAP_SIZE,
                                &js_runtime,
                                ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS);
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Failed to initialize mquickjs");
        heap_caps_free(js_heap);
        return;
    }

    if (!esp32_mquickjs_install_globals(ctx, &js_runtime)) {
        ESP_LOGE(TAG, "Failed to install ESP32 host globals");
        heap_caps_free(js_heap);
        return;
    }

    ESP_LOGI(TAG, "mquickjs runtime ready");
    ESP_LOGI(TAG, "js_heap=%u bytes, free_heap=%u bytes",
             (unsigned)JS_HEAP_SIZE,
             (unsigned)esp_get_free_heap_size());

    esp32qjs_repl_run(ctx, &js_runtime);
}
