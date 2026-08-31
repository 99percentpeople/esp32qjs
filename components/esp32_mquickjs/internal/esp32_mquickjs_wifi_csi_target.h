#ifndef ESP32_MQUICKJS_WIFI_CSI_TARGET_H
#define ESP32_MQUICKJS_WIFI_CSI_TARGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp32_mquickjs_wifi_csi_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY = 0,
    ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE,
} esp32_mquickjs_wifi_csi_schema_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_HE_STBC_FIRST = 0,
    ESP32_MQUICKJS_WIFI_CSI_HE_STBC_SECOND,
    ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE,
} esp32_mquickjs_wifi_csi_he_stbc_t;

typedef struct {
    bool lltf;
    bool ht_ltf;
    bool stbc_ht_ltf2;
    bool ltf_merge;
    bool adjacent_subcarrier_filter;
    bool manual_scale;
    uint8_t shift_bits;
    bool dump_ack;
} esp32_mquickjs_wifi_csi_legacy_config_t;

typedef struct {
    bool enable;
    bool enable_legacy;
    bool force_legacy_ltf;
    bool ht20;
    bool ht40;
    bool vht;
    bool he_su;
    bool he_mu;
    bool he_dcm;
    bool he_beamformed;
    esp32_mquickjs_wifi_csi_he_stbc_t he_stbc_ltf;
    uint8_t value_scale;
    bool dump_ack;
    uint8_t lltf_bits;
} esp32_mquickjs_wifi_csi_he_config_t;

typedef struct {
    esp32_mquickjs_wifi_csi_schema_t schema;
    union {
        esp32_mquickjs_wifi_csi_legacy_config_t legacy;
        esp32_mquickjs_wifi_csi_he_config_t he;
    } config;
} esp32_mquickjs_wifi_csi_capture_config_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK = 0,
    ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_SCHEMA_MISMATCH,
    ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID,
    ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_UNSUPPORTED,
} esp32_mquickjs_wifi_csi_target_config_result_t;

esp32_mquickjs_wifi_csi_target_config_result_t
esp32_mquickjs_wifi_csi_target_config_validate(
    bool he_target,
    bool vht_supported,
    bool lltf_bit_mode_supported,
    const esp32_mquickjs_wifi_csi_capture_config_t *config);
bool esp32_mquickjs_wifi_csi_target_channel_structurally_valid(
    bool supports_5ghz, uint8_t channel);
void esp32_mquickjs_wifi_csi_target_decode_he_signal(
    esp32_mquickjs_wifi_csi_metadata_t *metadata,
    uint32_t signal_a1,
    uint16_t signal_a2);
void esp32_mquickjs_wifi_csi_target_build_legacy_layout(
    esp32_mquickjs_wifi_csi_metadata_t *metadata,
    const esp32_mquickjs_wifi_csi_capture_config_t *config,
    size_t frame_length);
void esp32_mquickjs_wifi_csi_target_build_he_layout(
    esp32_mquickjs_wifi_csi_metadata_t *metadata,
    const esp32_mquickjs_wifi_csi_capture_config_t *config,
    size_t frame_length);

#if defined(CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI) && \
    CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI

#include "esp_err.h"
#include "esp_wifi.h"

bool esp32_mquickjs_wifi_csi_target_is_he(void);
bool esp32_mquickjs_wifi_csi_target_supports_vht(void);
bool esp32_mquickjs_wifi_csi_target_supports_lltf_bit_mode(void);
const char *esp32_mquickjs_wifi_csi_target_schema_name(void);
esp_err_t esp32_mquickjs_wifi_csi_target_apply_config(
    const esp32_mquickjs_wifi_csi_capture_config_t *config);
void esp32_mquickjs_wifi_csi_target_normalize_metadata(
    const wifi_csi_info_t *info,
    const esp32_mquickjs_wifi_csi_capture_config_t *config,
    esp32_mquickjs_wifi_csi_metadata_t *metadata);

#endif

#ifdef __cplusplus
}
#endif

#endif
