#include "esp32_mquickjs_wifi_nan_session.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp32_mquickjs_wifi_nan_discovery.h"
#include "esp32_mquickjs_wifi_nan_message.h"
#include "esp32_mquickjs_wifi_nan_path.h"
#include "esp32_mquickjs_wifi_nan_pairing.h"
#include "esp32_mquickjs_wifi_nan_usd_sdk.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

struct esp32_mquickjs_wifi_nan_session {
    uint32_t references, observer_identity, timeout_ms;
    int64_t deadline_us, next_retry_us;
    wifi_nan_sync_config_t config;
    esp32_mquickjs_wifi_radio_operation_t operation;
    esp32_mquickjs_wifi_nan_session_status_t status;
    bool worker_started;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    esp32_mquickjs_wifi_nan_pairing_t *pairing;
    struct {
        esp32_mquickjs_wifi_nan_discovery_t *service;
        uint8_t peer_service_id, peer[6];
        int64_t deadline_us;
    } pairing_request;
#endif
    esp32_mquickjs_wifi_nan_discovery_t *services[ESP_WIFI_NAN_MAX_SVC_SUPPORTED], *creating;
    esp32_mquickjs_wifi_nan_message_t *message;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    esp32_mquickjs_wifi_nan_path_t *paths[ESP_WIFI_NAN_DATAPATH_MAX_PEERS];
#endif
};
static portMUX_TYPE s_nan_session_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_nan_session_t *s_nan_active;
static uint32_t s_nan_next_identity = 1, s_nan_handles, s_nan_workers;
static bool s_nan_runtime_closing = true;
static uint32_t s_nan_discovery_handles, s_nan_discovery_next_identity = 1;
static uint32_t s_nan_message_handles, s_nan_message_next_identity = 1;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
static uint32_t s_nan_path_handles, s_nan_path_next_identity = 1;
static void nan_path_worker(esp32_mquickjs_wifi_nan_session_t *s, bool stopped);
static bool nan_path_pending_locked(esp32_mquickjs_wifi_nan_session_t *s);
#else
#define s_nan_path_handles 0U
static inline void nan_path_worker(esp32_mquickjs_wifi_nan_session_t *s, bool stopped) { (void)s; (void)stopped; }
static inline bool nan_path_pending_locked(esp32_mquickjs_wifi_nan_session_t *s) { (void)s; return false; }
#endif
#if CONFIG_ESP_WIFI_NAN_PAIRING
static uint32_t s_nan_pairing_handles, s_nan_pairing_next_identity = 1;
static void nan_pairing_worker(esp32_mquickjs_wifi_nan_session_t *s, bool stopped);
static bool nan_pairing_service_busy_locked(esp32_mquickjs_wifi_nan_session_t *s, esp32_mquickjs_wifi_nan_discovery_t *d);
static void nan_pairing_notice(esp32_mquickjs_wifi_nan_session_t *s, const esp32_mquickjs_wifi_nan_sdk_notice_t *notice);
static bool nan_pairing_pending_locked(esp32_mquickjs_wifi_nan_session_t *s) { return s->pairing || s->pairing_request.service; }
#else
#define s_nan_pairing_handles 0U
static inline void nan_pairing_worker(esp32_mquickjs_wifi_nan_session_t *s, bool stopped) { (void)s; (void)stopped; }
static inline bool nan_pairing_pending_locked(esp32_mquickjs_wifi_nan_session_t *s) { (void)s; return false; }
#endif
static void nan_message_worker(esp32_mquickjs_wifi_nan_session_t *s, bool stopped);
static bool nan_discovery_pending_locked(esp32_mquickjs_wifi_nan_session_t *s, int64_t now);
static void nan_discovery_worker(esp32_mquickjs_wifi_nan_session_t *s);
static void nan_discovery_notice(esp32_mquickjs_wifi_nan_session_t *s,
    const esp32_mquickjs_wifi_nan_sdk_notice_t *notice);
static void nan_discovery_retire_all(esp32_mquickjs_wifi_nan_session_t *s);

static void nan_session_close_locked(esp32_mquickjs_wifi_nan_session_t *s, bool timeout)
{
    if (!s->status.closing && timeout) {
        s->status.timed_out = true;
        if (s->status.error == ESP_OK) { s->status.error = ESP_ERR_TIMEOUT; s->status.stage = "nan-timeout"; }
    }
    s->status.closing = true;
    s->status.ready = false;
    s->next_retry_us = 0;
    if (!s->status.activated) s->status.retired = true;
}

esp_err_t esp32_mquickjs_wifi_nan_open_runtime(void)
{
    portENTER_CRITICAL(&s_nan_session_lock);
    bool ready = !s_nan_active && !s_nan_handles && !s_nan_workers && !s_nan_discovery_handles && !s_nan_message_handles && !s_nan_path_handles && !s_nan_pairing_handles;
    if (ready) s_nan_runtime_closing = false;
    portEXIT_CRITICAL(&s_nan_session_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_nan_global_status(esp32_mquickjs_wifi_nan_global_status_t *status)
{
    if (!status) return;
    portENTER_CRITICAL(&s_nan_session_lock);
    *status = (esp32_mquickjs_wifi_nan_global_status_t){
        .active = s_nan_active != NULL, .handles = s_nan_handles,
        .workers = s_nan_workers, .runtime_closing = s_nan_runtime_closing,
        .service_handles = s_nan_discovery_handles,
        .message_handles = s_nan_message_handles,
        .path_handles = s_nan_path_handles,
        .pairing_handles = s_nan_pairing_handles,
#if CONFIG_ESP_WIFI_NAN_PAIRING
        .active_pairings = s_nan_active && s_nan_active->pairing ? 1 : 0,
#endif
    };
    if (s_nan_active) {
        status->session = s_nan_active->status;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
        for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i)
            if (s_nan_active->paths[i]) ++status->active_paths;
#endif
        for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i)
            if (s_nan_active->services[i]) ++status->active_services;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
}

static esp_err_t nan_session_create(const wifi_nan_sync_config_t *config, bool usd,
    uint32_t timeout_ms, esp32_mquickjs_wifi_nan_session_t **out)
{
    if (!config || !out || *out || !timeout_ms || timeout_ms > ESP32_MQUICKJS_NAN_MAX_STARTUP_MS || !config->op_channel)
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (!usd) return ESP_ERR_NOT_SUPPORTED;
#endif
    if (config->reset_current_nvs_creds || config->use_nvs_for_caching)
        return ESP_ERR_NOT_SUPPORTED;
#if !CONFIG_ESP_WIFI_NAN_SECURITY
    if (config->group_mgmt_prot) return ESP_ERR_NOT_SUPPORTED;
#endif
    portENTER_CRITICAL(&s_nan_session_lock);
    esp_err_t error = s_nan_runtime_closing ? ESP_ERR_INVALID_STATE :
        s_nan_handles >= ESP32_MQUICKJS_NAN_MAX_HANDLES || !s_nan_next_identity ? ESP_ERR_NO_MEM : ESP_OK;
    uint32_t identity = 0;
    if (error == ESP_OK) { ++s_nan_handles; identity = s_nan_next_identity++; }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (error != ESP_OK) return error;
    esp32_mquickjs_wifi_nan_session_t *s = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*s),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!s) {
        portENTER_CRITICAL(&s_nan_session_lock); --s_nan_handles; portEXIT_CRITICAL(&s_nan_session_lock);
        return ESP_ERR_NO_MEM;
    }
    s->references = 1;
    s->config = *config;
    s->timeout_ms = timeout_ms;
    s->status.identity = identity;
    s->status.usd = usd;
    s->status.group_management_protection = config->group_mgmt_prot;
    s->status.reserved_bytes = sizeof(*s);
    *out = s;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_nan_session_create(const wifi_nan_sync_config_t *config,
    uint32_t timeout_ms, esp32_mquickjs_wifi_nan_session_t **out)
{ return nan_session_create(config, false, timeout_ms, out); }

esp_err_t esp32_mquickjs_wifi_nan_session_create_usd(uint32_t timeout_ms,
    esp32_mquickjs_wifi_nan_session_t **out)
{
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    wifi_nan_sync_config_t config = {.op_channel = 6};
    return nan_session_create(&config, true, timeout_ms, out);
#else
    (void)timeout_ms; (void)out;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool esp32_mquickjs_wifi_nan_session_retain(esp32_mquickjs_wifi_nan_session_t *s)
{
    if (!s) return false;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool retained = s->references && s->references != UINT32_MAX;
    if (retained) ++s->references;
    portEXIT_CRITICAL(&s_nan_session_lock);
    return retained;
}

void esp32_mquickjs_wifi_nan_session_release(esp32_mquickjs_wifi_nan_session_t *s)
{
    if (!s) return;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool destroy = --s->references == 0;
    if (s->references == 1 && s_nan_active == s) nan_session_close_locked(s, false);
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (!destroy) return;
    esp32_mquickjs_wireless_secure_zero(s, sizeof(*s));
    esp32_mquickjs_memory_payload_free(s);
    portENTER_CRITICAL(&s_nan_session_lock); --s_nan_handles; portEXIT_CRITICAL(&s_nan_session_lock);
}

esp_err_t esp32_mquickjs_wifi_nan_session_activate(esp32_mquickjs_wifi_nan_session_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time(), duration = (int64_t)s->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool ready = !s_nan_runtime_closing && !s_nan_active && !s->status.activated &&
        !s->status.closing && s->references && s->references != UINT32_MAX;
    if (ready) {
        ++s->references;
        s_nan_active = s;
        s->status.activated = true;
        s->deadline_us = now + duration;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_nan_session_close(esp32_mquickjs_wifi_nan_session_t *s, bool timeout)
{
    if (!s) return;
    portENTER_CRITICAL(&s_nan_session_lock); nan_session_close_locked(s, timeout); portEXIT_CRITICAL(&s_nan_session_lock);
}

bool esp32_mquickjs_wifi_nan_session_status(esp32_mquickjs_wifi_nan_session_t *s,
    esp32_mquickjs_wifi_nan_session_status_t *status)
{
    if (!s || !status) return false;
    portENTER_CRITICAL(&s_nan_session_lock); *status = s->status; portEXIT_CRITICAL(&s_nan_session_lock);
    return true;
}

#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
esp_err_t esp32_mquickjs_wifi_nan_session_query(esp32_mquickjs_wifi_nan_session_t *s,
    const esp32_mquickjs_wifi_nan_query_t *query, esp32_mquickjs_wifi_nan_query_result_t *out)
{
    if (!s || !query || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    portENTER_CRITICAL(&s_nan_session_lock);
    bool ready = s_nan_active == s && !s_nan_runtime_closing && s->status.ready &&
        !s->status.closing && !s->status.error && !s->status.retired;
    bool usd = s->status.usd;
    esp32_mquickjs_wifi_radio_operation_t token = s->operation;
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (!ready) return ESP_ERR_INVALID_STATE;
    if (usd) return ESP_ERR_NOT_SUPPORTED;
    /* Native service cancellation and parent STOP serialize on this same Radio
     * mutex. IDs mean current cache entries, never retained Service handles. */
    return esp32_mquickjs_wifi_radio_nan_query(&token, query, out);
}
#endif

static void nan_session_observer(uint32_t identity, const esp32_mquickjs_wifi_nan_sdk_notice_t *notice, void *opaque)
{
    esp32_mquickjs_wifi_nan_session_t *s = opaque;
    portENTER_CRITICAL(&s_nan_session_lock);
    if (!s->observer_identity) s->observer_identity = identity;
    bool exact = s->observer_identity == identity;
    if (exact) {
        if (notice->kind == ESP32_MQUICKJS_NAN_SDK_STARTED) {
            s->status.native_start_seen = notice->status == ESP_OK;
            if (notice->status != ESP_OK && !s->status.error) {
                s->status.error = notice->status;
                s->status.stage = "nan-native-start";
                nan_session_close_locked(s, false);
            }
        } else if (notice->kind == ESP32_MQUICKJS_NAN_SDK_STOPPED) {
            s->status.native_stop_seen = true;
            nan_session_close_locked(s, false);
        }
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (exact) {
        nan_discovery_notice(s, notice);
#if CONFIG_ESP_WIFI_NAN_PAIRING
        if (notice->kind == ESP32_MQUICKJS_NAN_SDK_BOOTSTRAP) nan_pairing_notice(s, notice);
#endif
    }
}

static void nan_session_worker(void *opaque)
{
    esp32_mquickjs_wifi_nan_session_t *s = opaque;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool first = !s->worker_started && !s->status.closing;
    s->worker_started = true;
    esp32_mquickjs_wifi_radio_operation_t operation = s->operation;
    esp32_mquickjs_wifi_nan_radio_status_t native = s->status.native;
    portEXIT_CRITICAL(&s_nan_session_lock);
    esp_err_t error = ESP_OK;
    if (first) {
        error = s->status.usd ? esp32_mquickjs_wifi_radio_nan_usd_begin(nan_session_observer, s, &operation, &native) :
            esp32_mquickjs_wifi_radio_nan_begin(&s->config, nan_session_observer, s, &operation, &native);
        esp32_mquickjs_wireless_secure_zero(&s->config, sizeof(s->config));
    } else if (s->status.usd && operation.identity) {
        error = esp32_mquickjs_wifi_radio_nan_poll(&operation, &native);
    }
    int64_t completed_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_nan_session_lock);
    /* The runtime may have been unable to poll while SDK startup was in
     * flight. A late successful START cannot erase an elapsed public deadline. */
    if (!s->status.closing && s->deadline_us && completed_us >= s->deadline_us)
        nan_session_close_locked(s, true);
    if (error != ESP_OK) {
        if (!s->status.error) { s->status.error = error; s->status.stage = native.stage; }
        nan_session_close_locked(s, false);
    }
    bool closing = s->status.closing;
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (!first) {
        nan_message_worker(s, false);
        nan_path_worker(s, false);
        nan_pairing_worker(s, false);
        portENTER_CRITICAL(&s_nan_session_lock);
        closing = s->status.closing;
        portEXIT_CRITICAL(&s_nan_session_lock);
        if (!closing) nan_discovery_worker(s);
        portENTER_CRITICAL(&s_nan_session_lock);
        closing = s->status.closing;
        portEXIT_CRITICAL(&s_nan_session_lock);
    }
    if (closing) error = operation.identity ? esp32_mquickjs_wifi_radio_nan_close(&operation, &native) : ESP_OK;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_nan_session_lock);
    s->operation = operation;
    s->status.native = native;
    s->status.ready = native.ready && !s->status.closing;
    if (s->status.ready) s->deadline_us = 0;
    s->status.cleanup_error = closing ? error : ESP_OK;
    s->status.cleanup_stage = !closing || error == ESP_OK ? NULL : native.cleanup_stage;
    bool detach = s->status.closing && !operation.identity;
    if (detach) { s->status.retired = true; s_nan_active = NULL; }
    s->status.reserved_bytes = sizeof(*s) + (operation.identity ? native.reserved_bytes : 0);
    s->status.worker_busy = false;
    s->next_retry_us = now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (detach) { nan_message_worker(s, true); nan_pairing_worker(s, true); nan_path_worker(s, true); nan_discovery_retire_all(s); }
    if (detach) esp32_mquickjs_wifi_nan_session_release(s); /* Registry: no native references remain. */
    esp32_mquickjs_wifi_nan_session_release(s); /* Worker. */
    portENTER_CRITICAL(&s_nan_session_lock); --s_nan_workers; portEXIT_CRITICAL(&s_nan_session_lock);
}

bool esp32_mquickjs_wifi_nan_service(void)
{
    int64_t now = esp_timer_get_time();
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    esp32_mquickjs_wifi_nan_tx_status_t native;
    esp32_mquickjs_wifi_nan_tx_status(&native);
#endif
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_t *s = s_nan_active;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (s && !s->status.usd && native.error && !s->status.closing) {
        if (!s->status.error) { s->status.error = native.error; s->status.stage = "nan-native"; }
        nan_session_close_locked(s, false);
    }
#endif
    if (s && !s->status.closing && s->deadline_us && now >= s->deadline_us) nan_session_close_locked(s, true);
    bool run = s && !s->status.worker_busy && !s->status.retired &&
        (!s->worker_started || s->status.usd || s->status.closing || s->message || nan_pairing_pending_locked(s) || nan_path_pending_locked(s) || nan_discovery_pending_locked(s, now)) &&
        s->references != UINT32_MAX && now >= s->next_retry_us;
    if (run) { ++s->references; ++s_nan_workers; s->status.worker_busy = true; }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (!run) return false;
    if (esp32_mquickjs_submit_background_worker(nan_session_worker, s)) return true;
    portENTER_CRITICAL(&s_nan_session_lock);
    s->status.worker_busy = false;
    s->status.cleanup_error = ESP_ERR_NO_MEM;
    s->status.cleanup_stage = "nan-worker-queue";
    s->next_retry_us = now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_release(s);
    portENTER_CRITICAL(&s_nan_session_lock); --s_nan_workers; portEXIT_CRITICAL(&s_nan_session_lock);
    return false;
}

bool esp32_mquickjs_wifi_nan_prepare_runtime_destroy(void)
{
    portENTER_CRITICAL(&s_nan_session_lock);
    s_nan_runtime_closing = true;
    if (s_nan_active) nan_session_close_locked(s_nan_active, false);
    portEXIT_CRITICAL(&s_nan_session_lock);
    (void)esp32_mquickjs_wifi_nan_service();
    portENTER_CRITICAL(&s_nan_session_lock);
    bool drained = !s_nan_active && !s_nan_workers;
    portEXIT_CRITICAL(&s_nan_session_lock);
    return drained;
}
#include "esp32_mquickjs_wifi_nan_discovery.inc"
#if CONFIG_ESP_WIFI_NAN_PAIRING
#include "esp32_mquickjs_wifi_nan_pairing.inc"
#endif
#include "esp32_mquickjs_wifi_nan_message.inc"
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
#include "esp32_mquickjs_wifi_nan_path.inc"
#endif
#endif
