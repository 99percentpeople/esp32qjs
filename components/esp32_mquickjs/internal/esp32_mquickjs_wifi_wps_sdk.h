#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wps.h"

/* Private fixed-SDK bridge. All functions except held() run on the Wi-Fi task.
 * No function performs IPC or accepts a JS pointer. The future worker must keep
 * IPC argument storage alive until native dispatch has actually finished. */
#define ESP32_MQUICKJS_WPS_CREDENTIALS_MAX 3
typedef struct {
    uint8_t ssid[32], password[64];
    uint8_t ssid_length, password_length, key_index, mac[6];
    uint16_t auth_type, encryption_type;
} esp32_mquickjs_wifi_wps_credential_t;

typedef struct {
    uint8_t count;
    esp32_mquickjs_wifi_wps_credential_t entries[ESP32_MQUICKJS_WPS_CREDENTIALS_MAX];
} esp32_mquickjs_wifi_wps_credentials_t;

typedef enum {
    ESP32QJS_WPS_CLEANUP_DISCONNECT,
    ESP32QJS_WPS_CLEANUP_TYPE,
    ESP32QJS_WPS_CLEANUP_START_FLAG,
    ESP32QJS_WPS_CLEANUP_SCAN,
    ESP32QJS_WPS_CLEANUP_TIMERS,
    ESP32QJS_WPS_CLEANUP_CONNECTION_TIMER,
    ESP32QJS_WPS_CLEANUP_CALLBACKS,
    ESP32QJS_WPS_CLEANUP_PROBE_IE,
    ESP32QJS_WPS_CLEANUP_ASSOC_IE,
    ESP32QJS_WPS_CLEANUP_STATUS,
    ESP32QJS_WPS_CLEANUP_DONE,
} esp32_mquickjs_wifi_wps_cleanup_stage_t;

typedef enum {
    ESP32QJS_WPS_STAGE_NONE,
    ESP32QJS_WPS_STAGE_INIT,
    ESP32QJS_WPS_STAGE_TYPE,
    ESP32QJS_WPS_STAGE_STATUS,
    ESP32QJS_WPS_STAGE_MAC,
    ESP32QJS_WPS_STAGE_CONTEXT,
    ESP32QJS_WPS_STAGE_DEVICE,
    ESP32QJS_WPS_STAGE_PIN,
    ESP32QJS_WPS_STAGE_PROTOCOL,
    ESP32QJS_WPS_STAGE_PROBE_IE,
    ESP32QJS_WPS_STAGE_ASSOC_IE,
    ESP32QJS_WPS_STAGE_CALLBACKS,
    ESP32QJS_WPS_STAGE_TIMER,
    ESP32QJS_WPS_STAGE_DH,
    ESP32QJS_WPS_STAGE_DISCONNECT,
    ESP32QJS_WPS_STAGE_SCAN,
    ESP32QJS_WPS_STAGE_SCAN_DONE,
    ESP32QJS_WPS_STAGE_CONFIG,
    ESP32QJS_WPS_STAGE_CONNECT,
    ESP32QJS_WPS_STAGE_START_FLAG,
    ESP32QJS_WPS_STAGE_EAPOL_BSSID,
    ESP32QJS_WPS_STAGE_EAPOL_TX,
    ESP32QJS_WPS_STAGE_EAPOL_RX,
    ESP32QJS_WPS_STAGE_FRAGMENT,
    ESP32QJS_WPS_STAGE_MODE,
} esp32_mquickjs_wifi_wps_error_stage_t;

/* Metadata only. inputs_stopped is NOT proof of native queue/TX retirement.
 * sdk_state_retired means SDK heap state was destroyed outside its callbacks;
 * the retained result still blocks reuse until the worker's ordered drain.
 * A retained terminal result and the public Future have independent lifetimes. */
typedef struct {
    uint64_t identity;
    esp_err_t error, cleanup_error;
    int32_t event_id, failure_reason;
    uint32_t reserved_bytes;
    bool enabled, started, closing, terminal_seen, inputs_stopped;
    bool pin_available, credentials_available;
    bool sdk_state_retired, tracking_fault;
    uint8_t cleanup_stage, error_stage;
    uint16_t callback_depth;
} esp32_mquickjs_wifi_wps_native_status_t;

bool esp32qjs_wps_native_held(void);
esp_err_t esp32qjs_wps_native_begin(const esp_wps_config_t *config, uint64_t *identity);
esp_err_t esp32qjs_wps_native_start(uint64_t identity);
esp_err_t esp32qjs_wps_native_status(uint64_t identity,
    esp32_mquickjs_wifi_wps_native_status_t *status);
esp_err_t esp32qjs_wps_native_pin_copy(uint64_t identity, uint8_t pin[8]);
esp_err_t esp32qjs_wps_native_pin_commit(uint64_t identity);
esp_err_t esp32qjs_wps_native_credentials_copy(uint64_t identity,
    esp32_mquickjs_wifi_wps_credentials_t *credentials);
esp_err_t esp32qjs_wps_native_credentials_commit(uint64_t identity);
esp_err_t esp32qjs_wps_native_close(uint64_t identity);
/* Stop/capture callbacks never free the SDK state they are still executing.
 * Call on a later native task dispatch after close/terminal input shutdown.
 * The callback-depth check enforces this even if invoked accidentally nested.
 * Success is SDK heap retirement only, not permission to reuse this lane. */
esp_err_t esp32qjs_wps_native_retire_state(uint64_t identity);
/* Keep results while stopping terminal capture. close() instead discards them. */
esp_err_t esp32qjs_wps_native_finish_capture(uint64_t identity);
/* Worker must stop inputs, drain ESP_TIMER_TASK, then dispatch checkpoint on
 * the Wi-Fi task. Radio ownership must exclude new scanning/connection work.
 * This snapshot alone is NOT a queue fence. A later callback invalidates it. */
esp_err_t esp32qjs_wps_native_checkpoint(uint64_t identity, uint32_t *revision);
esp_err_t esp32qjs_wps_native_release(uint64_t identity, uint32_t revision);
