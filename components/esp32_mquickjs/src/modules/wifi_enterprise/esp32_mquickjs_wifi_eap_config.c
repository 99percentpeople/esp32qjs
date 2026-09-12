#include "esp32_mquickjs_wifi_eap_config.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
#include "esp32_mquickjs_wifi_eap_radio.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static portMUX_TYPE s_eap_config_mux = portMUX_INITIALIZER_UNLOCKED;
static struct {
    esp32_mquickjs_wifi_eap_profile_t *configured, *operation_profile;
    uint64_t revision, next_identity;
    esp32_mquickjs_wifi_eap_config_token_t operation;
    bool closing;
} s_eap_config = {.closing = true};

/* Radio observation is always outside the critical section. A public control
 * first reserves operation under this mutex and keeps it until native return,
 * so replace/open cannot race a subsequent public SDK installation. Direct
 * SDK/Radio callers bypassing the config owner are internal-only boundaries. */
esp_err_t esp32_mquickjs_wifi_eap_config_open(void)
{
    if (esp32_mquickjs_wifi_radio_eap_identity()) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_eap_config_mux);
    bool allowed = !s_eap_config.configured && !s_eap_config.operation.identity && s_eap_config.revision != UINT64_MAX;
    if (allowed) { ++s_eap_config.revision; s_eap_config.closing = false; }
    portEXIT_CRITICAL(&s_eap_config_mux);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_eap_config_status(esp32_mquickjs_wifi_eap_config_status_t *output)
{
    if (!output) return;
    portENTER_CRITICAL(&s_eap_config_mux);
    *output = (esp32_mquickjs_wifi_eap_config_status_t){.revision = s_eap_config.revision,
        .configured = s_eap_config.configured != NULL, .busy = s_eap_config.operation.identity != 0,
        .closing = s_eap_config.closing, .revision_exhausted = s_eap_config.revision == UINT64_MAX,
        .identity_exhausted = s_eap_config.next_identity == UINT64_MAX};
    portEXIT_CRITICAL(&s_eap_config_mux);
}

esp_err_t esp32_mquickjs_wifi_eap_config_replace(uint64_t expected_revision,
    esp32_mquickjs_wifi_eap_profile_t *candidate)
{
    if (!candidate) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_eap_input_t view;
    if (!esp32_mquickjs_wifi_eap_profile_view(candidate, &view)) return ESP_ERR_INVALID_ARG;
    if (esp32_mquickjs_wifi_radio_eap_identity()) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_eap_profile_t *old = NULL;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_eap_config_mux);
    if (!s_eap_config.closing && !s_eap_config.operation.identity &&
        expected_revision == s_eap_config.revision && s_eap_config.revision != UINT64_MAX) {
        /* Lock order config -> profile; retain does not allocate/free or call SDK. */
        if (esp32_mquickjs_wifi_eap_profile_retain(candidate)) {
            old = s_eap_config.configured;
            s_eap_config.configured = candidate;
            ++s_eap_config.revision;
            error = ESP_OK;
        } else error = ESP_ERR_NO_MEM;
    }
    portEXIT_CRITICAL(&s_eap_config_mux);
    esp32_mquickjs_wifi_eap_profile_release(old);
    return error;
}

esp_err_t esp32_mquickjs_wifi_eap_config_begin(uint64_t expected_revision,
    esp32_mquickjs_wifi_eap_config_action_t action, esp32_mquickjs_wifi_eap_config_token_t *token,
    esp32_mquickjs_wifi_eap_profile_t **profile)
{
    if (profile) *profile = NULL;
    if (!token || !profile || token->identity || token->revision || token->action ||
        action < ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE || action > ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR)
        return ESP_ERR_INVALID_ARG;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_eap_config_mux);
    if (!s_eap_config.closing && !s_eap_config.operation.identity &&
        expected_revision == s_eap_config.revision && s_eap_config.next_identity != UINT64_MAX &&
        (action != ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE || s_eap_config.configured)) {
        if (!s_eap_config.configured || esp32_mquickjs_wifi_eap_profile_retain(s_eap_config.configured)) {
            s_eap_config.operation_profile = s_eap_config.configured;
            s_eap_config.operation = (esp32_mquickjs_wifi_eap_config_token_t){
                .identity = ++s_eap_config.next_identity, .revision = s_eap_config.revision, .action = action};
            *token = s_eap_config.operation;
            *profile = s_eap_config.operation_profile;
            error = ESP_OK;
        } else error = ESP_ERR_NO_MEM;
    }
    portEXIT_CRITICAL(&s_eap_config_mux);
    return error;
}

esp_err_t esp32_mquickjs_wifi_eap_config_finish(esp32_mquickjs_wifi_eap_config_token_t *token,
    bool discard_configuration)
{
    if (!token || !token->identity) return ESP_ERR_INVALID_ARG;
    bool borrowed = esp32_mquickjs_wifi_radio_eap_identity() != 0;
    esp32_mquickjs_wifi_eap_profile_t *operation_profile = NULL, *configured = NULL;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_eap_config_mux);
    if (token->identity == s_eap_config.operation.identity && token->revision == s_eap_config.operation.revision &&
        token->action == s_eap_config.operation.action) {
        bool discard = discard_configuration && token->action == ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR && !borrowed;
        if (discard) {
            configured = s_eap_config.configured;
            s_eap_config.configured = NULL;
            if (s_eap_config.revision != UINT64_MAX) ++s_eap_config.revision;
        }
        operation_profile = s_eap_config.operation_profile;
        s_eap_config.operation_profile = NULL;
        memset(&s_eap_config.operation, 0, sizeof(s_eap_config.operation));
        memset(token, 0, sizeof(*token));
        error = !discard_configuration || discard ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_eap_config_mux);
    esp32_mquickjs_wifi_eap_profile_release(operation_profile);
    esp32_mquickjs_wifi_eap_profile_release(configured);
    return error;
}

void esp32_mquickjs_wifi_eap_config_begin_close(void)
{
    portENTER_CRITICAL(&s_eap_config_mux);
    if (!s_eap_config.closing) {
        s_eap_config.closing = true;
        if (s_eap_config.revision != UINT64_MAX) ++s_eap_config.revision;
    }
    portEXIT_CRITICAL(&s_eap_config_mux);
}

bool esp32_mquickjs_wifi_eap_config_finish_close(void)
{
    if (esp32_mquickjs_wifi_radio_eap_identity()) return false;
    esp32_mquickjs_wifi_eap_profile_t *profile = NULL;
    portENTER_CRITICAL(&s_eap_config_mux);
    bool ready = s_eap_config.closing && !s_eap_config.operation.identity;
    if (ready) { profile = s_eap_config.configured; s_eap_config.configured = NULL; }
    portEXIT_CRITICAL(&s_eap_config_mux);
    esp32_mquickjs_wifi_eap_profile_release(profile);
    return ready;
}
#endif
