#pragma once
#include "esp32_mquickjs_wifi_eap_install.h"
#include "esp32_mquickjs_wifi_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
/* Pins the supplied helper leases across the SDK borrow, independently of the
 * scan/connect lane. Only disconnected STA/STA+AP with no other owners enters.
 * Any retained install result remains a Radio duty even when install failed. */
esp_err_t esp32_mquickjs_wifi_radio_eap_install(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_eap_profile_t *profile,
    esp32_mquickjs_wifi_eap_install_result_t *result);
esp_err_t esp32_mquickjs_wifi_radio_eap_clear(uint64_t identity,
    esp32_mquickjs_wifi_eap_install_result_t *result);
esp_err_t esp32_mquickjs_wifi_radio_eap_status(uint64_t identity,
    esp32_mquickjs_wifi_eap_install_result_t *result);
/* Exact pins transfer to an exclusive stop lifecycle before SDK retirement.
 * Failed clear retains both obligations; successful clear is idempotent only
 * while this same lifecycle remains live. Profile configuration is separate. */
esp_err_t esp32_mquickjs_wifi_radio_eap_begin_stop(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_eap_clear_lifecycle(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_eap_install_result_t *result);
esp_err_t esp32_mquickjs_wifi_radio_eap_begin_restart(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_eap_profile_t *profile, bool allow_ap_restart,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode);
/* Install the retained profile before checkpoint/lease publication. A failed
 * native installation stays owned by this lifecycle until exact clear. */
esp_err_t esp32_mquickjs_wifi_radio_eap_resume_restart(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode,
    esp32_mquickjs_wifi_radio_lease_t *application,
    esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_eap_profile_t *profile);
uint64_t esp32_mquickjs_wifi_radio_eap_identity(void);
bool esp32_mquickjs_wifi_radio_eap_ready(void);
esp_err_t esp32_mquickjs_wifi_eap_capture_owners(esp32_mquickjs_wifi_radio_lease_t owners[3]);
/* Runtime/helper task entry points, not SDK callback or JS APIs. */
bool esp32_mquickjs_wifi_eap_prepare_runtime_destroy(void);
esp_err_t esp32_mquickjs_wifi_eap_disconnect_ready(bool *ready);
#endif
