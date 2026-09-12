#include "esp32_mquickjs_wifi_smartconfig_session.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_wifi_smartconfig_connection.h"
#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

struct esp32_mquickjs_wifi_smartconfig_session {
    uint32_t references, timeout_ms;
    uint32_t receive_waiters, close_waiters;
    esp32_mquickjs_wifi_smartconfig_session_status_t status;
    esp32_mquickjs_wifi_radio_lease_t owners[3];
    esp32_mquickjs_wifi_radio_operation_t operation;
    esp32_mquickjs_wifi_smartconfig_decoder_options_t options;
    esp32_mquickjs_wifi_smartconfig_credentials_t credentials;
    uint8_t key[16];
    int64_t deadline_us, next_retry_us;
    bool worker_started, ack_submitted, allow_ap_channel_change;
    bool connection_attempted, connection_released, finish_requested;
    esp32_mquickjs_wifi_smartconfig_connection_options_t connection_options;
};
static portMUX_TYPE s_sc_session_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_smartconfig_session_t *s_sc_active;
static unsigned s_sc_handles, s_sc_workers;
static bool s_sc_runtime_closing = true;

static void sc_session_close_locked(esp32_mquickjs_wifi_smartconfig_session_t *s, bool timeout)
{
    if (!s->status.closing && timeout && (!s->status.credentials_ready ||
        (s->status.auto_connect && !s->status.connection_transferred))) {
        s->status.timed_out = true;
        if (s->status.error == ESP_OK) {
            s->status.error = ESP_ERR_TIMEOUT; s->status.stage = "smartconfig-timeout";
        }
    }
    s->status.closing = true;
    s->next_retry_us = 0;
    if (!s->status.worker_busy) {
        esp32_mquickjs_wireless_secure_zero(&s->credentials, sizeof(s->credentials));
        esp32_mquickjs_wireless_secure_zero(s->key, sizeof(s->key));
    }
    if (!s->status.activated) s->status.retired = true;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_open_runtime(void)
{
    portENTER_CRITICAL(&s_sc_session_lock);
    bool ready = !s_sc_active && !s_sc_workers && !s_sc_handles;
    if (ready) s_sc_runtime_closing = false;
    portEXIT_CRITICAL(&s_sc_session_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_session_create(
    const esp32_mquickjs_wifi_smartconfig_decoder_options_t *options,
    bool allow_ap_channel_change, uint32_t timeout_ms,
    esp32_mquickjs_wifi_smartconfig_session_t **out)
{
    if (!out || *out || !timeout_ms) return ESP_ERR_INVALID_ARG;
    esp_err_t error = esp32_mquickjs_wifi_smartconfig_decoder_validate_options(options);
    if (error != ESP_OK) return error;
    esp32_mquickjs_wifi_radio_lease_t owners[3];
    error = esp32_mquickjs_wifi_smartconfig_capture_owners(owners);
    if (error != ESP_OK) return error;
    portENTER_CRITICAL(&s_sc_session_lock);
    error = s_sc_runtime_closing ? ESP_ERR_INVALID_STATE :
        s_sc_handles >= ESP32_MQUICKJS_SMARTCONFIG_MAX_HANDLES ? ESP_ERR_NO_MEM : ESP_OK;
    if (error == ESP_OK) ++s_sc_handles;
    portEXIT_CRITICAL(&s_sc_session_lock);
    if (error != ESP_OK) return error;
    esp32_mquickjs_wifi_smartconfig_session_t *s = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*s), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!s) {
        portENTER_CRITICAL(&s_sc_session_lock); --s_sc_handles; portEXIT_CRITICAL(&s_sc_session_lock);
        return ESP_ERR_NO_MEM;
    }
    s->references = 1; s->timeout_ms = timeout_ms;
    s->options = *options;
    if (options->key_length) { memcpy(s->key, options->key, sizeof(s->key)); s->options.key = s->key; }
    memcpy(s->owners, owners, sizeof(owners));
    s->allow_ap_channel_change = allow_ap_channel_change;
    s->status.reserved_bytes = sizeof(*s);
    *out = s;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_connection_validate_options(
    const esp32_mquickjs_wifi_smartconfig_connection_options_t *options)
{
    if (!options || !options->timeout_ms || options->timeout_ms > 3600000 ||
        (options->minimum_auth != WIFI_AUTH_WPA2_PSK && options->minimum_auth != WIFI_AUTH_WPA3_PSK) ||
        (options->minimum_auth == WIFI_AUTH_WPA3_PSK && !options->pmf_required) ||
        (options->allow_open && (options->pmf_required || options->minimum_auth != WIFI_AUTH_WPA2_PSK)))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
    if (options->minimum_auth == WIFI_AUTH_WPA3_PSK) return ESP_ERR_NOT_SUPPORTED;
#endif
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_session_configure_connection(
    esp32_mquickjs_wifi_smartconfig_session_t *s,
    const esp32_mquickjs_wifi_smartconfig_connection_options_t *options)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    esp_err_t error = esp32_mquickjs_wifi_smartconfig_connection_validate_options(options);
    if (error != ESP_OK) return error;
    portENTER_CRITICAL(&s_sc_session_lock);
    bool ready = !s->status.activated && !s->status.closing;
    if (ready) { s->connection_options = *options; s->status.auto_connect = true; }
    portEXIT_CRITICAL(&s_sc_session_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool esp32_mquickjs_wifi_smartconfig_session_retain(esp32_mquickjs_wifi_smartconfig_session_t *s)
{
    if (!s) return false;
    portENTER_CRITICAL(&s_sc_session_lock);
    bool ok = s->references && s->references != UINT32_MAX;
    if (ok) ++s->references;
    portEXIT_CRITICAL(&s_sc_session_lock);
    return ok;
}

void esp32_mquickjs_wifi_smartconfig_session_release(esp32_mquickjs_wifi_smartconfig_session_t *s)
{
    if (!s) return;
    portENTER_CRITICAL(&s_sc_session_lock);
    bool destroy = --s->references == 0;
    if (s->references == 1 && s_sc_active == s) sc_session_close_locked(s, false);
    portEXIT_CRITICAL(&s_sc_session_lock);
    if (!destroy) return;
    esp32_mquickjs_wireless_secure_zero(s, sizeof(*s));
    esp32_mquickjs_memory_payload_free(s);
    portENTER_CRITICAL(&s_sc_session_lock); --s_sc_handles; portEXIT_CRITICAL(&s_sc_session_lock);
}

esp_err_t esp32_mquickjs_wifi_smartconfig_session_activate(esp32_mquickjs_wifi_smartconfig_session_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time();
    int64_t duration = (int64_t)s->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_sc_session_lock);
    bool allowed = !s_sc_runtime_closing && !s_sc_active && !s->status.activated &&
        !s->status.closing && s->references != UINT32_MAX;
    if (allowed) {
        ++s->references; s_sc_active = s;
        s->status.activated = true; s->deadline_us = now + duration;
    }
    portEXIT_CRITICAL(&s_sc_session_lock);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_smartconfig_session_close(esp32_mquickjs_wifi_smartconfig_session_t *s, bool timeout)
{
    if (!s) return;
    portENTER_CRITICAL(&s_sc_session_lock);
    sc_session_close_locked(s, timeout);
    portEXIT_CRITICAL(&s_sc_session_lock);
}

bool esp32_mquickjs_wifi_smartconfig_session_status(esp32_mquickjs_wifi_smartconfig_session_t *s,
    esp32_mquickjs_wifi_smartconfig_session_status_t *status)
{
    if (!s || !status) return false;
    portENTER_CRITICAL(&s_sc_session_lock); *status = s->status; portEXIT_CRITICAL(&s_sc_session_lock);
    return true;
}

bool esp32_mquickjs_wifi_smartconfig_session_wait_begin(esp32_mquickjs_wifi_smartconfig_session_t *s, bool close)
{
    portENTER_CRITICAL(&s_sc_session_lock);
    uint32_t *count = close ? &s->close_waiters : &s->receive_waiters;
    bool ok = *count != UINT32_MAX;
    if (ok) ++*count;
    portEXIT_CRITICAL(&s_sc_session_lock);
    return ok;
}

void esp32_mquickjs_wifi_smartconfig_session_wait_end(esp32_mquickjs_wifi_smartconfig_session_t *s, bool close)
{
    portENTER_CRITICAL(&s_sc_session_lock);
    uint32_t *count = close ? &s->close_waiters : &s->receive_waiters;
    if (*count) --*count;
    portEXIT_CRITICAL(&s_sc_session_lock);
}

bool esp32_mquickjs_wifi_smartconfig_session_observation(esp32_mquickjs_wifi_smartconfig_session_t *s,
    esp32_mquickjs_wifi_smartconfig_session_status_t *status)
{
    portENTER_CRITICAL(&s_sc_session_lock);
    *status = s->status;
    bool receive_ready = status->error || status->closing || status->credentials_consumed ||
        (status->credentials_ready && (!status->auto_connect || status->completed));
    bool ready = !(s->receive_waiters && receive_ready) && !(s->close_waiters && status->retired);
    portEXIT_CRITICAL(&s_sc_session_lock);
    return ready;
}

void esp32_mquickjs_wifi_smartconfig_global_status(esp32_mquickjs_wifi_smartconfig_global_status_t *status)
{
    if (!status) return;
    portENTER_CRITICAL(&s_sc_session_lock);
    *status = (esp32_mquickjs_wifi_smartconfig_global_status_t){
        .handles = s_sc_handles, .workers = s_sc_workers,
        .active = s_sc_active != NULL, .runtime_closing = s_sc_runtime_closing,
    };
    if (s_sc_active) status->session = s_sc_active->status;
    portEXIT_CRITICAL(&s_sc_session_lock);
}

esp_err_t esp32_mquickjs_wifi_smartconfig_session_credentials(esp32_mquickjs_wifi_smartconfig_session_t *s,
    esp32_mquickjs_wifi_smartconfig_credentials_t *credentials, bool commit)
{
    if (!s || (!commit && !credentials) || (commit && credentials)) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_sc_session_lock);
    bool ready = !s->status.closing && !s->status.worker_busy && s->status.credentials_ready &&
        !s->status.credentials_consumed && (!s->status.auto_connect || s->status.completed);
    if (ready) {
        if (commit) {
            esp32_mquickjs_wireless_secure_zero(&s->credentials, sizeof(s->credentials));
            s->status.credentials_consumed = true;
        } else *credentials = s->credentials;
    }
    portEXIT_CRITICAL(&s_sc_session_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_session_ack(esp32_mquickjs_wifi_smartconfig_session_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_sc_session_lock);
    bool ready = !s->status.closing && s->status.credentials_ready && !s->status.ack_requested;
    if (ready) { s->status.ack_requested = true; s->next_retry_us = 0; }
    portEXIT_CRITICAL(&s_sc_session_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static void sc_session_fail(esp32_mquickjs_wifi_smartconfig_session_t *s, esp_err_t error, const char *stage)
{
    portENTER_CRITICAL(&s_sc_session_lock);
    if (!s->status.closing && s->status.error == ESP_OK) { s->status.error = error; s->status.stage = stage; }
    sc_session_close_locked(s, false);
    portEXIT_CRITICAL(&s_sc_session_lock);
}

static bool sc_session_closing(esp32_mquickjs_wifi_smartconfig_session_t *s)
{
    portENTER_CRITICAL(&s_sc_session_lock); bool closing = s->status.closing; portEXIT_CRITICAL(&s_sc_session_lock);
    return closing;
}

static void sc_session_worker(void *opaque)
{
    esp32_mquickjs_wifi_smartconfig_session_t *s = opaque;
    portENTER_CRITICAL(&s_sc_session_lock);
    bool first = !s->worker_started && !s->status.closing;
    s->worker_started = true;
    esp32_mquickjs_wifi_radio_operation_t token = s->operation;
    esp32_mquickjs_wifi_smartconfig_radio_status_t native = s->status.native;
    bool have_credentials = s->status.credentials_ready;
    portEXIT_CRITICAL(&s_sc_session_lock);
    esp_err_t error = ESP_OK;
    if (first) {
        error = esp32_mquickjs_wifi_radio_smartconfig_begin(s->owners, s->allow_ap_channel_change, &s->options, &token, &native);
        esp32_mquickjs_wireless_secure_zero(s->key, sizeof(s->key));
        s->options.key = NULL; s->options.key_length = 0;
        if (error != ESP_OK) sc_session_fail(s, error, native.stage);
    }
    if (token.identity && !sc_session_closing(s)) {
        error = esp32_mquickjs_wifi_radio_smartconfig_status(&token, &native);
        if (error == ESP_OK) error = native.decoder.error ? native.decoder.error : native.events.event_error;
        if (error != ESP_OK) sc_session_fail(s, error, native.stage ? native.stage : "smartconfig-native-status");
        if (!sc_session_closing(s) && native.events.credentials_received && !have_credentials) {
            error = esp32_mquickjs_wifi_radio_smartconfig_finish_capture(&token, &native);
            if (error == ESP_OK) error = esp32_mquickjs_wifi_radio_smartconfig_credentials(&token, &s->credentials, false);
            if (error == ESP_OK) error = esp32_mquickjs_wifi_radio_smartconfig_credentials(&token, NULL, true);
            if (error != ESP_OK && error != ESP_ERR_NOT_FINISHED)
                sc_session_fail(s, error, native.stage ? native.stage : "smartconfig-credential-transfer");
            else if (error == ESP_OK) {
                portENTER_CRITICAL(&s_sc_session_lock);
                if (!s->status.closing) { s->status.credentials_ready = true; if (!s->status.auto_connect) s->deadline_us = 0; }
                portEXIT_CRITICAL(&s_sc_session_lock);
            }
        }
        portENTER_CRITICAL(&s_sc_session_lock);
        bool ack = !s->status.closing && s->status.ack_requested && !s->ack_submitted;
        if (ack) s->ack_submitted = true;
        portEXIT_CRITICAL(&s_sc_session_lock);
        if (ack) {
            error = esp32_mquickjs_wifi_radio_smartconfig_ack(&token, &native);
            if (error != ESP_OK) sc_session_fail(s, error, "smartconfig-ack-start");
        }
        if (!sc_session_closing(s) && s->ack_submitted && native.decoder.ack_identity &&
            !native.decoder.ack_busy && native.decoder.ack_error != ESP_OK)
            sc_session_fail(s, native.decoder.ack_error, "smartconfig-ack-send");
    }
    esp_err_t cleanup_error = error == ESP_ERR_NOT_FINISHED ? native.error : ESP_OK;
    if ((sc_session_closing(s) || s->finish_requested) && token.identity)
        cleanup_error = esp32_mquickjs_wifi_radio_smartconfig_close(&token, &native);
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_sc_session_lock);
    s->operation = token; s->status.native = native;
    s->status.cleanup_error = cleanup_error;
    s->status.cleanup_stage = cleanup_error == ESP_OK ? NULL : native.stage;
    s->status.retired = (s->status.closing || s->finish_requested) && token.identity == 0;
    if (s->status.retired && s->finish_requested && !s->status.closing) s->status.completed = true;
    if (s->status.closing) {
        esp32_mquickjs_wireless_secure_zero(&s->credentials, sizeof(s->credentials));
        esp32_mquickjs_wireless_secure_zero(s->key, sizeof(s->key));
        s->options.key = NULL; s->options.key_length = 0;
    }
    s->status.worker_busy = false;
    s->next_retry_us = now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    bool detach = s->status.retired && s_sc_active == s;
    if (detach) s_sc_active = NULL;
    portEXIT_CRITICAL(&s_sc_session_lock);
    if (detach) esp32_mquickjs_wifi_smartconfig_session_release(s); /* Registry. */
    esp32_mquickjs_wifi_smartconfig_session_release(s); /* Worker. */
    portENTER_CRITICAL(&s_sc_session_lock); --s_sc_workers; portEXIT_CRITICAL(&s_sc_session_lock);
}

static esp_err_t sc_session_connection_config(esp32_mquickjs_wifi_smartconfig_session_t *s, wifi_config_t *config)
{
    memset(config, 0, sizeof(*config));
    size_t ssid_length = strnlen((const char *)s->credentials.network.ssid, sizeof(s->credentials.network.ssid));
    size_t password_length = strnlen((const char *)s->credentials.network.password, sizeof(s->credentials.network.password));
    if (!ssid_length || (!password_length && !s->connection_options.allow_open) ||
        (password_length && s->connection_options.minimum_auth == WIFI_AUTH_WPA2_PSK && password_length < 8) ||
        (password_length == 64 && s->connection_options.minimum_auth == WIFI_AUTH_WPA3_PSK)) return ESP_ERR_INVALID_ARG;
    if (password_length == 64) for (unsigned i = 0; i < 64; ++i) {
        uint8_t c = s->credentials.network.password[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return ESP_ERR_INVALID_ARG;
    }
    if (s->credentials.network.bssid_set) {
        uint8_t any = 0;
        for (unsigned i = 0; i < 6; ++i) any |= s->credentials.network.bssid[i];
        if (!any || (s->credentials.network.bssid[0] & 1U)) return ESP_ERR_INVALID_ARG;
    }
    memcpy(config->sta.ssid, s->credentials.network.ssid, ssid_length);
    memcpy(config->sta.password, s->credentials.network.password, password_length);
    config->sta.bssid_set = s->credentials.network.bssid_set;
    if (config->sta.bssid_set) memcpy(config->sta.bssid, s->credentials.network.bssid, sizeof(config->sta.bssid));
    config->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config->sta.threshold.rssi = -127;
    config->sta.threshold.authmode = password_length ? s->connection_options.minimum_auth : WIFI_AUTH_OPEN;
    config->sta.pmf_cfg.capable = true;
    config->sta.pmf_cfg.required = s->connection_options.pmf_required;
#if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
    config->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#endif
    return ESP_OK;
}

/* Only the runtime task calls service. Claim worker_busy while borrowing the
 * existing Station helper; background decoder work cannot race this handoff.
 * No JS, observer delivery or user callback occurs inside these native hooks. */
static bool sc_session_progress_connection(int64_t now)
{
    portENTER_CRITICAL(&s_sc_session_lock);
    esp32_mquickjs_wifi_smartconfig_session_t *s = s_sc_active;
    bool run = s && s->status.auto_connect && !s->status.worker_busy &&
        !s->status.retired && !s->connection_released && now >= s->next_retry_us &&
        ((!s->status.closing && s->status.credentials_ready) ||
        (s->status.closing && s->status.connection_generation)) &&
        s->references != UINT32_MAX;
    if (run) { ++s->references; s->status.worker_busy = true; }
    portEXIT_CRITICAL(&s_sc_session_lock);
    if (!run) return false;
    esp_err_t error = ESP_OK;
    const char *stage = "smartconfig-connect-submit";
    if (!sc_session_closing(s) && !s->connection_attempted) {
        s->connection_attempted = true;
        wifi_config_t config;
        error = sc_session_connection_config(s, &config);
        uint32_t generation = 0;
        if (error == ESP_OK) error = esp32_mquickjs_wifi_smartconfig_connect_begin(
            &s->operation, &config, s->connection_options.timeout_ms, &generation);
        esp32_mquickjs_wireless_secure_zero(&config, sizeof(config));
        portENTER_CRITICAL(&s_sc_session_lock);
        s->status.connection_generation = generation;
        s->status.connection_started = error == ESP_OK;
        portEXIT_CRITICAL(&s_sc_session_lock);
        if (error == ESP_ERR_NOT_FINISHED && !generation) s->connection_attempted = false;
        else if (error != ESP_OK) sc_session_fail(s, error, stage);
    }
    if (s->status.connection_generation && !sc_session_closing(s)) {
        esp32_mquickjs_wifi_smartconfig_connection_status_t connection = {0};
        stage = "smartconfig-connect-result";
        error = esp32_mquickjs_wifi_smartconfig_connect_status(&s->operation, s->status.connection_generation, &connection);
        if (error == ESP_OK && connection.terminal &&
            (connection.terminal != ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS || !connection.connected))
            error = connection.terminal == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        portENTER_CRITICAL(&s_sc_session_lock);
        s->status.connection_reason = connection.reason;
        if (error == ESP_OK && connection.terminal == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS)
            s->status.ack_requested = true;
        portEXIT_CRITICAL(&s_sc_session_lock);
        if (error != ESP_OK) sc_session_fail(s, error, stage);
    }
    bool complete = !sc_session_closing(s) && s->status.native.decoder.ack_identity &&
        s->status.native.decoder.ack_completed && !s->status.native.decoder.ack_busy &&
        s->status.native.decoder.ack_error == ESP_OK;
    if (s->status.connection_generation && (sc_session_closing(s) || complete)) {
        stage = "smartconfig-connect-drain";
        error = esp32_mquickjs_wifi_smartconfig_connect_end(&s->operation, s->status.connection_generation, complete);
        portENTER_CRITICAL(&s_sc_session_lock);
        s->status.cleanup_error = error;
        s->status.cleanup_stage = error == ESP_OK ? NULL : stage;
        if (error == ESP_OK) {
            s->connection_released = true;
            s->status.connection_transferred = complete;
            s->finish_requested = complete;
            s->deadline_us = 0;
        }
        portEXIT_CRITICAL(&s_sc_session_lock);
    }
    portENTER_CRITICAL(&s_sc_session_lock);
    s->status.worker_busy = false;
    /* Let the ACK/close worker run immediately; helper-only polling is bounded. */
    bool native_work = s->finish_requested || (s->status.closing && s->connection_released) ||
        (!s->status.closing && s->status.ack_requested && (!s->ack_submitted || s->status.native.decoder.ack_busy));
    s->next_retry_us = native_work ? 0 : now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_sc_session_lock);
    esp32_mquickjs_wifi_smartconfig_session_release(s);
    return true;
}

bool esp32_mquickjs_wifi_smartconfig_service(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_sc_session_lock);
    esp32_mquickjs_wifi_smartconfig_session_t *s = s_sc_active;
    if (s && !s->status.closing && s->deadline_us && now >= s->deadline_us) sc_session_close_locked(s, true);
    portEXIT_CRITICAL(&s_sc_session_lock);
    bool connection_progress = sc_session_progress_connection(now);
    portENTER_CRITICAL(&s_sc_session_lock);
    s = s_sc_active;
    bool connection_held = s && s->status.connection_generation && !s->connection_released;
    bool work = s && ((s->status.closing && !connection_held) || s->finish_requested ||
        (!s->status.closing && (!s->status.credentials_ready ||
        (s->status.ack_requested && (!s->ack_submitted || s->status.native.decoder.ack_busy)))));
    bool run = work && !s->status.worker_busy && s->references != UINT32_MAX && now >= s->next_retry_us;
    if (run) { ++s->references; ++s_sc_workers; s->status.worker_busy = true; }
    portEXIT_CRITICAL(&s_sc_session_lock);
    if (!run) return connection_progress;
    if (esp32_mquickjs_submit_background_worker(sc_session_worker, s)) return true;
    portENTER_CRITICAL(&s_sc_session_lock);
    s->status.worker_busy = false;
    s->status.cleanup_error = ESP_ERR_NO_MEM; s->status.cleanup_stage = "smartconfig-worker-queue";
    s->next_retry_us = now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_sc_session_lock);
    esp32_mquickjs_wifi_smartconfig_session_release(s);
    portENTER_CRITICAL(&s_sc_session_lock); --s_sc_workers; portEXIT_CRITICAL(&s_sc_session_lock);
    return false;
}

bool esp32_mquickjs_wifi_smartconfig_prepare_runtime_destroy(void)
{
    portENTER_CRITICAL(&s_sc_session_lock);
    s_sc_runtime_closing = true;
    if (s_sc_active) sc_session_close_locked(s_sc_active, false);
    portEXIT_CRITICAL(&s_sc_session_lock);
    (void)esp32_mquickjs_wifi_smartconfig_service();
    portENTER_CRITICAL(&s_sc_session_lock);
    bool drained = !s_sc_active && !s_sc_workers;
    portEXIT_CRITICAL(&s_sc_session_lock);
    return drained;
}
#endif
