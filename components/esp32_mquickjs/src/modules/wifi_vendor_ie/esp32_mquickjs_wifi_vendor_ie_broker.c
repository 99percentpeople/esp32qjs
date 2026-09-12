#include "esp32_mquickjs_wifi_vendor_ie_watch.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include <string.h>
static portMUX_TYPE s_vendor_broker_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_vendor_ie_broker_status_t s_vendor_broker;

static void vendor_ie_callback(void *opaque, wifi_vendor_ie_type_t frame,
    const uint8_t address[6], const vendor_ie_data_t *data, int rssi)
{
    uint32_t generation = (uint32_t)(uintptr_t)opaque;
    portENTER_CRITICAL(&s_vendor_broker_lock);
    if (generation == 0U || generation != s_vendor_broker.generation || !s_vendor_broker.accepting ||
        s_vendor_broker.callbacks_active == UINT32_MAX) {
        portEXIT_CRITICAL(&s_vendor_broker_lock); return;
    }
    ++s_vendor_broker.callbacks_active;
    portEXIT_CRITICAL(&s_vendor_broker_lock);
    /* No Radio lock and no JS. The sink copies only this callback's bytes. */
    esp32_mquickjs_wifi_vendor_ie_watch_capture(generation, frame, address, data, rssi);
    portENTER_CRITICAL(&s_vendor_broker_lock);
    --s_vendor_broker.callbacks_active;
    portEXIT_CRITICAL(&s_vendor_broker_lock);
}

esp_err_t esp32_mquickjs_wifi_vendor_ie_broker_register(uint32_t generation)
{
    if (generation == 0U) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_vendor_broker_lock);
    if (s_vendor_broker.generation == generation && s_vendor_broker.registered && s_vendor_broker.accepting &&
        !s_vendor_broker.uncertain && !s_vendor_broker.control_busy) {
        portEXIT_CRITICAL(&s_vendor_broker_lock); return ESP_OK;
    }
    if (s_vendor_broker.generation != 0U || s_vendor_broker.callbacks_active != 0U || s_vendor_broker.control_busy) {
        portEXIT_CRITICAL(&s_vendor_broker_lock); return ESP_ERR_INVALID_STATE;
    }
    s_vendor_broker.generation = generation;
    s_vendor_broker.control_busy = true;
    portEXIT_CRITICAL(&s_vendor_broker_lock);
    esp_err_t err = esp_wifi_set_vendor_ie_cb(vendor_ie_callback, (void *)(uintptr_t)generation);
    portENTER_CRITICAL(&s_vendor_broker_lock);
    s_vendor_broker.control_busy = false;
    s_vendor_broker.registered = err == ESP_OK;
    s_vendor_broker.uncertain = err != ESP_OK;
    s_vendor_broker.accepting = err == ESP_OK;
    s_vendor_broker.error = err;
    portEXIT_CRITICAL(&s_vendor_broker_lock);
    return err;
}

esp_err_t esp32_mquickjs_wifi_vendor_ie_broker_unregister(uint32_t generation)
{
    portENTER_CRITICAL(&s_vendor_broker_lock);
    if (s_vendor_broker.generation == 0U) {
        portEXIT_CRITICAL(&s_vendor_broker_lock); return ESP_OK;
    }
    if (generation != s_vendor_broker.generation || s_vendor_broker.control_busy) {
        portEXIT_CRITICAL(&s_vendor_broker_lock); return ESP_ERR_INVALID_STATE;
    }
    s_vendor_broker.accepting = false;
    bool write = !s_vendor_broker.unregister_written;
    s_vendor_broker.control_busy = true;
    portEXIT_CRITICAL(&s_vendor_broker_lock);
    esp_err_t err = write ? esp_wifi_set_vendor_ie_cb(NULL, NULL) : ESP_OK;
    portENTER_CRITICAL(&s_vendor_broker_lock);
    s_vendor_broker.control_busy = false;
    if (err == ESP_OK) {
        s_vendor_broker.registered = false;
        s_vendor_broker.uncertain = false;
        s_vendor_broker.unregister_written = true;
        if (s_vendor_broker.callbacks_active != 0U) err = ESP_ERR_TIMEOUT;
    } else s_vendor_broker.uncertain = true;
    s_vendor_broker.error = err;
    portEXIT_CRITICAL(&s_vendor_broker_lock);
    return err;
}

bool esp32_mquickjs_wifi_vendor_ie_broker_reset(uint32_t generation)
{
    portENTER_CRITICAL(&s_vendor_broker_lock);
    bool ok = s_vendor_broker.generation == 0U ||
        (s_vendor_broker.generation == generation && s_vendor_broker.unregister_written &&
         !s_vendor_broker.registered && !s_vendor_broker.uncertain && !s_vendor_broker.control_busy &&
         !s_vendor_broker.accepting && s_vendor_broker.callbacks_active == 0U);
    if (ok) memset(&s_vendor_broker, 0, sizeof(s_vendor_broker));
    portEXIT_CRITICAL(&s_vendor_broker_lock);
    return ok;
}

void esp32_mquickjs_wifi_vendor_ie_broker_status(esp32_mquickjs_wifi_vendor_ie_broker_status_t *status)
{
    if (status == NULL) return;
    portENTER_CRITICAL(&s_vendor_broker_lock);
    *status = s_vendor_broker;
    portEXIT_CRITICAL(&s_vendor_broker_lock);
}
#endif
