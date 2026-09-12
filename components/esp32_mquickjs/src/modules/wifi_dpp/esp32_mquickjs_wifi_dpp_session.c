#include "esp32_mquickjs_wifi_dpp_session.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp32_mquickjs_wifi.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

struct esp32_mquickjs_wifi_dpp_session {
    uint32_t references, timeout_ms, station_identity;
    uint32_t receive_waiters, close_waiters;
    esp32_mquickjs_wifi_dpp_session_status_t status;
    esp32_mquickjs_wifi_radio_lease_t owners[3];
    esp32_mquickjs_wifi_radio_operation_t operation;
    esp32_mquickjs_wifi_dpp_worker_options_t options;
    wifi_config_t connection_config;
    esp32_mquickjs_wifi_link_snapshot_t connection_link;
    uint32_t connection_generation;
    int64_t connection_started_us, connection_completed_us;
    esp_dpp_config_data_t configs[ESP_DPP_MAX_CONFIG_COUNT];
    char uri[ESP32QJS_DPP_URI_MAX + 1U];
    int64_t deadline_us, next_retry_us;
    bool worker_started, allow_ap_channel_change, allow_ap_restart;
    bool restore_recovery_requested;
};
static portMUX_TYPE s_dpp_session_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_dpp_session_t *s_dpp_active;
static unsigned s_dpp_handles, s_dpp_workers;
static bool s_dpp_runtime_closing = true;

static void dpp_session_scrub(esp32_mquickjs_wifi_dpp_session_t *s)
{
    esp32_mquickjs_wireless_secure_zero(&s->options, sizeof(s->options));
    esp32_mquickjs_wireless_secure_zero(s->uri, sizeof(s->uri));
    esp32_mquickjs_wireless_secure_zero(&s->configs, sizeof(s->configs));
    esp32_mquickjs_wireless_secure_zero(&s->connection_config, sizeof(s->connection_config));
    s->status.uri_ready = s->status.configs_ready = false;
}

static void dpp_session_close_locked(esp32_mquickjs_wifi_dpp_session_t *s, bool timeout)
{
    if (!s->status.closing && timeout) {
        s->status.timed_out = true;
        if (s->status.error == ESP_OK) { s->status.error = ESP_ERR_TIMEOUT; s->status.stage = "dpp-timeout"; }
    }
    s->status.closing = true;
    s->status.connected = false;
    s->next_retry_us = 0;
    if (!s->status.worker_busy) dpp_session_scrub(s);
    if (!s->status.activated) s->status.retired = true;
}

esp_err_t esp32_mquickjs_wifi_dpp_open_runtime(void)
{
    portENTER_CRITICAL(&s_dpp_session_lock);
    bool ready = !s_dpp_active && !s_dpp_workers && !s_dpp_handles;
    if (ready) s_dpp_runtime_closing = false;
    portEXIT_CRITICAL(&s_dpp_session_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_dpp_session_create(const esp32_mquickjs_wifi_dpp_worker_options_t *options,
    bool allow_ap_channel_change, uint32_t timeout_ms, esp32_mquickjs_wifi_dpp_session_t **out)
{
    if (!out || *out || !timeout_ms || timeout_ms > 3600000)
        return ESP_ERR_INVALID_ARG;
    esp_err_t error = esp32_mquickjs_wifi_dpp_worker_validate(options);
    if (error != ESP_OK) return error;
    portENTER_CRITICAL(&s_dpp_session_lock);
    error = s_dpp_runtime_closing ? ESP_ERR_INVALID_STATE :
        s_dpp_handles >= ESP32_MQUICKJS_DPP_MAX_HANDLES ? ESP_ERR_NO_MEM : ESP_OK;
    if (error == ESP_OK) ++s_dpp_handles;
    portEXIT_CRITICAL(&s_dpp_session_lock);
    if (error != ESP_OK) return error;
    esp32_mquickjs_wifi_dpp_session_t *s = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*s), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!s) {
        portENTER_CRITICAL(&s_dpp_session_lock); --s_dpp_handles; portEXIT_CRITICAL(&s_dpp_session_lock);
        return ESP_ERR_NO_MEM;
    }
    s->references = 1; s->timeout_ms = timeout_ms; s->options = *options;
    s->allow_ap_channel_change = allow_ap_channel_change;
    s->status.reserved_bytes = sizeof(*s);
    *out = s;
    return ESP_OK;
}

bool esp32_mquickjs_wifi_dpp_session_retain(esp32_mquickjs_wifi_dpp_session_t *s)
{
    if (!s) return false;
    portENTER_CRITICAL(&s_dpp_session_lock);
    bool ok = s->references && s->references != UINT32_MAX;
    if (ok) ++s->references;
    portEXIT_CRITICAL(&s_dpp_session_lock);
    return ok;
}

void esp32_mquickjs_wifi_dpp_session_release(esp32_mquickjs_wifi_dpp_session_t *s)
{
    if (!s) return;
    portENTER_CRITICAL(&s_dpp_session_lock);
    bool destroy = --s->references == 0;
    if (s->references == 1 && s_dpp_active == s) dpp_session_close_locked(s, false);
    portEXIT_CRITICAL(&s_dpp_session_lock);
    if (!destroy) return;
    esp32_mquickjs_wireless_secure_zero(s, sizeof(*s));
    esp32_mquickjs_memory_payload_free(s);
    portENTER_CRITICAL(&s_dpp_session_lock); --s_dpp_handles; portEXIT_CRITICAL(&s_dpp_session_lock);
}

esp_err_t esp32_mquickjs_wifi_dpp_session_activate(esp32_mquickjs_wifi_dpp_session_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time(), duration = (int64_t)s->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_dpp_session_lock);
    bool ready = !s_dpp_runtime_closing && !s_dpp_active && !s->status.activated &&
        !s->status.closing && s->references != UINT32_MAX;
    if (ready) {
        ++s->references; s_dpp_active = s;
        s->status.activated = true; s->deadline_us = now + duration;
    }
    portEXIT_CRITICAL(&s_dpp_session_lock);
    if (!ready) return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32_mquickjs_wifi_dpp_station_reserve(&s->station_identity, s->owners);
    if (error != ESP_OK) {
        portENTER_CRITICAL(&s_dpp_session_lock);
        s->status.error = error; s->status.stage = "dpp-station-reserve";
        dpp_session_close_locked(s, false); s->status.retired = true;
        s_dpp_active = NULL;
        portEXIT_CRITICAL(&s_dpp_session_lock);
        esp32_mquickjs_wifi_dpp_session_release(s); /* Registry, no native admission. */
    }
    return error;
}

void esp32_mquickjs_wifi_dpp_session_close(esp32_mquickjs_wifi_dpp_session_t *s, bool timeout)
{
    if (!s) return;
    portENTER_CRITICAL(&s_dpp_session_lock); dpp_session_close_locked(s, timeout); portEXIT_CRITICAL(&s_dpp_session_lock);
}

esp_err_t esp32_mquickjs_wifi_dpp_session_recover(esp32_mquickjs_wifi_dpp_session_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_dpp_session_lock);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (s_dpp_active == s && !s_dpp_runtime_closing && s->status.closing &&
        !s->status.retired && !s->status.native_closed && !s->connection_generation && s->operation.identity) {
        if (s->status.recovery_pending) error = ESP_OK;
        else if (s->status.native.restore_start_failed && !s->status.native.worker.handoff_unknown) {
            s->restore_recovery_requested = s->status.recovery_pending = true;
            s->next_retry_us = 0;
            error = ESP_OK;
        }
    }
    portEXIT_CRITICAL(&s_dpp_session_lock);
    return error;
}

bool esp32_mquickjs_wifi_dpp_session_status(esp32_mquickjs_wifi_dpp_session_t *s,
    esp32_mquickjs_wifi_dpp_session_status_t *status)
{
    if (!s || !status) return false;
    portENTER_CRITICAL(&s_dpp_session_lock); *status = s->status; portEXIT_CRITICAL(&s_dpp_session_lock);
    return true;
}

void esp32_mquickjs_wifi_dpp_global_status(esp32_mquickjs_wifi_dpp_global_status_t *status)
{
    if (!status) return;
    portENTER_CRITICAL(&s_dpp_session_lock);
    *status = (esp32_mquickjs_wifi_dpp_global_status_t){.handles = s_dpp_handles, .workers = s_dpp_workers,
        .active = s_dpp_active != NULL, .runtime_closing = s_dpp_runtime_closing};
    if (s_dpp_active) status->session = s_dpp_active->status;
    portEXIT_CRITICAL(&s_dpp_session_lock);
}

esp_err_t esp32_mquickjs_wifi_dpp_session_uri(esp32_mquickjs_wifi_dpp_session_t *s,
    char *uri, size_t capacity, bool commit)
{
    if (!s || (!commit && (!uri || !capacity)) || (commit && (uri || capacity))) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_dpp_session_lock);
    esp_err_t error = s->status.closing ? ESP_ERR_INVALID_STATE :
        s->status.worker_busy || !s->status.uri_ready || s->status.uri_consumed ? ESP_ERR_NOT_FINISHED : ESP_OK;
    if (error == ESP_OK) {
        if (commit) {
            esp32_mquickjs_wireless_secure_zero(s->uri, sizeof(s->uri));
            s->status.uri_consumed = true; s->status.uri_ready = false;
        } else if (capacity <= s->status.uri_length) error = ESP_ERR_INVALID_SIZE;
        else memcpy(uri, s->uri, s->status.uri_length + 1U);
    }
    portEXIT_CRITICAL(&s_dpp_session_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_session_config(esp32_mquickjs_wifi_dpp_session_t *s,
    unsigned index, esp_dpp_config_data_t *config)
{
    if (!s || !config) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_dpp_session_lock);
    esp_err_t error = s->status.closing ? ESP_ERR_INVALID_STATE :
        s->status.worker_busy || (!s->status.configs_ready && !s->status.configs_consumed) ? ESP_ERR_NOT_FINISHED :
        index >= s->status.config_count ? ESP_ERR_INVALID_ARG : ESP_OK;
    if (error == ESP_OK) *config = s->configs[index];
    portEXIT_CRITICAL(&s_dpp_session_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_session_configs_commit(esp32_mquickjs_wifi_dpp_session_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_dpp_session_lock);
    esp_err_t error = s->status.closing ? ESP_ERR_INVALID_STATE :
        s->status.worker_busy || !s->status.configs_ready ? ESP_ERR_NOT_FINISHED : ESP_OK;
    if (error == ESP_OK) { s->status.configs_ready = false; s->status.configs_consumed = true; }
    portEXIT_CRITICAL(&s_dpp_session_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_session_connect(esp32_mquickjs_wifi_dpp_session_t *s,
    unsigned index, esp32_mquickjs_wifi_dpp_auth_t authentication, uint32_t timeout_ms, bool allow_ap_restart)
{
    if (!s || !timeout_ms || timeout_ms > 3600000) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time(), duration = (int64_t)timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) return ESP_ERR_INVALID_STATE;
    wifi_config_t prepared;
    esp32_mquickjs_wifi_dpp_auth_t selected;
    portENTER_CRITICAL(&s_dpp_session_lock);
    esp_err_t error = s_dpp_runtime_closing || s != s_dpp_active || s->status.closing ||
        (s->status.worker_busy && !s->status.connected) || !s->status.configs_consumed ||
        (s->status.connection_requested && !s->status.connected) ? ESP_ERR_INVALID_STATE :
        index >= s->status.config_count ? ESP_ERR_INVALID_ARG : ESP_OK;
    /* Pure bounded validation under the storage lock; no allocation/driver or
     * JS call. The worker prepares another copy before its first mutation. */
    if (error == ESP_OK) error = esp32_mquickjs_wifi_dpp_connection_prepare(&s->configs[index], authentication, &selected, &prepared);
    if (error == ESP_OK && s->status.connection_requested &&
        (index != s->status.configuration_index || selected != s->status.authentication)) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK && !s->status.connection_requested) {
        s->status.configuration_index = index; s->status.authentication = selected;
        s->status.connection_requested = true;
        s->allow_ap_restart = allow_ap_restart;
        s->connection_started_us = now; s->deadline_us = now + duration; s->next_retry_us = 0;
    }
    portEXIT_CRITICAL(&s_dpp_session_lock);
    esp32_mquickjs_wireless_secure_zero(&prepared, sizeof(prepared));
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_session_connection_result(esp32_mquickjs_wifi_dpp_session_t *s,
    esp32_mquickjs_wifi_link_snapshot_t *link, double *elapsed_ms)
{
    if (!s || !link || !elapsed_ms) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_dpp_session_lock);
    esp_err_t error = s->status.closing || !s->status.connected || !s->status.connection_verified ? ESP_ERR_INVALID_STATE : ESP_OK;
    esp32_mquickjs_wifi_radio_operation_t token = s->operation;
    uint32_t generation = s->connection_generation;
    if (error == ESP_OK) {
        *link = s->connection_link;
        *elapsed_ms = (double)(s->connection_completed_us - s->connection_started_us) / 1000.0;
    }
    portEXIT_CRITICAL(&s_dpp_session_lock);
    if (error != ESP_OK) return error;
    esp32_mquickjs_wifi_dpp_connection_status_t current;
    error = esp32_mquickjs_wifi_dpp_connect_status(&token, generation, &current);
    if (error == ESP_OK && (!current.connected || current.draining || !current.link.valid ||
        memcmp(current.link.bssid, link->bssid, sizeof(link->bssid)) || current.link.channel != link->channel))
        error = ESP_ERR_INVALID_STATE;
    return error;
}

bool esp32_mquickjs_wifi_dpp_session_wait_begin(esp32_mquickjs_wifi_dpp_session_t *s, bool close)
{
    portENTER_CRITICAL(&s_dpp_session_lock);
    uint32_t *count = close ? &s->close_waiters : &s->receive_waiters;
    bool ready = *count != UINT32_MAX;
    if (ready) ++*count;
    portEXIT_CRITICAL(&s_dpp_session_lock);
    return ready;
}

void esp32_mquickjs_wifi_dpp_session_wait_end(esp32_mquickjs_wifi_dpp_session_t *s, bool close)
{
    portENTER_CRITICAL(&s_dpp_session_lock);
    uint32_t *count = close ? &s->close_waiters : &s->receive_waiters;
    if (*count) --*count;
    portEXIT_CRITICAL(&s_dpp_session_lock);
}

bool esp32_mquickjs_wifi_dpp_session_observation(esp32_mquickjs_wifi_dpp_session_t *s,
    esp32_mquickjs_wifi_dpp_session_status_t *status)
{
    portENTER_CRITICAL(&s_dpp_session_lock);
    *status = s->status;
    bool result = status->error || status->closing || status->uri_ready || status->configs_ready || status->configs_consumed;
    bool ready = !(s->receive_waiters && result) && !(s->close_waiters && status->retired);
    portEXIT_CRITICAL(&s_dpp_session_lock);
    return ready;
}

static bool dpp_session_closing(esp32_mquickjs_wifi_dpp_session_t *s)
{
    portENTER_CRITICAL(&s_dpp_session_lock); bool closing = s->status.closing; portEXIT_CRITICAL(&s_dpp_session_lock);
    return closing;
}

static void dpp_session_fail(esp32_mquickjs_wifi_dpp_session_t *s, esp_err_t error, const char *stage)
{
    portENTER_CRITICAL(&s_dpp_session_lock);
    if (!s->status.closing && s->status.error == ESP_OK) { s->status.error = error; s->status.stage = stage; }
    dpp_session_close_locked(s, false);
    portEXIT_CRITICAL(&s_dpp_session_lock);
}

static void dpp_session_worker(void *opaque)
{
    esp32_mquickjs_wifi_dpp_session_t *s = opaque;
    portENTER_CRITICAL(&s_dpp_session_lock);
    bool first = !s->worker_started && !s->status.closing;
    s->worker_started = true;
    esp32_mquickjs_wifi_radio_operation_t token = s->operation;
    esp32_mquickjs_wifi_dpp_radio_status_t native = s->status.native;
    bool capture = s->status.capture_finished, native_closed = s->status.native_closed;
    bool helper_drained = s->status.helper_drained, listening = s->status.listening;
    bool uri = s->status.uri_ready || s->status.uri_consumed;
    bool configs = s->status.configs_ready || s->status.configs_consumed;
    bool connect = s->status.connection_requested, prepared = s->status.connection_prepared;
    bool established = s->status.connection_established, verified = s->status.connection_verified;
    uint32_t connection_generation = s->connection_generation;
    bool recover = s->restore_recovery_requested;
    s->restore_recovery_requested = false;
    portEXIT_CRITICAL(&s_dpp_session_lock);
    esp_err_t error = ESP_OK;
    const char *stage = NULL;
    if (first) {
        error = esp32_mquickjs_wifi_radio_dpp_begin(s->owners, s->allow_ap_channel_change, &s->options, &token, &native);
        esp32_mquickjs_wireless_secure_zero(&s->options, sizeof(s->options));
        if (error != ESP_OK) dpp_session_fail(s, error, native.stage);
    }
    if (token.identity && !dpp_session_closing(s)) {
        error = esp32_mquickjs_wifi_radio_dpp_status(&token, &native);
        if (error == ESP_OK) error = native.error;
        if (error != ESP_OK) dpp_session_fail(s, error, native.stage ? native.stage : "dpp-native-status");
        if (error == ESP_OK && !dpp_session_closing(s) && native.worker.native.uri_available && !uri) {
            stage = "dpp-uri-transfer";
            error = esp32_mquickjs_wifi_radio_dpp_uri(&token, s->uri, sizeof(s->uri), false);
            if (error == ESP_OK) error = esp32_mquickjs_wifi_radio_dpp_uri(&token, NULL, 0, true);
            if (error == ESP_OK) {
                portENTER_CRITICAL(&s_dpp_session_lock);
                s->status.uri_length = native.worker.native.uri_length; s->status.uri_ready = true;
                portEXIT_CRITICAL(&s_dpp_session_lock);
            }
        }
        if (error == ESP_OK && !dpp_session_closing(s) && !connect && !listening && !native.worker.native.terminal) {
            stage = "dpp-native-listen";
            error = esp32_mquickjs_wifi_radio_dpp_listen(&token, &native);
            if (error == ESP_OK) listening = true;
        }
        if (error == ESP_OK && !dpp_session_closing(s) && native.worker.native.terminal && !capture) {
            stage = "dpp-capture-retire";
            error = esp32_mquickjs_wifi_radio_dpp_finish_capture(&token, &native);
            if (error == ESP_OK) { capture = true; helper_drained = false; listening = false; }
        }
        if (error == ESP_OK && !dpp_session_closing(s) && capture && helper_drained && !configs) {
            stage = "dpp-config-transfer";
            unsigned count = native.worker.native.config_count;
            if (!count || count > ESP_DPP_MAX_CONFIG_COUNT) error = ESP_ERR_INVALID_SIZE;
            for (unsigned i = 0; error == ESP_OK && i < count; ++i)
                error = esp32_mquickjs_wifi_radio_dpp_config(&token, i, &s->configs[i]);
            if (error == ESP_OK) error = esp32_mquickjs_wifi_radio_dpp_configs_commit(&token);
            if (error == ESP_OK) {
                portENTER_CRITICAL(&s_dpp_session_lock);
                s->status.config_count = count; s->status.configs_ready = true; s->deadline_us = 0;
                portEXIT_CRITICAL(&s_dpp_session_lock);
            }
        }
        if (error == ESP_OK && !dpp_session_closing(s) && connect && !prepared) {
            stage = "dpp-configuration-select";
            esp32_mquickjs_wifi_dpp_auth_t selected;
            error = esp32_mquickjs_wifi_radio_dpp_select(&token, &s->configs[s->status.configuration_index],
                s->status.authentication, s->allow_ap_restart, &selected, &s->connection_config, &native);
            if (error == ESP_OK) prepared = true;
            else dpp_session_fail(s, error, native.stage ? native.stage : stage);
        }
        if (error == ESP_OK && !dpp_session_closing(s) && established && !verified) {
            stage = "dpp-negotiated-authentication";
            error = esp32_mquickjs_wifi_radio_dpp_check_connection(&token, s->status.authentication, s->connection_link.bssid);
            if (error == ESP_OK) verified = true;
        }
        if (error != ESP_OK && error != ESP_ERR_NOT_FINISHED && error != ESP_ERR_TIMEOUT)
            dpp_session_fail(s, error, native.stage ? native.stage : stage);
    }
    if (token.identity && dpp_session_closing(s) && !connection_generation) {
        if (recover) {
            stage = "dpp-restore-recovery";
            error = esp32_mquickjs_wifi_radio_dpp_recover(&token, &native);
        }
        if (!native_closed && (!recover || error == ESP_OK)) {
            stage = "dpp-native-close";
            error = esp32_mquickjs_wifi_radio_dpp_prepare_close(&token, &native);
            if (error == ESP_OK) { native_closed = true; helper_drained = false; }
        } else if (native_closed && helper_drained) {
            stage = "dpp-radio-release";
            error = esp32_mquickjs_wifi_radio_dpp_close(&token, &native);
        }
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_dpp_session_lock);
    s->operation = token; s->status.native = native; s->status.listening = listening;
    s->status.recovery_pending = s->restore_recovery_requested || native.restore_recovery_pending;
    s->status.capture_finished = capture; s->status.native_closed = native_closed;
    s->status.helper_drained = helper_drained;
    s->status.connection_prepared = prepared; s->status.connection_verified = verified;
    s->status.reserved_bytes = sizeof(*s) + (token.identity ? native.reserved_bytes : 0);
    s->status.cleanup_error = error;
    s->status.cleanup_stage = error == ESP_OK ? NULL : native.stage ? native.stage : stage;
    if (s->status.closing) dpp_session_scrub(s);
    s->status.worker_busy = false;
    s->next_retry_us = now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_dpp_session_lock);
    esp32_mquickjs_wifi_dpp_session_release(s); /* Worker. */
    portENTER_CRITICAL(&s_dpp_session_lock); --s_dpp_workers; portEXIT_CRITICAL(&s_dpp_session_lock);
}

#include "esp32_mquickjs_wifi_dpp_session_connection.inc"

/* Runtime-only helper phase, serialized with worker by worker_busy. A public
 * result cannot be published while DHCP/IP cleanup is pending. Final helper
 * release follows final Radio release, keeping admission continuously closed. */
static bool dpp_session_progress_helper(int64_t now)
{
    portENTER_CRITICAL(&s_dpp_session_lock);
    esp32_mquickjs_wifi_dpp_session_t *s = s_dpp_active;
    bool release = s && s->status.closing && !s->operation.identity;
    bool run = s && !s->status.worker_busy && now >= s->next_retry_us &&
        s->references != UINT32_MAX && (release ||
        ((s->status.closing ? s->status.native_closed : s->status.capture_finished) && !s->status.helper_drained));
    if (run) { ++s->references; s->status.worker_busy = true; }
    portEXIT_CRITICAL(&s_dpp_session_lock);
    if (!run) return false;
    esp_err_t error = release ? esp32_mquickjs_wifi_dpp_station_release(&s->station_identity) :
        esp32_mquickjs_wifi_dpp_station_drain(s->station_identity);
    portENTER_CRITICAL(&s_dpp_session_lock);
    s->status.cleanup_error = error;
    s->status.cleanup_stage = error == ESP_OK ? NULL : "dpp-station-drain";
    bool detach = error == ESP_OK && release;
    if (error == ESP_OK) {
        s->status.helper_drained = true;
        if (detach) { s->status.retired = true; s_dpp_active = NULL; }
    }
    s->status.worker_busy = false;
    s->next_retry_us = error == ESP_OK ? 0 : now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_dpp_session_lock);
    if (detach) esp32_mquickjs_wifi_dpp_session_release(s); /* Registry. */
    esp32_mquickjs_wifi_dpp_session_release(s); /* Helper phase. */
    return true;
}

bool esp32_mquickjs_wifi_dpp_service(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_dpp_session_lock);
    esp32_mquickjs_wifi_dpp_session_t *s = s_dpp_active;
    if (s && !s->status.closing && s->deadline_us && now >= s->deadline_us) dpp_session_close_locked(s, true);
    portEXIT_CRITICAL(&s_dpp_session_lock);
    bool connection_progress = dpp_session_progress_connection(now);
    bool helper_progress = dpp_session_progress_helper(now) || connection_progress;
    portENTER_CRITICAL(&s_dpp_session_lock);
    s = s_dpp_active;
    bool helper_pending = s && (s->status.closing ? s->status.native_closed : s->status.capture_finished) && !s->status.helper_drained;
    bool work = s && !helper_pending && (s->status.closing ? s->operation.identity != 0 && !s->connection_generation :
        ((!s->status.configs_ready && !s->status.configs_consumed) || s->status.connection_requested));
    /* A failed SDK START needs explicit recovery. Repeated close/runtime
     * cleanup must not schedule an unchanging native fault every 100 ms. */
    bool recovery_wait = s && s->status.closing && s->status.native.restore_start_failed && !s->status.recovery_pending;
    bool run = work && !recovery_wait && !s->status.worker_busy && s->references != UINT32_MAX && now >= s->next_retry_us;
    if (run) { ++s->references; ++s_dpp_workers; s->status.worker_busy = true; }
    portEXIT_CRITICAL(&s_dpp_session_lock);
    if (!run) return helper_progress;
    if (esp32_mquickjs_submit_background_worker(dpp_session_worker, s)) return true;
    portENTER_CRITICAL(&s_dpp_session_lock);
    s->status.worker_busy = false;
    s->status.cleanup_error = ESP_ERR_NO_MEM; s->status.cleanup_stage = "dpp-worker-queue";
    s->next_retry_us = now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_dpp_session_lock);
    esp32_mquickjs_wifi_dpp_session_release(s);
    portENTER_CRITICAL(&s_dpp_session_lock); --s_dpp_workers; portEXIT_CRITICAL(&s_dpp_session_lock);
    return false;
}

bool esp32_mquickjs_wifi_dpp_prepare_runtime_destroy(void)
{
    portENTER_CRITICAL(&s_dpp_session_lock);
    s_dpp_runtime_closing = true;
    if (s_dpp_active) dpp_session_close_locked(s_dpp_active, false);
    portEXIT_CRITICAL(&s_dpp_session_lock);
    (void)esp32_mquickjs_wifi_dpp_service();
    portENTER_CRITICAL(&s_dpp_session_lock);
    bool drained = !s_dpp_active && !s_dpp_workers;
    portEXIT_CRITICAL(&s_dpp_session_lock);
    return drained;
}
#endif
