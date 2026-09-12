#include "esp32_mquickjs_wifi_wps_session.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

struct esp32_mquickjs_wifi_wps_session {
    uint32_t references, timeout_ms, station_identity;
    uint32_t receive_waiters, close_waiters;
    esp32_mquickjs_wifi_wps_session_status_t status;
    esp32_mquickjs_wifi_radio_lease_t owners[3];
    esp32_mquickjs_wifi_radio_operation_t operation;
    esp_wps_config_t config;
    esp32_mquickjs_wifi_wps_credentials_t credentials;
    uint8_t pin[8];
    int64_t deadline_us, next_retry_us;
    bool worker_started, allow_ap_channel_change;
};
static portMUX_TYPE s_wps_session_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_wps_session_t *s_wps_active;
static unsigned s_wps_handles, s_wps_workers;
static bool s_wps_runtime_closing = true;

static void wps_session_scrub(esp32_mquickjs_wifi_wps_session_t *s)
{
    esp32_mquickjs_wireless_secure_zero(&s->config, sizeof(s->config));
    esp32_mquickjs_wireless_secure_zero(s->pin, sizeof(s->pin));
    esp32_mquickjs_wireless_secure_zero(&s->credentials, sizeof(s->credentials));
    s->status.pin_ready = s->status.credentials_ready = false;
}

static void wps_session_close_locked(esp32_mquickjs_wifi_wps_session_t *s, bool timeout)
{
    if (!s->status.closing && timeout && !s->status.credentials_ready) {
        s->status.timed_out = true;
        if (s->status.error == ESP_OK) { s->status.error = ESP_ERR_TIMEOUT; s->status.stage = "wps-timeout"; }
    }
    s->status.closing = true;
    s->next_retry_us = 0;
    if (!s->status.worker_busy) wps_session_scrub(s);
    if (!s->status.activated) s->status.retired = true;
}

esp_err_t esp32_mquickjs_wifi_wps_open_runtime(void)
{
    portENTER_CRITICAL(&s_wps_session_lock);
    bool ready = !s_wps_active && !s_wps_workers && !s_wps_handles;
    if (ready) s_wps_runtime_closing = false;
    portEXIT_CRITICAL(&s_wps_session_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_wps_session_create(const esp_wps_config_t *config,
    bool allow_ap_channel_change, uint32_t timeout_ms, esp32_mquickjs_wifi_wps_session_t **out)
{
    if (!out || *out || !timeout_ms || timeout_ms > 3600000 || !esp32_mquickjs_wifi_wps_config_valid(config))
        return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_wps_session_lock);
    esp_err_t error = s_wps_runtime_closing ? ESP_ERR_INVALID_STATE :
        s_wps_handles >= ESP32_MQUICKJS_WPS_MAX_HANDLES ? ESP_ERR_NO_MEM : ESP_OK;
    if (error == ESP_OK) ++s_wps_handles;
    portEXIT_CRITICAL(&s_wps_session_lock);
    if (error != ESP_OK) return error;
    esp32_mquickjs_wifi_wps_session_t *s = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*s), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!s) {
        portENTER_CRITICAL(&s_wps_session_lock); --s_wps_handles; portEXIT_CRITICAL(&s_wps_session_lock);
        return ESP_ERR_NO_MEM;
    }
    s->references = 1; s->timeout_ms = timeout_ms; s->config = *config;
    s->allow_ap_channel_change = allow_ap_channel_change;
    s->status.reserved_bytes = sizeof(*s);
    *out = s;
    return ESP_OK;
}

bool esp32_mquickjs_wifi_wps_session_retain(esp32_mquickjs_wifi_wps_session_t *s)
{
    if (!s) return false;
    portENTER_CRITICAL(&s_wps_session_lock);
    bool ok = s->references && s->references != UINT32_MAX;
    if (ok) ++s->references;
    portEXIT_CRITICAL(&s_wps_session_lock);
    return ok;
}

void esp32_mquickjs_wifi_wps_session_release(esp32_mquickjs_wifi_wps_session_t *s)
{
    if (!s) return;
    portENTER_CRITICAL(&s_wps_session_lock);
    bool destroy = --s->references == 0;
    if (s->references == 1 && s_wps_active == s) wps_session_close_locked(s, false);
    portEXIT_CRITICAL(&s_wps_session_lock);
    if (!destroy) return;
    esp32_mquickjs_wireless_secure_zero(s, sizeof(*s));
    esp32_mquickjs_memory_payload_free(s);
    portENTER_CRITICAL(&s_wps_session_lock); --s_wps_handles; portEXIT_CRITICAL(&s_wps_session_lock);
}

esp_err_t esp32_mquickjs_wifi_wps_session_activate(esp32_mquickjs_wifi_wps_session_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time(), duration = (int64_t)s->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_wps_session_lock);
    bool ready = !s_wps_runtime_closing && !s_wps_active && !s->status.activated &&
        !s->status.closing && s->references != UINT32_MAX;
    if (ready) {
        ++s->references; s_wps_active = s;
        s->status.activated = true; s->deadline_us = now + duration;
    }
    portEXIT_CRITICAL(&s_wps_session_lock);
    if (!ready) return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32_mquickjs_wifi_wps_station_reserve(&s->station_identity, s->owners);
    if (error != ESP_OK) {
        portENTER_CRITICAL(&s_wps_session_lock);
        s->status.error = error; s->status.stage = "wps-station-reserve";
        wps_session_close_locked(s, false); s->status.retired = true;
        s_wps_active = NULL;
        portEXIT_CRITICAL(&s_wps_session_lock);
        esp32_mquickjs_wifi_wps_session_release(s); /* Registry, no native admission. */
    }
    return error;
}

void esp32_mquickjs_wifi_wps_session_close(esp32_mquickjs_wifi_wps_session_t *s, bool timeout)
{
    if (!s) return;
    portENTER_CRITICAL(&s_wps_session_lock); wps_session_close_locked(s, timeout); portEXIT_CRITICAL(&s_wps_session_lock);
}

bool esp32_mquickjs_wifi_wps_session_status(esp32_mquickjs_wifi_wps_session_t *s,
    esp32_mquickjs_wifi_wps_session_status_t *status)
{
    if (!s || !status) return false;
    portENTER_CRITICAL(&s_wps_session_lock); *status = s->status; portEXIT_CRITICAL(&s_wps_session_lock);
    return true;
}

void esp32_mquickjs_wifi_wps_global_status(esp32_mquickjs_wifi_wps_global_status_t *status)
{
    if (!status) return;
    portENTER_CRITICAL(&s_wps_session_lock);
    *status = (esp32_mquickjs_wifi_wps_global_status_t){.handles = s_wps_handles, .workers = s_wps_workers,
        .active = s_wps_active != NULL, .runtime_closing = s_wps_runtime_closing};
    if (s_wps_active) status->session = s_wps_active->status;
    portEXIT_CRITICAL(&s_wps_session_lock);
}

static esp_err_t wps_session_copy(esp32_mquickjs_wifi_wps_session_t *s, bool credentials, void *out, bool commit)
{
    if (!s || (!commit && !out) || (commit && out)) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_wps_session_lock);
    bool *ready = credentials ? &s->status.credentials_ready : &s->status.pin_ready;
    bool *consumed = credentials ? &s->status.credentials_consumed : &s->status.pin_consumed;
    esp_err_t error = s->status.closing ? ESP_ERR_INVALID_STATE :
        s->status.worker_busy || !*ready || *consumed ? ESP_ERR_NOT_FINISHED : ESP_OK;
    if (error == ESP_OK) {
        void *source = credentials ? (void *)&s->credentials : (void *)s->pin;
        size_t size = credentials ? sizeof(s->credentials) : sizeof(s->pin);
        if (commit) { esp32_mquickjs_wireless_secure_zero(source, size); *consumed = true; *ready = false; }
        else memcpy(out, source, size);
    }
    portEXIT_CRITICAL(&s_wps_session_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_wps_session_pin(esp32_mquickjs_wifi_wps_session_t *s, uint8_t pin[8], bool commit)
{
    return wps_session_copy(s, false, pin, commit);
}

esp_err_t esp32_mquickjs_wifi_wps_session_credentials(esp32_mquickjs_wifi_wps_session_t *s,
    esp32_mquickjs_wifi_wps_credentials_t *credentials, bool commit)
{
    return wps_session_copy(s, true, credentials, commit);
}

bool esp32_mquickjs_wifi_wps_session_wait_begin(esp32_mquickjs_wifi_wps_session_t *s, bool close)
{
    portENTER_CRITICAL(&s_wps_session_lock);
    uint32_t *count = close ? &s->close_waiters : &s->receive_waiters;
    bool ready = *count != UINT32_MAX;
    if (ready) ++*count;
    portEXIT_CRITICAL(&s_wps_session_lock);
    return ready;
}

void esp32_mquickjs_wifi_wps_session_wait_end(esp32_mquickjs_wifi_wps_session_t *s, bool close)
{
    portENTER_CRITICAL(&s_wps_session_lock);
    uint32_t *count = close ? &s->close_waiters : &s->receive_waiters;
    if (*count) --*count;
    portEXIT_CRITICAL(&s_wps_session_lock);
}

bool esp32_mquickjs_wifi_wps_session_observation(esp32_mquickjs_wifi_wps_session_t *s,
    esp32_mquickjs_wifi_wps_session_status_t *status)
{
    portENTER_CRITICAL(&s_wps_session_lock);
    *status = s->status;
    bool result = status->error || status->closing || status->pin_ready || status->credentials_ready || status->credentials_consumed;
    bool ready = !(s->receive_waiters && result) && !(s->close_waiters && status->retired);
    portEXIT_CRITICAL(&s_wps_session_lock);
    return ready;
}

static bool wps_session_closing(esp32_mquickjs_wifi_wps_session_t *s)
{
    portENTER_CRITICAL(&s_wps_session_lock); bool closing = s->status.closing; portEXIT_CRITICAL(&s_wps_session_lock);
    return closing;
}

static void wps_session_fail(esp32_mquickjs_wifi_wps_session_t *s, esp_err_t error, const char *stage)
{
    portENTER_CRITICAL(&s_wps_session_lock);
    if (!s->status.closing && s->status.error == ESP_OK) { s->status.error = error; s->status.stage = stage; }
    wps_session_close_locked(s, false);
    portEXIT_CRITICAL(&s_wps_session_lock);
}

static void wps_session_worker(void *opaque)
{
    esp32_mquickjs_wifi_wps_session_t *s = opaque;
    portENTER_CRITICAL(&s_wps_session_lock);
    bool first = !s->worker_started && !s->status.closing;
    s->worker_started = true;
    esp32_mquickjs_wifi_radio_operation_t token = s->operation;
    esp32_mquickjs_wifi_wps_radio_status_t native = s->status.native;
    bool capture = s->status.capture_finished, native_closed = s->status.native_closed;
    bool helper_drained = s->status.helper_drained;
    bool pin = s->status.pin_ready || s->status.pin_consumed;
    bool credentials = s->status.credentials_ready || s->status.credentials_consumed;
    portEXIT_CRITICAL(&s_wps_session_lock);
    esp_err_t error = ESP_OK;
    const char *stage = NULL;
    if (first) {
        error = esp32_mquickjs_wifi_radio_wps_begin(s->owners, s->allow_ap_channel_change, &s->config, &token, &native);
        esp32_mquickjs_wireless_secure_zero(&s->config, sizeof(s->config));
        if (error != ESP_OK) wps_session_fail(s, error, native.stage);
    }
    if (token.identity && !wps_session_closing(s)) {
        error = esp32_mquickjs_wifi_radio_wps_status(&token, &native);
        if (error == ESP_OK) error = native.error;
        if (error != ESP_OK) wps_session_fail(s, error, native.stage ? native.stage : "wps-native-status");
        if (!wps_session_closing(s) && native.worker.native.pin_available && !pin) {
            stage = "wps-pin-transfer";
            error = esp32_mquickjs_wifi_radio_wps_pin(&token, s->pin, false);
            if (error == ESP_OK) error = esp32_mquickjs_wifi_radio_wps_pin(&token, NULL, true);
            if (error == ESP_OK) {
                portENTER_CRITICAL(&s_wps_session_lock); s->status.pin_ready = true; portEXIT_CRITICAL(&s_wps_session_lock);
            }
        }
        if (!wps_session_closing(s) && native.worker.native.terminal_seen && !capture) {
            stage = "wps-capture-retire";
            error = esp32_mquickjs_wifi_radio_wps_finish_capture(&token, &native);
            if (error == ESP_OK) { capture = true; helper_drained = false; }
        }
        if (!wps_session_closing(s) && capture && helper_drained && !credentials) {
            stage = "wps-credential-transfer";
            error = esp32_mquickjs_wifi_radio_wps_credentials(&token, &s->credentials, false);
            if (error == ESP_OK) error = esp32_mquickjs_wifi_radio_wps_credentials(&token, NULL, true);
            if (error == ESP_OK) {
                portENTER_CRITICAL(&s_wps_session_lock);
                s->status.credentials_ready = true; s->deadline_us = 0;
                portEXIT_CRITICAL(&s_wps_session_lock);
            }
        }
    }
    if (token.identity && wps_session_closing(s)) {
        if (!native_closed) {
            stage = "wps-native-close";
            error = esp32_mquickjs_wifi_radio_wps_prepare_close(&token, &native);
            if (error == ESP_OK) { native_closed = true; helper_drained = false; }
        } else if (helper_drained) {
            stage = "wps-radio-release";
            error = esp32_mquickjs_wifi_radio_wps_close(&token, &native);
        }
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_wps_session_lock);
    s->operation = token; s->status.native = native;
    s->status.capture_finished = capture; s->status.native_closed = native_closed;
    s->status.helper_drained = helper_drained;
    s->status.cleanup_error = error;
    s->status.cleanup_stage = error == ESP_OK ? NULL : native.stage ? native.stage : stage;
    if (s->status.closing) wps_session_scrub(s);
    s->status.worker_busy = false;
    s->next_retry_us = now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_wps_session_lock);
    esp32_mquickjs_wifi_wps_session_release(s); /* Worker. */
    portENTER_CRITICAL(&s_wps_session_lock); --s_wps_workers; portEXIT_CRITICAL(&s_wps_session_lock);
}

/* Runtime-only helper phase, serialized with worker by worker_busy. A public
 * result cannot be published while DHCP/IP cleanup is pending. Final helper
 * release follows final Radio release, keeping admission continuously closed. */
static bool wps_session_progress_helper(int64_t now)
{
    portENTER_CRITICAL(&s_wps_session_lock);
    esp32_mquickjs_wifi_wps_session_t *s = s_wps_active;
    bool release = s && s->status.closing && !s->operation.identity;
    bool run = s && !s->status.worker_busy && now >= s->next_retry_us &&
        s->references != UINT32_MAX && (release ||
        ((s->status.closing ? s->status.native_closed : s->status.capture_finished) && !s->status.helper_drained));
    if (run) { ++s->references; s->status.worker_busy = true; }
    portEXIT_CRITICAL(&s_wps_session_lock);
    if (!run) return false;
    esp_err_t error = release ? esp32_mquickjs_wifi_wps_station_release(&s->station_identity) :
        esp32_mquickjs_wifi_wps_station_drain(s->station_identity);
    portENTER_CRITICAL(&s_wps_session_lock);
    s->status.cleanup_error = error;
    s->status.cleanup_stage = error == ESP_OK ? NULL : "wps-station-drain";
    bool detach = error == ESP_OK && release;
    if (error == ESP_OK) {
        s->status.helper_drained = true;
        if (detach) { s->status.retired = true; s_wps_active = NULL; }
    }
    s->status.worker_busy = false;
    s->next_retry_us = error == ESP_OK ? 0 : now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_wps_session_lock);
    if (detach) esp32_mquickjs_wifi_wps_session_release(s); /* Registry. */
    esp32_mquickjs_wifi_wps_session_release(s); /* Helper phase. */
    return true;
}

bool esp32_mquickjs_wifi_wps_service(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_wps_session_lock);
    esp32_mquickjs_wifi_wps_session_t *s = s_wps_active;
    if (s && !s->status.closing && s->deadline_us && now >= s->deadline_us) wps_session_close_locked(s, true);
    portEXIT_CRITICAL(&s_wps_session_lock);
    bool helper_progress = wps_session_progress_helper(now);
    portENTER_CRITICAL(&s_wps_session_lock);
    s = s_wps_active;
    bool helper_pending = s && (s->status.closing ? s->status.native_closed : s->status.capture_finished) && !s->status.helper_drained;
    bool work = s && !helper_pending && (s->status.closing ? s->operation.identity != 0 :
        (!s->status.credentials_ready && !s->status.credentials_consumed));
    bool run = work && !s->status.worker_busy && s->references != UINT32_MAX && now >= s->next_retry_us;
    if (run) { ++s->references; ++s_wps_workers; s->status.worker_busy = true; }
    portEXIT_CRITICAL(&s_wps_session_lock);
    if (!run) return helper_progress;
    if (esp32_mquickjs_submit_background_worker(wps_session_worker, s)) return true;
    portENTER_CRITICAL(&s_wps_session_lock);
    s->status.worker_busy = false;
    s->status.cleanup_error = ESP_ERR_NO_MEM; s->status.cleanup_stage = "wps-worker-queue";
    s->next_retry_us = now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_wps_session_lock);
    esp32_mquickjs_wifi_wps_session_release(s);
    portENTER_CRITICAL(&s_wps_session_lock); --s_wps_workers; portEXIT_CRITICAL(&s_wps_session_lock);
    return false;
}

bool esp32_mquickjs_wifi_wps_prepare_runtime_destroy(void)
{
    portENTER_CRITICAL(&s_wps_session_lock);
    s_wps_runtime_closing = true;
    if (s_wps_active) wps_session_close_locked(s_wps_active, false);
    portEXIT_CRITICAL(&s_wps_session_lock);
    (void)esp32_mquickjs_wifi_wps_service();
    portENTER_CRITICAL(&s_wps_session_lock);
    bool drained = !s_wps_active && !s_wps_workers;
    portEXIT_CRITICAL(&s_wps_session_lock);
    return drained;
}
#endif
