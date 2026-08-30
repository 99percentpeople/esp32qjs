#include "esp32_mquickjs_wifi_csi_target.h"

#include <stddef.h>

esp32_mquickjs_wifi_csi_target_config_result_t
esp32_mquickjs_wifi_csi_target_config_validate(
    bool he_target,
    bool vht_supported,
    bool lltf_bit_mode_supported,
    const esp32_mquickjs_wifi_csi_capture_config_t *config)
{
    if (config == NULL) {
        return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID;
    }
    if ((he_target && config->schema != ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE) ||
        (!he_target &&
         config->schema != ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY)) {
        return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_SCHEMA_MISMATCH;
    }
    if (!he_target) {
        return config->config.legacy.shift_bits <= 15U
            ? ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK
            : ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID;
    }
    if (config->config.he.he_stbc_ltf >
            ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE ||
        config->config.he.value_scale > 8U ||
        (config->config.he.lltf_bits != 8U &&
         config->config.he.lltf_bits != 12U)) {
        return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID;
    }
    if (config->config.he.vht && !vht_supported) {
        return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_UNSUPPORTED;
    }
    if (config->config.he.lltf_bits != 12U &&
        !lltf_bit_mode_supported) {
        return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_UNSUPPORTED;
    }
    return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK;
}
