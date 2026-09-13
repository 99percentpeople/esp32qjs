#include "esp32_mquickjs_wifi_raw_tx_pump.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_raw_tx_session.h"
#include "esp32_mquickjs_future.h"
#include "esp_timer.h"
#include <stdatomic.h>

/* A boot-owned timer and coalesced worker use no executor pointer and no
 * dedicated task stack. The timer is a queue-saturation/cleanup retry, while
 * admission and completion wake the worker immediately. Retained across runtime
 * restarts; disarmed when all Sessions close. */
static esp_timer_handle_t s_pump_timer;
static atomic_bool s_pump_queued, s_pump_initializing;
static atomic_bool s_pump_ready;
static void pump_worker(void *unused);

static void pump_retry(void *unused)
{
    (void)unused;
    esp32_mquickjs_wifi_raw_tx_pump_wake();
}

esp_err_t esp32_mquickjs_wifi_raw_tx_pump_init(void)
{
    if (atomic_load_explicit(&s_pump_ready, memory_order_acquire)) return ESP_OK;
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_pump_initializing, &expected, true)) return ESP_ERR_INVALID_STATE;
    const esp_timer_create_args_t args = {.callback = pump_retry, .name = "raw-tx-pump"};
    esp_err_t error = esp_timer_create(&args, &s_pump_timer);
    if (error == ESP_OK) atomic_store_explicit(&s_pump_ready, true, memory_order_release);
    atomic_store(&s_pump_initializing, false);
    return error;
}

void esp32_mquickjs_wifi_raw_tx_pump_wake(void)
{
    if (!atomic_load_explicit(&s_pump_ready, memory_order_acquire)) return;
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_pump_queued, &expected, true)) return;
    if (esp32_mquickjs_submit_background_worker(pump_worker, NULL)) return;
    atomic_store(&s_pump_queued, false);
    /* INVALID_STATE means an existing retry is already armed. */
    (void)esp_timer_start_once(s_pump_timer, ESP32_MQUICKJS_WIFI_RAW_TX_SERVICE_RETRY_US);
}

static void pump_worker(void *unused)
{
    (void)unused;
    (void)esp32_mquickjs_wifi_raw_tx_sessions_service();
    atomic_store(&s_pump_queued, false);
    /* A callback can race service while its Session worker is busy. An armed
     * retry also covers that race and failed native cleanup without JS polling. */
    if (esp32_mquickjs_wifi_raw_tx_sessions_need_service())
        (void)esp_timer_start_once(s_pump_timer, ESP32_MQUICKJS_WIFI_RAW_TX_SERVICE_RETRY_US);
}
#endif
