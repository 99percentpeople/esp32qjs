#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_radio.h"
#define ESP32_MQUICKJS_WIFI_BTM_MAX_CANDIDATES 16U
#define ESP32_MQUICKJS_WIFI_BTM_TEXT_BYTES (64U * ESP32_MQUICKJS_WIFI_BTM_MAX_CANDIDATES + 1U)
typedef struct {
    uint8_t bssid[6];
    uint32_t information;
    uint8_t operating_class, channel, phy_type, preference;
    bool preference_set;
} esp32_mquickjs_wifi_btm_candidate_t;
typedef struct {
    esp32_mquickjs_wifi_btm_candidate_t candidates[ESP32_MQUICKJS_WIFI_BTM_MAX_CANDIDATES];
    uint8_t count, reason;
    bool allow_ap_channel_change;
} esp32_mquickjs_wifi_btm_query_t;
typedef struct {
    const char *stage;
    uint32_t radio_generation;
    esp_err_t error;
    int sdk_code;
    bool entered, submitted, rrm, btm;
} esp32_mquickjs_wifi_roaming_result_t;
/* SDK string is an internal encoding, never caller-supplied text. */
bool esp32_mquickjs_wifi_btm_encode(const esp32_mquickjs_wifi_btm_query_t *query,
    char *text, size_t capacity);
#if CONFIG_ESP_WIFI_RRM_SUPPORT || CONFIG_ESP_WIFI_WNM_SUPPORT
/* Radio mutex protects lifecycle; SDK dispatch serializes with supplicant RX/reset. */
esp_err_t esp32_mquickjs_wifi_roaming_sdk(const esp32_mquickjs_wifi_btm_query_t *query,
    esp32_mquickjs_wifi_roaming_result_t *result);
esp_err_t esp32_mquickjs_wifi_radio_roaming(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_btm_query_t *query, esp32_mquickjs_wifi_roaming_result_t *result);
esp_err_t esp32_mquickjs_wifi_roaming_execute(const esp32_mquickjs_wifi_btm_query_t *query,
    esp32_mquickjs_wifi_roaming_result_t *result);
#endif
#if CONFIG_ESP_WIFI_RRM_SUPPORT || CONFIG_ESP_WIFI_WNM_SUPPORT || CONFIG_ESP_WIFI_11R_SUPPORT
JSValue js_wifi_roaming_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv);
#endif
#if CONFIG_ESP_WIFI_RRM_SUPPORT
JSValue js_wifi_roaming_is_rrm_supported(JSContext *ctx, JSValue *self, int argc, JSValue *argv);
#endif
#if CONFIG_ESP_WIFI_WNM_SUPPORT
JSValue js_wifi_roaming_is_btm_supported(JSContext *ctx, JSValue *self, int argc, JSValue *argv);
JSValue js_wifi_roaming_send_btm_query(JSContext *ctx, JSValue *self, int argc, JSValue *argv);
#endif
#endif
