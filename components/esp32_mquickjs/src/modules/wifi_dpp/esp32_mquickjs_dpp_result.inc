#include "esp32_mquickjs_wifi_dpp_result.h"
#include "esp32_mquickjs_wifi_action_sdk.h"
#include "esp32_mquickjs_wifi_offchan_frame.h"
#include "esp32_mquickjs_wifi_chm_timer.h"
#include "esp32_mquickjs_wifi_wps_sdk.h"
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
#include "esp32_mquickjs_wifi_wps_ap_result.h"
#endif
extern bool current_task_is_wifi_task(void);
static bool esp32qjs_dpp_roc_held(void);
static void esp32qjs_dpp_roc_status_locked(esp32_mquickjs_wifi_dpp_result_status_t *);
static bool esp32qjs_dpp_tx_held(void);
static void esp32qjs_dpp_tx_status_locked(esp32_mquickjs_wifi_dpp_result_status_t *);

/* One preallocated bounded result owner. Capture never allocates and the first
 * terminal result cannot be replaced by a delayed timeout or queue pressure.
 * Rows remain available for explicit selection after delivery is committed. */
struct esp32qjs_dpp_result {
    esp32_mquickjs_wifi_dpp_result_status_t status;
    char uri[ESP32QJS_DPP_URI_MAX + 1U];
    esp_dpp_config_data_t configs[ESP_DPP_MAX_CONFIG_COUNT];
    bool ever_bound;
};
static struct esp32qjs_dpp_result *s_dpp_result;
static uint64_t s_dpp_result_last_identity;
static atomic_bool s_dpp_result_held;
static bool s_dpp_fence_registered, s_dpp_fence_posted;
static bool s_dpp_tx_submitted, s_dpp_roc_submitted;
static bool s_dpp_tx_cancelled, s_dpp_roc_cancelled;
static uint64_t s_dpp_command_identity;
ESP_EVENT_DEFINE_BASE(ESP32QJS_DPP_PRIVATE_EVENT);

bool esp32qjs_dpp_is_action_callback(uintptr_t callback)
{ return callback == (uintptr_t)s_action_rx_cb; }

static bool esp32qjs_dpp_command_allowed_locked(void)
{
    return !s_dpp_result || !s_dpp_result->status.retained ||
        (current_task_is_wifi_task() && s_dpp_command_identity &&
         s_dpp_command_identity == s_dpp_result->status.identity);
}

/* Network Introduction uses an installed Connector after the provisioning
 * authentication object has been destroyed. Timers still carry the async
 * ledger's generation/ticket, never a retained old auth pointer. */
static bool esp32qjs_dpp_connection_locked(void)
{
    return s_dpp_result && s_dpp_result->status.retained && s_dpp_result->status.attached &&
        s_dpp_result->status.connection_mode && s_dpp_result->status.configuration_installed &&
        !s_dpp_result->status.closing && !s_dpp_result->status.terminal;
}

static void esp32qjs_dpp_fence_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != ESP32QJS_DPP_PRIVATE_EVENT || id || !data || dpp_api_lock() != ESP_OK) return;
    uint64_t identity;
    os_memcpy(&identity, data, sizeof(identity));
    if (s_dpp_result && s_dpp_result->status.identity == identity &&
        s_dpp_result->status.closing && s_dpp_result->status.driver_retired && s_dpp_fence_posted)
        s_dpp_result->status.event_fenced = true;
    dpp_api_unlock();
}

bool esp32qjs_dpp_result_held(void)
{ return atomic_load(&s_dpp_result_held); }

static void esp32qjs_dpp_result_count(uint32_t *value)
{ if (*value != UINT32_MAX) ++*value; }

static bool esp32qjs_dpp_result_exact(uint64_t identity)
{ return identity && s_dpp_result && s_dpp_result->status.identity == identity; }

static esp_err_t esp32qjs_dpp_result_create_locked(bool retained)
{
    if (s_dpp_result || s_dpp_async_count || atomic_load(&s_dpp_init_done) ||
        s_dpp_ctx.dpp_global || s_dpp_ctx.dpp_config_store || s_dpp_init_pending || s_dpp_deinit_dispatch_pending ||
        s_dpp_fence_registered || s_dpp_tx_submitted || s_dpp_roc_submitted ||
        esp32qjs_dpp_tx_held() || esp32qjs_dpp_roc_held()) return ESP_ERR_INVALID_STATE;
    if (wps_get_owner() != WPS_OWNER_NONE || esp32qjs_wps_native_held()) return ESP_ERR_INVALID_STATE;
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
    if (esp32qjs_wps_ap_result_held()) return ESP_ERR_INVALID_STATE;
#endif
    if (s_dpp_result_last_identity == UINT64_MAX) return ESP_ERR_NO_MEM;
    struct esp32qjs_dpp_result *entry = os_zalloc(sizeof(*entry));
    if (!entry) return ESP_ERR_NO_MEM;
    entry->status.identity = ++s_dpp_result_last_identity;
    entry->status.retained = retained;
    entry->status.event_id = -1;
    entry->status.reserved_bytes = sizeof(*entry);
    s_dpp_result = entry;
    atomic_store(&s_dpp_result_held, true);
    return ESP_OK;
}

static void esp32qjs_dpp_result_free_locked(void)
{
    bin_clear_free(s_dpp_result, sizeof(*s_dpp_result));
    s_dpp_result = NULL;
    atomic_store(&s_dpp_result_held, false);
}

esp_err_t esp32qjs_dpp_result_reserve(uint64_t *identity)
{
    if (!identity || *identity) return ESP_ERR_INVALID_ARG;
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    error = esp32qjs_dpp_result_create_locked(true);
    if (error == ESP_OK) *identity = s_dpp_result->status.identity;
    dpp_api_unlock(); return error;
}

esp_err_t esp32qjs_dpp_result_discard_unbound(uint64_t identity)
{
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity) || s_dpp_result->ever_bound) error = ESP_ERR_INVALID_STATE;
    else esp32qjs_dpp_result_free_locked();
    dpp_api_unlock(); return error;
}

static esp_err_t esp32qjs_dpp_result_bind_locked(uint64_t identity)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    if (!s_dpp_result) {
        if (identity) return ESP_ERR_INVALID_STATE;
        /* Public SDK init has already claimed init_pending. */
        bool pending = s_dpp_init_pending;
        s_dpp_init_pending = false;
        esp_err_t error = esp32qjs_dpp_result_create_locked(false);
        s_dpp_init_pending = pending;
        if (error != ESP_OK) return error;
    } else if (!esp32qjs_dpp_result_exact(identity)) return ESP_ERR_INVALID_STATE;
    if (s_dpp_result->ever_bound || s_dpp_result->status.closing) return ESP_ERR_INVALID_STATE;
    s_dpp_result->ever_bound = true;
    s_dpp_result->status.attached = true;
    return ESP_OK;
}

static void esp32qjs_dpp_result_failure_locked(esp_err_t error)
{
    if (!s_dpp_result || !s_dpp_result->status.attached || s_dpp_result->status.closing) return;
    if (s_dpp_result->status.terminal) {
        esp32qjs_dpp_result_count(&s_dpp_result->status.duplicate_results); return;
    }
    s_dpp_result->status.error = error;
    s_dpp_result->status.event_id = WIFI_EVENT_DPP_FAILED;
    s_dpp_result->status.terminal = true;
}

static esp_err_t esp32qjs_dpp_result_uri_locked(const char *uri, size_t length)
{
    if (!s_dpp_result || !s_dpp_result->status.attached || s_dpp_result->status.closing ||
        s_dpp_result->status.terminal) return ESP_ERR_INVALID_STATE;
    if (!uri || !length || length > ESP32QJS_DPP_URI_MAX || uri[length] || memchr(uri, 0, length))
        return ESP_ERR_INVALID_SIZE;
    if (s_dpp_result->status.uri_length && s_dpp_result->status.retained) return ESP_ERR_INVALID_STATE;
    os_memcpy(s_dpp_result->uri, uri, length + 1U);
    s_dpp_result->status.uri_length = length;
    s_dpp_result->status.uri_available = true;
    return ESP_OK;
}

static esp_err_t esp32qjs_dpp_result_configs_locked(const wifi_event_dpp_config_received_t *event)
{
    if (!s_dpp_result || !s_dpp_result->status.attached || s_dpp_result->status.closing)
        return ESP_ERR_INVALID_STATE;
    if (s_dpp_result->status.terminal) {
        esp32qjs_dpp_result_count(&s_dpp_result->status.duplicate_results);
        return ESP_ERR_INVALID_STATE;
    }
    if (!event || !event->total_conf || event->total_conf > ESP_DPP_MAX_CONFIG_COUNT)
        return ESP_ERR_INVALID_SIZE;
    for (unsigned i = 0; i < event->total_conf; ++i) {
        const esp_dpp_config_data_t *row = &event->configs[i];
        if (!row->ssid_len || row->ssid_len > sizeof(row->ssid) || row->password_len > sizeof(row->password) ||
            row->akm < ESP_DPP_AKM_DPP || row->akm > ESP_DPP_AKM_PSK_SAE_DPP ||
            (dpp_akm_dpp((enum dpp_akm)row->akm) && !esp32qjs_dpp_config_valid(row)))
            return ESP_ERR_INVALID_ARG;
    }
    /* SDK converter clears each row first, including padding and unused tails. */
    os_memcpy(s_dpp_result->configs, event->configs, event->total_conf * sizeof(event->configs[0]));
    s_dpp_result->status.config_count = event->total_conf;
    s_dpp_result->status.configs_available = true;
    s_dpp_result->status.event_id = WIFI_EVENT_DPP_CFG_RECVD;
    s_dpp_result->status.terminal = true;
    return ESP_OK;
}

static void esp32qjs_dpp_result_detach_locked(void)
{
    if (!s_dpp_result) return;
    s_dpp_result->status.attached = false;
    s_dpp_result->status.sdk_retired = true;
    /* Managed results remain until the later native driver/event fence and
     * explicit release. SDK heap retirement alone cannot allow lane reuse. */
    if (!s_dpp_result->status.retained) esp32qjs_dpp_result_free_locked();
}

esp_err_t esp32qjs_dpp_result_status(uint64_t identity, esp32_mquickjs_wifi_dpp_result_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity)) error = ESP_ERR_INVALID_STATE;
    else {
        *out = s_dpp_result->status;
        out->async_pending = s_dpp_async_count; out->async_active = s_dpp_async_active;
        esp32qjs_dpp_roc_status_locked(out);
        esp32qjs_dpp_tx_status_locked(out);
        esp32qjs_wifi_chm_timer_status_t chm;
        esp32qjs_wifi_chm_timer_status(&chm);
        out->chm_error = chm.error; out->chm_cleanup_error = chm.cleanup_error;
        out->chm_post_failures = chm.post_failures; out->chm_ignored_messages = chm.ignored_messages;
        out->chm_timers_held = chm.timers_held; out->chm_active_arms = chm.active_arms;
        out->listening = s_dpp_ctx.dpp_listen_ongoing;
    }
    dpp_api_unlock(); return error;
}

esp_err_t esp32qjs_dpp_result_uri_copy(uint64_t identity, char *out, size_t capacity)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity) || !s_dpp_result->status.uri_available) error = ESP_ERR_INVALID_STATE;
    else if (capacity <= s_dpp_result->status.uri_length) error = ESP_ERR_INVALID_SIZE;
    else os_memcpy(out, s_dpp_result->uri, s_dpp_result->status.uri_length + 1U);
    dpp_api_unlock(); return error;
}

esp_err_t esp32qjs_dpp_result_uri_commit(uint64_t identity)
{
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity) || !s_dpp_result->status.uri_available) error = ESP_ERR_INVALID_STATE;
    else s_dpp_result->status.uri_available = false;
    dpp_api_unlock(); return error;
}

esp_err_t esp32qjs_dpp_result_config_copy(uint64_t identity, unsigned index, esp_dpp_config_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity) || index >= s_dpp_result->status.config_count) error = ESP_ERR_INVALID_STATE;
    else *out = s_dpp_result->configs[index];
    dpp_api_unlock(); return error;
}

esp_err_t esp32qjs_dpp_result_configs_commit(uint64_t identity)
{
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity) || !s_dpp_result->status.configs_available) error = ESP_ERR_INVALID_STATE;
    else s_dpp_result->status.configs_available = false;
    dpp_api_unlock(); return error;
}

esp_err_t esp32qjs_dpp_result_release(uint64_t identity)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity) || !s_dpp_result->status.sdk_retired ||
        !s_dpp_result->status.driver_retired || !s_dpp_result->status.event_fenced ||
        s_dpp_async_count || s_dpp_async_active || esp32qjs_dpp_tx_held() ||
        s_dpp_fence_registered || s_dpp_tx_submitted || s_dpp_roc_submitted ||
        esp32qjs_dpp_roc_held() || atomic_load(&s_dpp_init_done)) error = ESP_ERR_INVALID_STATE;
    else esp32qjs_dpp_result_free_locked();
    dpp_api_unlock(); return error;
}
