#include "esp32_mquickjs_wifi_enterprise_profile.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
#include "esp32_mquickjs_wireless_core.h"
#include "esp_eap_client.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

struct esp32_mquickjs_wifi_eap_profile {
    uint32_t refs;
    size_t bytes;
    bool sealed;
    esp32_mquickjs_wifi_eap_input_t input;
    uint8_t payload[];
};
_Static_assert(sizeof(esp32_mquickjs_wifi_eap_profile_t) < ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_PROFILE_BYTES,
    "EAP profile metadata must fit before bounded payload additions");
static portMUX_TYPE s_eap_profile_mux = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_eap_counts_t s_eap_profile_counts;

size_t esp32_mquickjs_wifi_eap_field_limit(esp32_mquickjs_wifi_eap_field_t field)
{
    switch (field) {
    case ESP32_MQUICKJS_WIFI_EAP_ANONYMOUS_IDENTITY:
    case ESP32_MQUICKJS_WIFI_EAP_USERNAME: return 128; /* Fixed SDK accepts 128; header prose says 127. */
    case ESP32_MQUICKJS_WIFI_EAP_PASSWORD:
    case ESP32_MQUICKJS_WIFI_EAP_NEW_PASSWORD:
    case ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY_PASSWORD: return 1024;
    case ESP32_MQUICKJS_WIFI_EAP_CA_CERT: return 32768;
    case ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT:
    case ESP32_MQUICKJS_WIFI_EAP_PAC: return 16384;
    case ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY: return 4096;
    case ESP32_MQUICKJS_WIFI_EAP_DOMAIN: return 255;
    default: return 0;
    }
}

static esp_err_t eap_profile_validate_lengths(const esp32_mquickjs_wifi_eap_input_t *input, size_t *bytes)
{
    if (!input || !bytes) return ESP_ERR_INVALID_ARG;
    const esp32_mquickjs_wifi_eap_policy_t *p = &input->policy;
    if (!p->methods || (p->methods & ~ESP_EAP_TYPE_ALL) ||
        p->ttls_phase2 > ESP_EAP_TTLS_PHASE2_CHAP || p->fast_provisioning > 2 ||
        p->fast_max_pac_list > 99) return ESP_ERR_INVALID_ARG;
#if CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT
    if ((p->methods & ESP_EAP_TYPE_FAST) || input->fields[ESP32_MQUICKJS_WIFI_EAP_PAC].length ||
        p->fast_provisioning || p->fast_max_pac_list || p->fast_binary) return ESP_ERR_NOT_SUPPORTED;
#else
    if (input->fields[ESP32_MQUICKJS_WIFI_EAP_DOMAIN].length) return ESP_ERR_NOT_SUPPORTED;
#endif
#if !CONFIG_ESP_WIFI_SUITE_B_192
    if (p->suiteb) return ESP_ERR_NOT_SUPPORTED;
#endif
#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE || !CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT
    if (p->default_bundle) return ESP_ERR_NOT_SUPPORTED;
#endif
    /* Lengths are checked before reading any caller bytes or adding padding. */
    size_t total = sizeof(esp32_mquickjs_wifi_eap_profile_t);
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT; ++i) {
        const esp32_mquickjs_wifi_eap_span_t *span = &input->fields[i];
        if ((span->data == NULL) != (span->length == 0) ||
            span->length > esp32_mquickjs_wifi_eap_field_limit(i)) return ESP_ERR_INVALID_ARG;
        if (!span->length) continue;
        size_t padded = span->length + 1; /* Bounded above before addition. */
        if (padded > ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_PROFILE_BYTES - total) return ESP_ERR_INVALID_SIZE;
        total += padded;
    }
    bool cert = input->fields[ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT].length != 0;
    bool key = input->fields[ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY].length != 0;
    if (cert != key || (!key && input->fields[ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY_PASSWORD].length))
        return ESP_ERR_INVALID_ARG;
    if ((p->methods & ESP_EAP_TYPE_TLS) && !key) return ESP_ERR_INVALID_ARG;
    if ((p->methods & (ESP_EAP_TYPE_PEAP | ESP_EAP_TYPE_TTLS)) &&
        !input->fields[ESP32_MQUICKJS_WIFI_EAP_USERNAME].length) return ESP_ERR_INVALID_ARG;
    size_t pac_length = input->fields[ESP32_MQUICKJS_WIFI_EAP_PAC].length;
    /* SDK silently discards PAC data shorter than 512 bytes. Provisioning is
     * explicit: no PAC plus a nonzero provisioning mode, never a dummy file. */
    if ((pac_length && pac_length < 512) ||
        (!(p->methods & ESP_EAP_TYPE_FAST) && (pac_length || p->fast_provisioning || p->fast_max_pac_list || p->fast_binary)) ||
        ((p->methods & ESP_EAP_TYPE_FAST) && !pac_length && !p->fast_provisioning))
        return ESP_ERR_INVALID_ARG;
    *bytes = total;
    return ESP_OK;
}

static esp_err_t eap_profile_validate(const esp32_mquickjs_wifi_eap_input_t *input, size_t *bytes)
{
    esp_err_t error = eap_profile_validate_lengths(input, bytes);
    if (error != ESP_OK) return error;
    /* SDK consumes these two values as C strings; appended padding must not
     * hide an embedded terminator. Other spans retain exact binary bytes. */
    unsigned string_fields[] = {ESP32_MQUICKJS_WIFI_EAP_DOMAIN, ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY_PASSWORD};
    for (unsigned i = 0; i < sizeof(string_fields) / sizeof(string_fields[0]); ++i) {
        const esp32_mquickjs_wifi_eap_span_t *span = &input->fields[string_fields[i]];
        if (span->length && memchr(span->data, 0, span->length)) return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t eap_profile_allocate(const esp32_mquickjs_wifi_eap_input_t *input,
    size_t bytes, esp32_mquickjs_wifi_eap_profile_t **output)
{
    portENTER_CRITICAL(&s_eap_profile_mux);
    bool admitted = s_eap_profile_counts.profiles < ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_PROFILES &&
        bytes <= ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_RETAINED_BYTES - s_eap_profile_counts.reserved_bytes;
    if (admitted) {
        ++s_eap_profile_counts.profiles;
        s_eap_profile_counts.reserved_bytes += bytes;
    }
    portEXIT_CRITICAL(&s_eap_profile_mux);
    if (!admitted) return ESP_ERR_NO_MEM;
    esp32_mquickjs_wifi_eap_profile_t *profile = esp32_mquickjs_memory_wireless_calloc("wifi.enterprise", 1, bytes, ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
    if (!profile) {
        portENTER_CRITICAL(&s_eap_profile_mux);
        --s_eap_profile_counts.profiles;
        s_eap_profile_counts.reserved_bytes -= bytes;
        portEXIT_CRITICAL(&s_eap_profile_mux);
        return ESP_ERR_NO_MEM;
    }
    profile->refs = 1;
    profile->bytes = bytes;
    profile->input.policy = input->policy;
    size_t offset = 0;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT; ++i) {
        size_t length = input->fields[i].length;
        if (!length) continue;
        profile->input.fields[i].data = profile->payload + offset;
        profile->input.fields[i].length = length;
        offset += length + 1;
    }
    *output = profile;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_eap_profile_create(const esp32_mquickjs_wifi_eap_input_t *input,
    esp32_mquickjs_wifi_eap_profile_t **output)
{
    if (!output) return ESP_ERR_INVALID_ARG;
    *output = NULL;
    size_t bytes;
    esp_err_t error = eap_profile_validate(input, &bytes);
    if (error != ESP_OK) return error;
    error = eap_profile_allocate(input, bytes, output);
    if (error != ESP_OK) return error;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT; ++i)
        if (input->fields[i].length)
            memcpy((void *)(*output)->input.fields[i].data, input->fields[i].data, input->fields[i].length);
    (*output)->sealed = true;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_eap_profile_begin(const esp32_mquickjs_wifi_eap_policy_t *policy,
    const size_t lengths[ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT], esp32_mquickjs_wifi_eap_profile_t **output)
{
    if (!output) return ESP_ERR_INVALID_ARG;
    *output = NULL;
    if (!policy || !lengths) return ESP_ERR_INVALID_ARG;
    /* The length validator never reads bytes. It uses only presence and size. */
    static const uint8_t present;
    esp32_mquickjs_wifi_eap_input_t input = {.policy = *policy};
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT; ++i)
        input.fields[i] = (esp32_mquickjs_wifi_eap_span_t){lengths[i] ? &present : NULL, lengths[i]};
    size_t bytes;
    esp_err_t error = eap_profile_validate_lengths(&input, &bytes);
    return error == ESP_OK ? eap_profile_allocate(&input, bytes, output) : error;
}

uint8_t *esp32_mquickjs_wifi_eap_profile_field(esp32_mquickjs_wifi_eap_profile_t *profile,
    esp32_mquickjs_wifi_eap_field_t field)
{
    if (!profile || profile->sealed || (unsigned)field >= ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT) return NULL;
    return (uint8_t *)profile->input.fields[field].data;
}

esp_err_t esp32_mquickjs_wifi_eap_profile_seal(esp32_mquickjs_wifi_eap_profile_t *profile)
{
    if (!profile || profile->sealed) return ESP_ERR_INVALID_ARG;
    size_t bytes;
    esp_err_t error = eap_profile_validate(&profile->input, &bytes);
    if (error == ESP_OK && bytes != profile->bytes) error = ESP_ERR_INVALID_SIZE;
    if (error == ESP_OK) profile->sealed = true;
    return error;
}

bool esp32_mquickjs_wifi_eap_profile_retain(esp32_mquickjs_wifi_eap_profile_t *profile)
{
    if (!profile) return false;
    portENTER_CRITICAL(&s_eap_profile_mux);
    bool retained = profile->sealed && profile->refs && profile->refs < UINT32_MAX;
    if (retained) ++profile->refs;
    portEXIT_CRITICAL(&s_eap_profile_mux);
    return retained;
}

void esp32_mquickjs_wifi_eap_profile_release(esp32_mquickjs_wifi_eap_profile_t *profile)
{
    if (!profile) return;
    portENTER_CRITICAL(&s_eap_profile_mux);
    bool destroy = profile->refs && --profile->refs == 0;
    portEXIT_CRITICAL(&s_eap_profile_mux);
    if (!destroy) return;
    size_t bytes = profile->bytes;
    esp32_mquickjs_wireless_secure_zero(profile, bytes);
    esp32_mquickjs_memory_payload_free(profile);
    /* Keep the reservation until both wipe and free have completed. */
    portENTER_CRITICAL(&s_eap_profile_mux);
    --s_eap_profile_counts.profiles;
    s_eap_profile_counts.reserved_bytes -= bytes;
    portEXIT_CRITICAL(&s_eap_profile_mux);
}

bool esp32_mquickjs_wifi_eap_profile_view(const esp32_mquickjs_wifi_eap_profile_t *profile,
    esp32_mquickjs_wifi_eap_input_t *output)
{
    if (!profile || !output || !profile->sealed) return false;
    *output = profile->input; /* Immutable and pinned by caller's existing ref. */
    return true;
}

void esp32_mquickjs_wifi_eap_profile_counts(esp32_mquickjs_wifi_eap_counts_t *output)
{
    if (!output) return;
    portENTER_CRITICAL(&s_eap_profile_mux);
    *output = s_eap_profile_counts;
    portEXIT_CRITICAL(&s_eap_profile_mux);
}
#endif
