#include "esp32_mquickjs_wifi_csi_target.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    esp32_mquickjs_wifi_csi_capture_config_t config;

    memset(&config, 0, sizeof(config));
    config.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY;
    config.config.legacy.lltf = true;
    config.config.legacy.ht_ltf = true;
    config.config.legacy.shift_bits = 15;
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        false, false, false, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK);
    config.config.legacy.shift_bits = 16;
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        false, false, false, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID);
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        true, true, true, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_SCHEMA_MISMATCH);

    memset(&config, 0, sizeof(config));
    config.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE;
    config.config.he.enable = true;
    config.config.he.vht = true;
    config.config.he.lltf_bits = 12;
    config.config.he.value_scale = 8;
    config.config.he.he_stbc_ltf =
        ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE;
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        true, true, true, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK);
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        true, false, true, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_UNSUPPORTED);
    config.config.he.vht = false;
    config.config.he.lltf_bits = 8;
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        true, true, false, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_UNSUPPORTED);
    config.config.he.lltf_bits = 7;
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        true, true, true, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID);
    return 0;
}
