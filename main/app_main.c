#include "esp32qjs_runtime.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "esp32qjs";
static esp32qjs_runtime_t *s_runtime;

void app_main(void)
{
    esp32qjs_runtime_config_t config;
    esp_err_t err;

    esp32qjs_runtime_default_config(&config);
    err = esp32qjs_runtime_create(&config, &s_runtime);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create JavaScript runtime: %s", esp_err_to_name(err));
        return;
    }

    err = esp32qjs_runtime_start(s_runtime);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start JavaScript runtime: %s", esp_err_to_name(err));
        esp32qjs_runtime_destroy(s_runtime);
        s_runtime = NULL;
    }
}
