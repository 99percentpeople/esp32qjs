#pragma once
#include "sdkconfig.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
#define ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_PROFILES 2U
#define ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_PROFILE_BYTES 65536U
#define ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_RETAINED_BYTES 131072U

typedef enum {
    /* SDK set_identity is the outer anonymous identity; set_username supplies
     * the EAP authentication identity. Keep these as two independent spans. */
    ESP32_MQUICKJS_WIFI_EAP_ANONYMOUS_IDENTITY,
    ESP32_MQUICKJS_WIFI_EAP_USERNAME,
    ESP32_MQUICKJS_WIFI_EAP_PASSWORD,
    ESP32_MQUICKJS_WIFI_EAP_NEW_PASSWORD,
    ESP32_MQUICKJS_WIFI_EAP_CA_CERT,
    ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT,
    ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY,
    ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY_PASSWORD,
    ESP32_MQUICKJS_WIFI_EAP_PAC, /* Present data >=512 bytes; absent requires explicit FAST provisioning. */
    ESP32_MQUICKJS_WIFI_EAP_DOMAIN,
    ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT,
} esp32_mquickjs_wifi_eap_field_t;

typedef struct { const uint8_t *data; size_t length; } esp32_mquickjs_wifi_eap_span_t;
typedef struct {
    uint8_t methods; /* esp_eap_method_t bits; nonzero, explicit. */
    uint8_t ttls_phase2; /* esp_eap_ttls_phase2_types, including zero (EAP). */
    uint8_t fast_provisioning; /* 0, 1 or 2; SDK supports 2 despite header prose. */
    uint8_t fast_max_pac_list; /* 0 means SDK default, otherwise 1..99. */
    bool fast_binary, disable_time_check, suiteb, default_bundle, okc;
} esp32_mquickjs_wifi_eap_policy_t;
typedef struct {
    esp32_mquickjs_wifi_eap_policy_t policy;
    esp32_mquickjs_wifi_eap_span_t fields[ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT];
} esp32_mquickjs_wifi_eap_input_t;
typedef struct esp32_mquickjs_wifi_eap_profile esp32_mquickjs_wifi_eap_profile_t;
typedef struct {
    uint32_t profiles, reserved_bytes; /* Includes allocation and final wipe in progress. */
} esp32_mquickjs_wifi_eap_counts_t;

/* Immutable native storage, not SDK configuration or a public JS API. Input
 * spans remain valid for the synchronous call only; all bytes are copied.
 * No driver I/O. On error *output is NULL. Limits include private trailing NULs
 * and the profile record, but exclude allocator overhead and SDK/TLS copies. */
esp_err_t esp32_mquickjs_wifi_eap_profile_create(const esp32_mquickjs_wifi_eap_input_t *input,
    esp32_mquickjs_wifi_eap_profile_t **output);
/* Capture-only construction. Reserves exact final storage in the same profile
 * budget before any bytes are written. No view/retain before successful seal.
 * Builder is exclusive to its creating task; release also destroys unsealed
 * partial secrets. Mutable field pointers must not escape capture. */
esp_err_t esp32_mquickjs_wifi_eap_profile_begin(const esp32_mquickjs_wifi_eap_policy_t *policy,
    const size_t lengths[ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT], esp32_mquickjs_wifi_eap_profile_t **output);
uint8_t *esp32_mquickjs_wifi_eap_profile_field(esp32_mquickjs_wifi_eap_profile_t *profile,
    esp32_mquickjs_wifi_eap_field_t field);
esp_err_t esp32_mquickjs_wifi_eap_profile_seal(esp32_mquickjs_wifi_eap_profile_t *profile);
/* Every access requires a live owned reference; never pass stale pointers.
 * SDK borrowers must retain until native disable/retirement is proven. */
bool esp32_mquickjs_wifi_eap_profile_retain(esp32_mquickjs_wifi_eap_profile_t *profile);
void esp32_mquickjs_wifi_eap_profile_release(esp32_mquickjs_wifi_eap_profile_t *profile);
/* Internal borrowed view only. Cannot be exposed through status/errors/logs.
 * Each present field has a trailing zero excluded from its exact byte length. */
bool esp32_mquickjs_wifi_eap_profile_view(const esp32_mquickjs_wifi_eap_profile_t *profile,
    esp32_mquickjs_wifi_eap_input_t *output);
void esp32_mquickjs_wifi_eap_profile_counts(esp32_mquickjs_wifi_eap_counts_t *output);
size_t esp32_mquickjs_wifi_eap_field_limit(esp32_mquickjs_wifi_eap_field_t field);
#endif
