#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_NAN_USD_ENABLE
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_wifi_types.h"
#include "esp32_mquickjs_wifi_nan_tx.h"

struct nan_de;
typedef struct {
    uint32_t identity, registered_handlers, dropped_events;
    esp_err_t error;
    esp_err_t transport_error, cleanup_error;
    uint64_t operation;
    size_t reserved_bytes;
    bool transport_held, tx_done, buffer_retired;
    bool initialized, closing;
} esp32_mquickjs_wifi_nan_usd_sdk_status_t;

/* Native USD engine status. The Radio owner must additionally fence Action/
 * ROC, RX and actual driver buffers before releasing its lease or reopening.
 * Engine deinit alone is not an off-channel/driver retirement barrier. */
void esp32_mquickjs_wifi_nan_usd_sdk_status(esp32_mquickjs_wifi_nan_usd_sdk_status_t *status);

/* The SDK timer carries a numeric lifetime ticket, never ownership of de.
 * Enter compares both without dereferencing de and holds the USD recursive
 * mutex until leave. A dispatched old callback cannot enter a new lifetime,
 * even when the engine allocator reuses the exact pointer. */
bool esp32qjs_nan_usd_timer_enter(struct nan_de *de, uint32_t identity);
void esp32qjs_nan_usd_timer_leave(void);
uint32_t esp32qjs_nan_usd_timer_identity(struct nan_de *de);
void esp32qjs_nan_usd_timer_failed(struct nan_de *de);
bool esp32qjs_nan_usd_is_action_callback(uintptr_t callback);
void esp32qjs_nan_usd_tx_status_capture(const wifi_event_action_tx_status_t *event);
/* Native worker poll; drives missed deadline wakeups and exact retirement.
 * Does not free an engine or grant/release the Radio owner. */
esp_err_t esp32_mquickjs_wifi_nan_usd_sdk_poll(bool close);
esp_err_t esp32_mquickjs_wifi_nan_usd_sdk_send(const wifi_nan_followup_params_t *params, uint32_t identity);
bool esp32_mquickjs_wifi_nan_usd_message_status(uint32_t identity,
    esp32_mquickjs_wifi_nan_message_tx_status_t *status);
esp_err_t esp32_mquickjs_wifi_nan_usd_message_release(uint32_t identity);
uint8_t esp32qjs_nan_usd_tx_service_scope(uint8_t service);
esp_err_t esp32_mquickjs_wifi_nan_usd_service_start(const wifi_nan_publish_cfg_t *publish,
    const wifi_nan_subscribe_cfg_t *subscribe, uint8_t *id);
esp_err_t esp32_mquickjs_wifi_nan_usd_service_close(uint8_t id, bool *cancelled);
void esp32qjs_nan_usd_configure_publish(struct nan_de *de, int id, const wifi_nan_usd_config_t *config);
#endif
