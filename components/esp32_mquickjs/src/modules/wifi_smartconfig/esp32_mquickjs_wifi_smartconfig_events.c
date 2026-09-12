#include "esp32_mquickjs_wifi_smartconfig_events.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_wireless_core.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

typedef struct {
    esp32_mquickjs_wifi_smartconfig_events_status_t status;
    esp32_mquickjs_wifi_smartconfig_credentials_t credentials;
} smartconfig_events_t;

static portMUX_TYPE s_smartconfig_events_lock = portMUX_INITIALIZER_UNLOCKED;
static smartconfig_events_t *s_smartconfig_events;
static uint64_t s_smartconfig_last_identity;
static bool s_smartconfig_reserving;

/* Reviewed C3/S3/C5 libsmartconfig.a, pinned by patch_idf_smartconfig.py:
 * TOUCH_v2_init_glob allocates 0xb08 bytes. The binary count is at 0xa41;
 * custom data starts at 0xaac (65-byte SDK region, including its terminator).
 * This is called synchronously at the native GOT_SSID_PSWD post boundary,
 * while the Wi-Fi task still owns the decoder. Never call after native stop.
 * The public SDK getter truncates to len-1 and cannot provide this length. */
extern uint8_t *g_config_data;
static esp_err_t smartconfig_custom_capture(esp32_mquickjs_wifi_smartconfig_credentials_t *credentials)
{
    if (!g_config_data) return ESP_ERR_INVALID_STATE;
    uint8_t length = g_config_data[0xa41];
    if (length > sizeof(credentials->custom_data)) return ESP_ERR_INVALID_SIZE;
    memcpy(credentials->custom_data, g_config_data + 0xaac, length);
    credentials->custom_length = length;
    return ESP_OK;
}

_Static_assert(sizeof(smartconfig_event_got_ssid_pswd_t) == 116 &&
    offsetof(smartconfig_event_got_ssid_pswd_t, password) == 32 &&
    offsetof(smartconfig_event_got_ssid_pswd_t, bssid_set) == 96 &&
    offsetof(smartconfig_event_got_ssid_pswd_t, type) == 104 &&
    sizeof(smartconfig_type_t) == 4, "review SmartConfig credential event ABI");

static bool smartconfig_events_exact(const esp32_mquickjs_wifi_smartconfig_token_t *token)
{
    return token && token->identity && s_smartconfig_events &&
        s_smartconfig_events->status.token.identity == token->identity &&
        s_smartconfig_events->status.token.radio_generation == token->radio_generation;
}

static void smartconfig_events_count(uint32_t *counter)
{
    if (*counter != UINT32_MAX) ++*counter;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_events_begin(uint32_t generation,
    esp32_mquickjs_wifi_smartconfig_token_t *token)
{
    if (!generation || !token || token->identity || token->radio_generation) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_smartconfig_events_lock);
    esp_err_t error = s_smartconfig_events || s_smartconfig_reserving ? ESP_ERR_INVALID_STATE :
        s_smartconfig_last_identity == UINT64_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    if (error == ESP_OK) s_smartconfig_reserving = true;
    portEXIT_CRITICAL(&s_smartconfig_events_lock);
    if (error != ESP_OK) return error;
    smartconfig_events_t *fresh = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    portENTER_CRITICAL(&s_smartconfig_events_lock);
    if (fresh) {
        fresh->status.token = (esp32_mquickjs_wifi_smartconfig_token_t){
            .identity = ++s_smartconfig_last_identity, .radio_generation = generation};
        fresh->status.reserved_bytes = sizeof(*fresh);
        *token = fresh->status.token;
        s_smartconfig_events = fresh;
    } else error = ESP_ERR_NO_MEM;
    s_smartconfig_reserving = false;
    portEXIT_CRITICAL(&s_smartconfig_events_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_events_status(
    const esp32_mquickjs_wifi_smartconfig_token_t *token,
    esp32_mquickjs_wifi_smartconfig_events_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_smartconfig_events_lock);
    bool exact = smartconfig_events_exact(token);
    if (exact) *status = s_smartconfig_events->status;
    portEXIT_CRITICAL(&s_smartconfig_events_lock);
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_smartconfig_allocation_failed(void)
{
    portENTER_CRITICAL(&s_smartconfig_events_lock);
    if (s_smartconfig_events && !s_smartconfig_events->status.closing)
        s_smartconfig_events->status.allocation_error = ESP_ERR_NO_MEM;
    portEXIT_CRITICAL(&s_smartconfig_events_lock);
}

esp_err_t esp32_mquickjs_wifi_smartconfig_credentials_copy(
    const esp32_mquickjs_wifi_smartconfig_token_t *token,
    esp32_mquickjs_wifi_smartconfig_credentials_t *credentials)
{
    if (!credentials) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_smartconfig_events_lock);
    bool exact = smartconfig_events_exact(token);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (exact && !s_smartconfig_events->status.closing && s_smartconfig_events->status.allocation_error != ESP_OK)
        error = s_smartconfig_events->status.allocation_error;
    else if (exact && !s_smartconfig_events->status.closing &&
        s_smartconfig_events->status.credentials_received &&
        !s_smartconfig_events->status.credentials_consumed) {
        *credentials = s_smartconfig_events->credentials;
        error = ESP_OK;
    }
    portEXIT_CRITICAL(&s_smartconfig_events_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_credentials_commit(
    const esp32_mquickjs_wifi_smartconfig_token_t *token)
{
    portENTER_CRITICAL(&s_smartconfig_events_lock);
    bool exact = smartconfig_events_exact(token);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (exact && !s_smartconfig_events->status.closing && s_smartconfig_events->status.allocation_error != ESP_OK)
        error = s_smartconfig_events->status.allocation_error;
    else if (exact && !s_smartconfig_events->status.closing &&
        s_smartconfig_events->status.credentials_received &&
        !s_smartconfig_events->status.credentials_consumed) {
        esp32_mquickjs_wireless_secure_zero(&s_smartconfig_events->credentials,
            sizeof(s_smartconfig_events->credentials));
        s_smartconfig_events->status.credentials_consumed = true;
        error = ESP_OK;
    }
    portEXIT_CRITICAL(&s_smartconfig_events_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_events_close(
    const esp32_mquickjs_wifi_smartconfig_token_t *token)
{
    portENTER_CRITICAL(&s_smartconfig_events_lock);
    bool exact = smartconfig_events_exact(token);
    if (exact) {
        s_smartconfig_events->status.closing = true;
        esp32_mquickjs_wireless_secure_zero(&s_smartconfig_events->credentials,
            sizeof(s_smartconfig_events->credentials));
    }
    portEXIT_CRITICAL(&s_smartconfig_events_lock);
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_events_release(
    esp32_mquickjs_wifi_smartconfig_token_t *token)
{
    smartconfig_events_t *retired = NULL;
    portENTER_CRITICAL(&s_smartconfig_events_lock);
    if (smartconfig_events_exact(token) && s_smartconfig_events->status.closing) {
        retired = s_smartconfig_events;
        s_smartconfig_events = NULL;
        memset(token, 0, sizeof(*token));
    }
    portEXIT_CRITICAL(&s_smartconfig_events_lock);
    if (!retired) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wireless_secure_zero(retired, sizeof(*retired));
    esp32_mquickjs_memory_payload_free(retired);
    return ESP_OK;
}

bool esp32_mquickjs_wifi_smartconfig_event_capture(int32_t event_id,
    const void *data, size_t length)
{
    portENTER_CRITICAL(&s_smartconfig_events_lock);
    smartconfig_events_t *entry = s_smartconfig_events;
    if (!entry) {
        portEXIT_CRITICAL(&s_smartconfig_events_lock);
        return false;
    }
    esp32_mquickjs_wifi_smartconfig_events_status_t *status = &entry->status;
    if (status->closing || status->allocation_error != ESP_OK) {
        smartconfig_events_count(&status->discarded_events);
        goto done;
    }
    bool valid = length == 0;
    if (event_id == SC_EVENT_GOT_SSID_PSWD) {
        /* Validate raw representations before reading the SDK bool/enum. Do
         * not assume NUL termination at maximum SSID/password capacity. */
        smartconfig_type_t type = SC_TYPE_ESPTOUCH_AIRKISS;
        valid = data && length == sizeof(entry->credentials.network);
        if (valid) {
            memcpy(&type, (const uint8_t *)data + offsetof(smartconfig_event_got_ssid_pswd_t, type), sizeof(type));
            valid = ((const uint8_t *)data)[offsetof(smartconfig_event_got_ssid_pswd_t, bssid_set)] <= 1 &&
                (type >= SC_TYPE_ESPTOUCH && type <= SC_TYPE_ESPTOUCH_V2);
        }
        if (valid) {
            if (status->credentials_received) smartconfig_events_count(&status->duplicate_credentials);
            else {
                esp_err_t error = type == SC_TYPE_ESPTOUCH_V2
                    ? smartconfig_custom_capture(&entry->credentials) : ESP_OK;
                if (error != ESP_OK) {
                    if (status->event_error == ESP_OK) status->event_error = error;
                    smartconfig_events_count(&status->discarded_events);
                    goto done;
                }
                /* Padding is not payload. Copy only declared fields into the
                 * zeroed record; it is the single retained credential owner. */
                const smartconfig_event_got_ssid_pswd_t *value = data;
                memcpy(entry->credentials.network.ssid, value->ssid, sizeof(value->ssid));
                memcpy(entry->credentials.network.password, value->password, sizeof(value->password));
                entry->credentials.network.bssid_set = value->bssid_set;
                memcpy(entry->credentials.network.bssid, value->bssid, sizeof(value->bssid));
                entry->credentials.network.type = type;
                entry->credentials.network.token = value->token;
                memcpy(entry->credentials.network.cellphone_ip, value->cellphone_ip, sizeof(value->cellphone_ip));
                status->credentials_received = true;
            }
        }
    } else if (event_id == SC_EVENT_SCAN_DONE) {
        if (valid) status->scan_done = true;
    } else if (event_id == SC_EVENT_FOUND_CHANNEL) {
        if (valid) status->channel_found = true;
    } else if (event_id == SC_EVENT_SEND_ACK_DONE) {
        if (valid) status->ack_observed = true;
    } else valid = false;
    if (valid) smartconfig_events_count(&status->captured_events);
    else {
        if (status->event_error == ESP_OK) status->event_error = ESP_ERR_INVALID_SIZE;
        smartconfig_events_count(&status->discarded_events);
    }
done:
    portEXIT_CRITICAL(&s_smartconfig_events_lock);
    return true;
}

esp_err_t __real_esp_event_post(esp_event_base_t base, int32_t event_id,
    const void *data, size_t length, TickType_t ticks_to_wait);

esp_err_t __wrap_esp_event_post(esp_event_base_t base, int32_t event_id,
    const void *data, size_t length, TickType_t ticks_to_wait)
{
    if (base == SC_EVENT && esp32_mquickjs_wifi_smartconfig_event_capture(event_id, data, length))
        return ESP_OK;
    return __real_esp_event_post(base, event_id, data, length, ticks_to_wait);
}
#endif
