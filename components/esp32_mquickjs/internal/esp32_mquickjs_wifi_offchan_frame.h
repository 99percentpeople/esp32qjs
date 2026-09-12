#pragma once
#include "sdkconfig.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
/* Fixed SDK call sites only. Frame status identity and physical TX retirement
 * are distinct: status/reset can revoke the former before the EB is freed. */
int esp32qjs_wifi_offchan_frame_post(void *buffer);
void esp32qjs_wifi_offchan_frame_done(void *buffer, int status);
void *esp32qjs_wifi_offchan_record_reset(void *destination, int value, size_t size);
uint64_t esp32qjs_wifi_offchan_frame_recycle(void *buffer);
void esp32qjs_wifi_offchan_frame_recycled(uint64_t ticket);
int esp32qjs_wifi_offchan_event_post(int event_id, void *data, size_t size);
typedef struct {
    uint64_t ticket;
    esp_err_t error, post_error;
    uint32_t wake_retries;
    bool held, posted, buffer_present, recycling, recycled, wake_pending, unknown_frames;
} esp32qjs_wifi_offchan_tx_status_t;
/* Shared boot-unique tickets for DPP and USD, including queued operations.
 * Allocation does not claim a buffer; an abandoned ticket is never reused. */
esp_err_t esp32qjs_wifi_offchan_tx_allocate_ticket(uint64_t *ticket);
/* One native managed TX at a time. Reserve before driver admission; release only
 * after BOTH native operation retirement and the exact recycler return. */
esp_err_t esp32qjs_wifi_offchan_tx_reserve(uint64_t ticket, uint32_t context, uint8_t channel);
esp_err_t esp32qjs_wifi_offchan_tx_status(uint64_t ticket, esp32qjs_wifi_offchan_tx_status_t *out);
esp_err_t esp32qjs_wifi_offchan_tx_poll_native(uint64_t ticket);
esp_err_t esp32qjs_wifi_offchan_tx_release(uint64_t ticket);
#endif
