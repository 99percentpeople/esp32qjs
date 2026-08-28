#pragma once

#include "sdkconfig.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi.h"

#include "esp32_mquickjs_future.h"

typedef enum {
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA = 0,
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT,
} esp32_mquickjs_wifi_radio_client_t;

typedef struct {
    uint32_t generation;
    esp32_mquickjs_wifi_radio_client_t client;
    bool acquired;
} esp32_mquickjs_wifi_radio_lease_t;

typedef struct {
    uint32_t generation;
    bool initialized;
    bool starting;
    bool started;
    wifi_mode_t mode;
    uint8_t primary_channel;
    wifi_second_chan_t secondary_channel;
    uint32_t channel_generation;
    bool max_tx_power_available;
    int8_t max_tx_power_quarter_dbm;
    uint32_t clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
} esp32_mquickjs_wifi_radio_status_t;

esp_err_t esp32_mquickjs_wifi_radio_acquire(
    esp32_mquickjs_wifi_radio_client_t client,
    wifi_mode_t required_mode,
    esp32_mquickjs_wifi_radio_lease_t *out_lease);

esp_err_t esp32_mquickjs_wifi_radio_ensure_started(
    esp32_mquickjs_wifi_radio_lease_t *lease);

esp_err_t esp32_mquickjs_wifi_radio_get_status(
    esp32_mquickjs_wifi_radio_status_t *out_status);

esp_err_t esp32_mquickjs_wifi_radio_get_channel(
    uint8_t *primary,
    wifi_second_chan_t *secondary,
    uint32_t *channel_generation);

esp_err_t esp32_mquickjs_wifi_radio_set_channel(
    esp32_mquickjs_wifi_radio_lease_t *lease,
    uint8_t primary,
    wifi_second_chan_t secondary);

void esp32_mquickjs_wifi_radio_release(
    esp32_mquickjs_wifi_radio_lease_t *lease);

esp32_mquickjs_resource_key_t
esp32_mquickjs_wifi_radio_channel_key(void);

#endif
