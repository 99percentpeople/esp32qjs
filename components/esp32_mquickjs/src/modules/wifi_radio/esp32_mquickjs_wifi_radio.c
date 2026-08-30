#include "esp32_mquickjs_wifi_radio.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO

#include "esp32_mquickjs_nvs_flash_boot.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum {
    WIFI_RADIO_NOT_STARTED = 0,
    WIFI_RADIO_IN_PROGRESS,
    WIFI_RADIO_COMPLETE,
} wifi_radio_once_state_t;

typedef struct {
    portMUX_TYPE lock;
    uint32_t generation;
    uint32_t clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    wifi_mode_t required_modes[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    uint8_t primary_channel;
    wifi_second_chan_t secondary_channel;
    uint32_t channel_generation;
    bool fixed_channel_claimed;
    esp32_mquickjs_wifi_radio_client_t fixed_channel_client;
    uint8_t fixed_primary_channel;
    wifi_second_chan_t fixed_secondary_channel;
    bool promiscuous_claimed;
    esp32_mquickjs_wifi_radio_client_t promiscuous_client;
} wifi_radio_state_t;

static const char *TAG = "esp32qjs_wifi_radio";
static const char s_wifi_radio_channel_lane_key;
static wifi_radio_state_t s_radio = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .generation = 1,
};
static _Atomic int s_init_state = WIFI_RADIO_NOT_STARTED;
static _Atomic int s_start_state = WIFI_RADIO_NOT_STARTED;
static _Atomic int s_init_result = ESP_ERR_INVALID_STATE;
static _Atomic int s_start_result = ESP_ERR_INVALID_STATE;

static esp_err_t wifi_radio_wait_once(_Atomic int *state,
                                      _Atomic int *result)
{
    while (atomic_load_explicit(state, memory_order_acquire) ==
           WIFI_RADIO_IN_PROGRESS) {
        vTaskDelay(1);
    }
    return (esp_err_t)atomic_load_explicit(result, memory_order_relaxed);
}

static esp_err_t wifi_radio_initialize(void)
{
    int expected = WIFI_RADIO_NOT_STARTED;

    if (atomic_compare_exchange_strong_explicit(
            &s_init_state, &expected, WIFI_RADIO_IN_PROGRESS,
            memory_order_acq_rel, memory_order_acquire)) {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp32_mquickjs_nvs_flash_ensure_initialized();

        if (err == ESP_OK) {
            err = esp_wifi_init(&config);
        }
        if (err == ESP_OK) {
            err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        }
        atomic_store_explicit(&s_init_result, err, memory_order_relaxed);
        atomic_store_explicit(
            &s_init_state, WIFI_RADIO_COMPLETE, memory_order_release);
        return err;
    }
    return wifi_radio_wait_once(&s_init_state, &s_init_result);
}

static bool wifi_radio_lease_valid(
    const esp32_mquickjs_wifi_radio_lease_t *lease)
{
    bool valid;

    if (lease == NULL || !lease->acquired ||
        lease->client >= ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT) {
        return false;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    valid = lease->generation == s_radio.generation &&
            s_radio.clients[lease->client] > 0;
    taskEXIT_CRITICAL(&s_radio.lock);
    return valid;
}

esp_err_t esp32_mquickjs_wifi_radio_acquire(
    esp32_mquickjs_wifi_radio_client_t client,
    wifi_mode_t required_mode,
    esp32_mquickjs_wifi_radio_lease_t *out_lease)
{
    if (out_lease == NULL ||
        client >= ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT ||
        (required_mode != WIFI_MODE_STA &&
         required_mode != WIFI_MODE_APSTA)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_lease, 0, sizeof(*out_lease));
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.clients[client]++;
    if (required_mode == WIFI_MODE_APSTA ||
        s_radio.required_modes[client] == WIFI_MODE_NULL) {
        s_radio.required_modes[client] = required_mode;
    }
    out_lease->generation = s_radio.generation;
    out_lease->client = client;
    out_lease->acquired = true;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_ensure_started(
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    int expected = WIFI_RADIO_NOT_STARTED;
    wifi_mode_t required_mode = WIFI_MODE_STA;
    wifi_mode_t current_mode;
    wifi_mode_t merged_mode;
    uint32_t client;
    esp_err_t err;

    if (!wifi_radio_lease_valid(lease)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(
        wifi_radio_initialize(), TAG, "Wi-Fi radio initialization failed");
    taskENTER_CRITICAL(&s_radio.lock);
    for (client = 0; client < ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT;
         ++client) {
        if (s_radio.clients[client] > 0 &&
            s_radio.required_modes[client] == WIFI_MODE_APSTA) {
            required_mode = WIFI_MODE_APSTA;
        }
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    ESP_RETURN_ON_ERROR(
        esp_wifi_get_mode(&current_mode), TAG, "read Wi-Fi mode failed");
    merged_mode = current_mode;
    if (required_mode == WIFI_MODE_APSTA || current_mode == WIFI_MODE_AP) {
        merged_mode = WIFI_MODE_APSTA;
    } else if (current_mode == WIFI_MODE_NULL) {
        merged_mode = WIFI_MODE_STA;
    }
    if (merged_mode != current_mode) {
        ESP_RETURN_ON_ERROR(
            esp_wifi_set_mode(merged_mode), TAG, "set Wi-Fi mode failed");
    }
    if (atomic_compare_exchange_strong_explicit(
            &s_start_state, &expected, WIFI_RADIO_IN_PROGRESS,
            memory_order_acq_rel, memory_order_acquire)) {
        err = esp_wifi_start();

        atomic_store_explicit(&s_start_result, err, memory_order_relaxed);
        atomic_store_explicit(
            &s_start_state, WIFI_RADIO_COMPLETE, memory_order_release);
        return err;
    }
    return wifi_radio_wait_once(&s_start_state, &s_start_result);
}

esp_err_t esp32_mquickjs_wifi_radio_get_channel(
    uint8_t *primary,
    wifi_second_chan_t *secondary,
    uint32_t *channel_generation)
{
    uint8_t actual_primary = 0;
    wifi_second_chan_t actual_secondary = WIFI_SECOND_CHAN_NONE;
    esp_err_t err;

    if (primary == NULL || secondary == NULL ||
        channel_generation == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = esp_wifi_get_channel(&actual_primary, &actual_secondary);
    if (err != ESP_OK) {
        return err;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.channel_generation == 0 ||
        s_radio.primary_channel != actual_primary ||
        s_radio.secondary_channel != actual_secondary) {
        s_radio.primary_channel = actual_primary;
        s_radio.secondary_channel = actual_secondary;
        s_radio.channel_generation++;
        if (s_radio.channel_generation == 0) {
            s_radio.channel_generation = 1;
        }
    }
    *primary = s_radio.primary_channel;
    *secondary = s_radio.secondary_channel;
    *channel_generation = s_radio.channel_generation;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_get_status(
    esp32_mquickjs_wifi_radio_status_t *out_status)
{
    int init_state;
    int start_state;
    esp_err_t init_result;
    esp_err_t start_result;
    esp_err_t err;

    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_status, 0, sizeof(*out_status));
    out_status->mode = WIFI_MODE_NULL;
    out_status->secondary_channel = WIFI_SECOND_CHAN_NONE;

    init_state = atomic_load_explicit(&s_init_state, memory_order_acquire);
    start_state = atomic_load_explicit(&s_start_state, memory_order_acquire);
    init_result = (esp_err_t)atomic_load_explicit(
        &s_init_result, memory_order_relaxed);
    start_result = (esp_err_t)atomic_load_explicit(
        &s_start_result, memory_order_relaxed);
    out_status->initialized = init_state == WIFI_RADIO_COMPLETE &&
                              init_result == ESP_OK;
    out_status->starting = start_state == WIFI_RADIO_IN_PROGRESS;
    out_status->started = start_state == WIFI_RADIO_COMPLETE &&
                          start_result == ESP_OK;

    taskENTER_CRITICAL(&s_radio.lock);
    out_status->generation = s_radio.generation;
    memcpy(out_status->clients, s_radio.clients,
           sizeof(out_status->clients));
    out_status->primary_channel = s_radio.primary_channel;
    out_status->secondary_channel = s_radio.secondary_channel;
    out_status->channel_generation = s_radio.channel_generation;
    out_status->fixed_channel_claimed = s_radio.fixed_channel_claimed;
    out_status->fixed_channel_client = s_radio.fixed_channel_client;
    out_status->promiscuous_claimed = s_radio.promiscuous_claimed;
    out_status->promiscuous_client = s_radio.promiscuous_client;
    taskEXIT_CRITICAL(&s_radio.lock);

    if (!out_status->initialized) {
        return ESP_OK;
    }
    err = esp_wifi_get_mode(&out_status->mode);
    if (err != ESP_OK) {
        return err;
    }
    if (out_status->started) {
        uint8_t primary = 0;
        wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
        uint32_t generation = 0;
        int8_t max_tx_power = 0;
        wifi_ps_type_t power_save = WIFI_PS_NONE;

        if (esp32_mquickjs_wifi_radio_get_channel(
                &primary, &secondary, &generation) == ESP_OK) {
            out_status->primary_channel = primary;
            out_status->secondary_channel = secondary;
            out_status->channel_generation = generation;
        }
        if (esp_wifi_get_max_tx_power(&max_tx_power) == ESP_OK) {
            out_status->max_tx_power_available = true;
            out_status->max_tx_power_quarter_dbm = max_tx_power;
        }
        if (esp_wifi_get_ps(&power_save) == ESP_OK) {
            out_status->power_save_available = true;
            out_status->power_save = power_save;
        }
    }
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_set_channel(
    esp32_mquickjs_wifi_radio_lease_t *lease,
    uint8_t primary,
    wifi_second_chan_t secondary)
{
    wifi_ap_record_t ap = {0};
    wifi_config_t ap_config = {0};
    wifi_mode_t mode = WIFI_MODE_NULL;
    bool conflict;
    esp_err_t err;

    if (!wifi_radio_lease_valid(lease) || primary == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    conflict = s_radio.fixed_channel_claimed &&
               s_radio.fixed_channel_client != lease->client &&
               (s_radio.fixed_primary_channel != primary ||
                s_radio.fixed_secondary_channel != secondary);
    taskEXIT_CRITICAL(&s_radio.lock);
    if (conflict) {
        return ESP_ERR_INVALID_STATE;
    }
    err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK && ap.primary != primary) {
        return ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
        return err;
    }
    ESP_RETURN_ON_ERROR(
        esp_wifi_get_mode(&mode), TAG, "read Wi-Fi mode failed");
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        ESP_RETURN_ON_ERROR(
            esp_wifi_get_config(WIFI_IF_AP, &ap_config), TAG,
            "read SoftAP channel failed");
        if (ap_config.ap.channel != 0U && ap_config.ap.channel != primary) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_channel(primary, secondary), TAG,
        "set Wi-Fi channel failed");
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.fixed_channel_claimed = true;
    s_radio.fixed_channel_client = lease->client;
    s_radio.fixed_primary_channel = primary;
    s_radio.fixed_secondary_channel = secondary;
    if (s_radio.channel_generation == 0 ||
        s_radio.primary_channel != primary ||
        s_radio.secondary_channel != secondary) {
        s_radio.primary_channel = primary;
        s_radio.secondary_channel = secondary;
        s_radio.channel_generation++;
        if (s_radio.channel_generation == 0) {
            s_radio.channel_generation = 1;
        }
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

void esp32_mquickjs_wifi_radio_release_channel(
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    if (!wifi_radio_lease_valid(lease)) {
        return;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.fixed_channel_claimed &&
        s_radio.fixed_channel_client == lease->client) {
        s_radio.fixed_channel_claimed = false;
        s_radio.fixed_primary_channel = 0U;
        s_radio.fixed_secondary_channel = WIFI_SECOND_CHAN_NONE;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
}

esp_err_t esp32_mquickjs_wifi_radio_acquire_promiscuous(
    esp32_mquickjs_wifi_radio_lease_t *radio_lease,
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *out_lease)
{
    bool enabled = false;
    esp_err_t err;

    if (!wifi_radio_lease_valid(radio_lease) || out_lease == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_lease, 0, sizeof(*out_lease));
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.promiscuous_claimed) {
        taskEXIT_CRITICAL(&s_radio.lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_radio.promiscuous_claimed = true;
    s_radio.promiscuous_client = radio_lease->client;
    taskEXIT_CRITICAL(&s_radio.lock);

    err = esp_wifi_get_promiscuous(&enabled);
    if (err == ESP_OK && enabled) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_promiscuous(true);
    }
    if (err != ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        if (s_radio.promiscuous_claimed &&
            s_radio.promiscuous_client == radio_lease->client) {
            s_radio.promiscuous_claimed = false;
        }
        taskEXIT_CRITICAL(&s_radio.lock);
        return err;
    }
    out_lease->generation = radio_lease->generation;
    out_lease->client = radio_lease->client;
    out_lease->acquired = true;
    out_lease->framework_enabled = true;
    return ESP_OK;
}

void esp32_mquickjs_wifi_radio_release_promiscuous(
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *lease)
{
    bool restore = false;

    if (lease == NULL || !lease->acquired) {
        return;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    if (lease->generation == s_radio.generation &&
        s_radio.promiscuous_claimed &&
        s_radio.promiscuous_client == lease->client) {
        s_radio.promiscuous_claimed = false;
        restore = lease->framework_enabled;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (restore) {
        (void)esp_wifi_set_promiscuous(false);
    }
    memset(lease, 0, sizeof(*lease));
}

void esp32_mquickjs_wifi_radio_release(
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    bool restore_promiscuous = false;

    if (!wifi_radio_lease_valid(lease)) {
        return;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.clients[lease->client] > 0) {
        s_radio.clients[lease->client]--;
        if (s_radio.clients[lease->client] == 0) {
            s_radio.required_modes[lease->client] = WIFI_MODE_NULL;
            if (s_radio.fixed_channel_claimed &&
                s_radio.fixed_channel_client == lease->client) {
                s_radio.fixed_channel_claimed = false;
                s_radio.fixed_primary_channel = 0U;
                s_radio.fixed_secondary_channel = WIFI_SECOND_CHAN_NONE;
            }
            if (s_radio.promiscuous_claimed &&
                s_radio.promiscuous_client == lease->client) {
                s_radio.promiscuous_claimed = false;
                restore_promiscuous = true;
            }
        }
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (restore_promiscuous) {
        (void)esp_wifi_set_promiscuous(false);
    }
    memset(lease, 0, sizeof(*lease));
}

esp32_mquickjs_resource_key_t
esp32_mquickjs_wifi_radio_channel_key(void)
{
    return &s_wifi_radio_channel_lane_key;
}

#endif
