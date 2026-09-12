/* Included inside the reviewed SDK esp_wps.c, after its singleton declarations.
 * Every record access is on the Wi-Fi task. Only the admission hint is atomic.
 * Radio admission and final native retirement belong to the framework worker;
 * it must not reuse this lane merely because the SDK reports owner=NONE. */
#include "esp32_mquickjs_wifi_wps_sdk.h"
#ifdef CONFIG_WPS_REGISTRAR
#include "esp32_mquickjs_wifi_wps_ap_sdk.h"
#endif

extern bool current_task_is_wifi_task(void);
static int wps_check_wifi_mode(void);
static bool esp32qjs_wps_credential_valid(const struct wps_credential *cred);
static int wifi_station_wps_deinit(void);
static void wifi_station_wps_post_m8_timeout(void *data, void *user_ctx);
static bool esp32qjs_wps_scan_quiet(void);
static int esp32qjs_wps_record_error(int error, esp32_mquickjs_wifi_wps_error_stage_t stage);

typedef struct {
    esp32_mquickjs_wifi_wps_native_status_t status;
    uint8_t pin[8];
    esp32_mquickjs_wifi_wps_credentials_t credentials;
    bool start_attempted;
    uint8_t timers_cancelled;
    uint32_t callback_revision;
} esp32qjs_wps_native_t;

static esp32qjs_wps_native_t *s_wps_native;
static uint64_t s_wps_native_last_identity;
static atomic_bool s_wps_native_held;

_Static_assert(MAX_CRED_COUNT <= ESP32_MQUICKJS_WPS_CREDENTIALS_MAX,
    "review WPS credential capacity");
_Static_assert(sizeof(uintptr_t) == 4, "review WPS timer identity encoding");

bool esp32qjs_wps_native_held(void)
{
    return atomic_load(&s_wps_native_held);
}

#ifdef CONFIG_WPS_REGISTRAR
esp_err_t esp32qjs_wps_ap_factory_release(uint32_t identity)
{
    esp32_mquickjs_wifi_wps_ap_result_status_t status;
    if (!current_task_is_wifi_task() || s_wps_native || gWpsSm ||
        wps_get_owner() != WPS_OWNER_NONE ||
        esp32qjs_wps_ap_result_status(identity, &status) != ESP_OK ||
        !status.retained || !status.closing || !status.close_prepared ||
        status.sdk_attached || status.callback_depth || status.tracking_fault)
        return ESP_ERR_INVALID_STATE;
    if (s_factory_info) {
        bin_clear_free(s_factory_info, sizeof(*s_factory_info));
        s_factory_info = NULL;
    }
    return ESP_OK;
}
#endif

static bool esp32qjs_wps_native_exact(uint64_t identity)
{
    return current_task_is_wifi_task() && identity && s_wps_native &&
        s_wps_native->status.identity == identity;
}

static bool esp32qjs_wps_callback_enter(void)
{
#ifdef CONFIG_WPS_REGISTRAR
    /* A late Station callback has no authority over the shared AP WPS SM. */
    if (esp32qjs_wps_ap_result_held()) return false;
#endif
    if (!s_wps_native) return true;
    if (!current_task_is_wifi_task()) return false;
    if (s_wps_native->status.callback_depth == UINT16_MAX ||
        s_wps_native->callback_revision == UINT32_MAX) {
        s_wps_native->status.tracking_fault = true;
        if (s_wps_native->status.error == ESP_OK)
            s_wps_native->status.error = ESP_ERR_INVALID_STATE;
        return false;
    }
    ++s_wps_native->callback_revision;
    ++s_wps_native->status.callback_depth;
    return true;
}

static void esp32qjs_wps_callback_leave(void)
{
    if (!s_wps_native) return;
    if (s_wps_native->status.callback_depth) --s_wps_native->status.callback_depth;
    else {
        s_wps_native->status.tracking_fault = true;
        if (s_wps_native->status.error == ESP_OK)
            s_wps_native->status.error = ESP_ERR_INVALID_STATE;
    }
}

static void *esp32qjs_wps_timer_high(void)
{
    return (void *)(uintptr_t)(s_wps_native ? s_wps_native->status.identity >> 32 : 0);
}

static void *esp32qjs_wps_timer_low(void)
{
    return (void *)(uintptr_t)(s_wps_native ? (uint32_t)s_wps_native->status.identity : 0);
}

static bool esp32qjs_wps_timer_exact(void *high, void *low)
{
    uint64_t identity = ((uint64_t)(uintptr_t)high << 32) | (uint32_t)(uintptr_t)low;
    if (!s_wps_native) return identity == 0;
    return identity == s_wps_native->status.identity &&
        !s_wps_native->status.closing && !s_wps_native->status.terminal_seen;
}

static int esp32qjs_wps_register_timeout(unsigned seconds, unsigned useconds,
    eloop_timeout_handler handler)
{
    int error = eloop_register_timeout(seconds, useconds, handler,
        esp32qjs_wps_timer_high(), esp32qjs_wps_timer_low());
    esp32qjs_wps_record_error(error, ESP32QJS_WPS_STAGE_TIMER);
    /* Do not free the SDK state inside registration: its caller still uses it.
     * start/status observes the sticky error and drives the normal close path. */
    return error;
}

static void esp32qjs_wps_native_scrub(void)
{
    forced_memzero(s_wps_native->pin, sizeof(s_wps_native->pin));
    forced_memzero(&s_wps_native->credentials, sizeof(s_wps_native->credentials));
    s_wps_native->status.pin_available = false;
    s_wps_native->status.credentials_available = false;
}

/* Record the original operation failure without freeing or mutating SDK state
 * under its current stack. Terminal revokes callbacks; worker performs stop.
 * Cleanup errors use cleanup_error and cannot replace this first failure. */
static int esp32qjs_wps_record_error(int error, esp32_mquickjs_wifi_wps_error_stage_t stage)
{
    if (error != ESP_OK && s_wps_native && !s_wps_native->status.closing &&
        !s_wps_native->status.terminal_seen && s_wps_native->status.error == ESP_OK) {
        s_wps_native->status.error = error;
        s_wps_native->status.error_stage = stage;
        s_wps_native->status.terminal_seen = true;
        s_wps_native->status.event_id = WIFI_EVENT_STA_WPS_ER_FAILED;
        esp32qjs_wps_native_scrub();
    }
    return error;
}

static bool esp32qjs_wps_rx_peer(const struct wps_sm *sm, const uint8_t *source)
{
    uint8_t associated[ETH_ALEN];
    if (!sm || !sm->wps || !source || wps_get_status() != WPS_STATUS_PENDING ||
        (sm->state != WAIT_START && sm->state != WPA_MESG) ||
        memcmp(source, sm->bssid, ETH_ALEN) != 0) return false;
    /* A queued packet after disconnect is ignored, not a new failure of an
     * unrelated connection. This is peer/phase validation, not an RF cookie. */
    return esp_wifi_get_assoc_bssid_internal(associated) == ESP_OK &&
        memcmp(associated, source, ETH_ALEN) == 0;
}

/* Never post managed credentials to the default event queue. The framework
 * reads this owner, completes its Future, then publishes metadata observations. */
static bool esp32qjs_wps_native_pin(const void *bytes, size_t size)
{
    if (!s_wps_native) return false;
    if (!s_wps_native->status.closing && !s_wps_native->status.terminal_seen &&
        s_wps_native->status.error == ESP_OK) {
        if (!bytes || size != sizeof(s_wps_native->pin)) {
            s_wps_native->status.error = ESP_ERR_INVALID_SIZE;
            forced_memzero(s_wps_native->pin, sizeof(s_wps_native->pin));
            s_wps_native->status.pin_available = false;
        } else {
            memcpy(s_wps_native->pin, bytes, sizeof(s_wps_native->pin));
            s_wps_native->status.pin_available = true;
        }
    }
    return true;
}

static int esp32qjs_wps_native_stop_sdk(void)
{
    esp32_mquickjs_wifi_wps_native_status_t *status = &s_wps_native->status;
    if (status->inputs_stopped) return ESP_OK;
    if (!status->closing && !status->terminal_seen) return ESP_ERR_INVALID_STATE;
    while (status->cleanup_stage != ESP32QJS_WPS_CLEANUP_DONE) {
        int error = ESP_OK;
        switch (status->cleanup_stage) {
        case ESP32QJS_WPS_CLEANUP_DISCONNECT:
            if (s_wps_native->start_attempted) error = esp_wifi_disconnect();
            break;
        case ESP32QJS_WPS_CLEANUP_TYPE:
            error = wps_set_type(WPS_TYPE_DISABLE);
            break;
        case ESP32QJS_WPS_CLEANUP_START_FLAG:
            error = esp_wifi_set_wps_start_flag_internal(false);
            break;
        case ESP32QJS_WPS_CLEANUP_SCAN:
            if (s_wps_native->start_attempted) error = esp_wifi_scan_stop();
            break;
        case ESP32QJS_WPS_CLEANUP_TIMERS: {
            const eloop_timeout_handler handlers[] = {wifi_station_wps_success,
                wifi_station_wps_timeout, wifi_station_wps_msg_timeout, wifi_wps_scan,
                wifi_station_wps_eapol_start_handle, wifi_station_wps_post_m8_timeout};
            while (s_wps_native->timers_cancelled < sizeof(handlers) / sizeof(handlers[0])) {
                int count = eloop_cancel_timeout(handlers[s_wps_native->timers_cancelled],
                    ELOOP_ALL_CTX, ELOOP_ALL_CTX);
                if (count < 0) { error = count; break; }
                ++s_wps_native->timers_cancelled;
            }
            break;
        }
        case ESP32QJS_WPS_CLEANUP_CONNECTION_TIMER:
            error = esp_wifi_disarm_sta_connection_timer_internal();
            break;
        case ESP32QJS_WPS_CLEANUP_CALLBACKS:
            error = esp_wifi_set_wps_cb_internal(NULL);
            break;
        case ESP32QJS_WPS_CLEANUP_PROBE_IE:
            error = esp_wifi_unset_appie_internal(WIFI_APPIE_WPS_PR);
            break;
        case ESP32QJS_WPS_CLEANUP_ASSOC_IE:
            error = esp_wifi_unset_appie_internal(WIFI_APPIE_WPS_AR);
            break;
        case ESP32QJS_WPS_CLEANUP_STATUS:
            error = wps_set_status(WPS_STATUS_DISABLE);
            break;
        default:
            error = ESP_ERR_INVALID_STATE;
            break;
        }
        status->cleanup_error = error;
        if (error != ESP_OK) return error;
        ++status->cleanup_stage;
    }
    gWpaSm.wpa_sm_wps_disable = NULL;
    wps_set_owner(WPS_OWNER_NONE);
    status->inputs_stopped = true;
    status->enabled = false;
    status->started = false;
    return ESP_OK;
}

static int esp32qjs_wps_native_failure(int32_t event_id, const void *data, size_t size)
{
    if (!s_wps_native || s_wps_native->status.terminal_seen || s_wps_native->status.closing) return ESP_OK;
    esp32_mquickjs_wifi_wps_native_status_t *status = &s_wps_native->status;
    status->terminal_seen = true;
    status->event_id = event_id;
    if (status->error == ESP_OK)
        status->error = event_id == WIFI_EVENT_STA_WPS_ER_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
    status->failure_reason = -1;
    if (event_id == WIFI_EVENT_STA_WPS_ER_FAILED && data &&
        size == sizeof(wifi_event_sta_wps_fail_reason_t)) {
        wifi_event_sta_wps_fail_reason_t reason;
        memcpy(&reason, data, sizeof(reason));
        if (reason >= WPS_FAIL_REASON_NORMAL && reason < WPS_FAIL_REASON_MAX)
            status->failure_reason = reason;
    }
    esp32qjs_wps_native_scrub();
    return esp32qjs_wps_native_stop_sdk();
}

static int esp32qjs_wps_native_finish(struct wps_sm *sm)
{
    esp32_mquickjs_wifi_wps_native_status_t *status = &s_wps_native->status;
    if (status->terminal_seen || status->closing) return ESP_ERR_INVALID_STATE;
    status->terminal_seen = true; /* Revoke timer callbacks before disconnect. */
    status->event_id = WIFI_EVENT_STA_WPS_ER_SUCCESS;
    forced_memzero(s_wps_native->pin, sizeof(s_wps_native->pin));
    status->pin_available = false;
    if (status->error == ESP_OK &&
        (!sm->ap_cred_cnt || sm->ap_cred_cnt > ESP32_MQUICKJS_WPS_CREDENTIALS_MAX))
        status->error = ESP_ERR_INVALID_SIZE;
    for (unsigned i = 0; status->error == ESP_OK && i < sm->ap_cred_cnt; ++i) {
        const struct wps_credential *source = &sm->creds[i];
        if (!esp32qjs_wps_credential_valid(source)) {
            status->error = ESP_ERR_INVALID_SIZE;
            break;
        }
        esp32_mquickjs_wifi_wps_credential_t *target = &s_wps_native->credentials.entries[i];
        memcpy(target->ssid, source->ssid, source->ssid_len);
        memcpy(target->password, source->key, source->key_len);
        memcpy(target->mac, source->mac_addr, sizeof(target->mac));
        target->ssid_length = source->ssid_len;
        target->password_length = source->key_len;
        target->key_index = source->key_idx;
        target->auth_type = source->auth_type;
        target->encryption_type = source->encr_type;
    }
    if (status->error == ESP_OK) {
        s_wps_native->credentials.count = sm->ap_cred_cnt;
        status->credentials_available = true;
    } else {
        status->event_id = WIFI_EVENT_STA_WPS_ER_FAILED;
        esp32qjs_wps_native_scrub();
    }
    /* Managed provisioning delivers credentials only. Explicit connection and
     * security policy will be performed by the Station helper after retirement.
     * Existing unmanaged SDK success behavior remains in wps_finish(). */
    return esp32qjs_wps_native_stop_sdk();
}

esp_err_t esp32qjs_wps_native_begin(const esp_wps_config_t *config, uint64_t *identity)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
#ifdef CONFIG_WPS_REGISTRAR
    if (esp32qjs_wps_ap_result_held()) return ESP_ERR_INVALID_STATE;
#endif
    if (!config || !identity || *identity ||
        (config->wps_type != WPS_TYPE_PBC && config->wps_type != WPS_TYPE_PIN)) return ESP_ERR_INVALID_ARG;
    if (s_wps_native || gWpsSm || wps_get_owner() != WPS_OWNER_NONE || is_dpp_enabled() ||
        eloop_is_timeout_registered(wifi_station_wps_success, NULL, NULL)) return ESP_ERR_INVALID_STATE;
    if (!esp_wifi_get_user_init_flag_internal()) return ESP_ERR_WIFI_STATE;
    int error = wps_check_wifi_mode();
    if (error != ESP_OK) return error;
    if (!esp32qjs_wps_scan_quiet()) return ESP_ERR_INVALID_STATE;
    if (s_wps_native_last_identity == UINT64_MAX) return ESP_ERR_NO_MEM;
    esp32qjs_wps_native_t *entry = os_zalloc(sizeof(*entry));
    if (!entry) return ESP_ERR_NO_MEM;
    entry->status.identity = ++s_wps_native_last_identity;
    entry->status.reserved_bytes = sizeof(*entry);
    entry->status.event_id = -1;
    entry->status.failure_reason = -1;
    s_wps_native = entry;
    *identity = entry->status.identity;
    atomic_store(&s_wps_native_held, true);
    error = wifi_wps_enable_internal(entry, (void *)config);
    if (entry->status.error != ESP_OK) error = entry->status.error;
    esp32qjs_wps_record_error(error, ESP32QJS_WPS_STAGE_INIT);
    entry->status.error = error;
    entry->status.enabled = error == ESP_OK;
    if (error != ESP_OK) {
        entry->status.terminal_seen = true;
        esp32qjs_wps_native_scrub();
    }
    /* Even an enable failure retains the exact owner for native cleanup. */
    return error;
}

esp_err_t esp32qjs_wps_native_start(uint64_t identity)
{
    if (!esp32qjs_wps_native_exact(identity) || !s_wps_native->status.enabled ||
        s_wps_native->status.started || s_wps_native->status.closing ||
        s_wps_native->status.terminal_seen) return ESP_ERR_INVALID_STATE;
    s_wps_native->start_attempted = true;
    int error = wifi_station_wps_start(s_wps_native, NULL);
    if (s_wps_native->status.error != ESP_OK) error = s_wps_native->status.error;
    esp32qjs_wps_record_error(error, ESP32QJS_WPS_STAGE_INIT);
    s_wps_native->status.error = error;
    s_wps_native->status.started = error == ESP_OK && !s_wps_native->status.terminal_seen;
    if (error != ESP_OK) {
        s_wps_native->status.terminal_seen = true;
        esp32qjs_wps_native_scrub();
    }
    return error;
}

esp_err_t esp32qjs_wps_native_status(uint64_t identity,
    esp32_mquickjs_wifi_wps_native_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!esp32qjs_wps_native_exact(identity)) return ESP_ERR_INVALID_STATE;
    *status = s_wps_native->status;
    return ESP_OK;
}

esp_err_t esp32qjs_wps_native_pin_copy(uint64_t identity, uint8_t pin[8])
{
    if (!pin) return ESP_ERR_INVALID_ARG;
    if (!esp32qjs_wps_native_exact(identity) || s_wps_native->status.closing ||
        s_wps_native->status.error != ESP_OK || !s_wps_native->status.pin_available)
        return ESP_ERR_INVALID_STATE;
    memcpy(pin, s_wps_native->pin, sizeof(s_wps_native->pin));
    return ESP_OK;
}

esp_err_t esp32qjs_wps_native_pin_commit(uint64_t identity)
{
    if (!esp32qjs_wps_native_exact(identity) || s_wps_native->status.closing ||
        s_wps_native->status.error != ESP_OK || !s_wps_native->status.pin_available)
        return ESP_ERR_INVALID_STATE;
    forced_memzero(s_wps_native->pin, sizeof(s_wps_native->pin));
    s_wps_native->status.pin_available = false;
    return ESP_OK;
}

esp_err_t esp32qjs_wps_native_credentials_copy(uint64_t identity,
    esp32_mquickjs_wifi_wps_credentials_t *credentials)
{
    if (!credentials) return ESP_ERR_INVALID_ARG;
    if (!esp32qjs_wps_native_exact(identity) || s_wps_native->status.closing ||
        !s_wps_native->status.credentials_available || s_wps_native->status.error != ESP_OK ||
        s_wps_native->status.cleanup_error != ESP_OK || !s_wps_native->status.inputs_stopped)
        return ESP_ERR_INVALID_STATE;
    *credentials = s_wps_native->credentials;
    return ESP_OK;
}

esp_err_t esp32qjs_wps_native_credentials_commit(uint64_t identity)
{
    if (!esp32qjs_wps_native_exact(identity) || s_wps_native->status.closing ||
        !s_wps_native->status.credentials_available || s_wps_native->status.error != ESP_OK ||
        s_wps_native->status.cleanup_error != ESP_OK || !s_wps_native->status.inputs_stopped)
        return ESP_ERR_INVALID_STATE;
    forced_memzero(&s_wps_native->credentials, sizeof(s_wps_native->credentials));
    s_wps_native->status.credentials_available = false;
    return ESP_OK;
}

esp_err_t esp32qjs_wps_native_close(uint64_t identity)
{
    if (!esp32qjs_wps_native_exact(identity)) return ESP_ERR_INVALID_STATE;
    s_wps_native->status.closing = true;
    esp32qjs_wps_native_scrub();
    return esp32qjs_wps_native_stop_sdk();
}

esp_err_t esp32qjs_wps_native_retire_state(uint64_t identity)
{
    if (!esp32qjs_wps_native_exact(identity)) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_wps_native_status_t *status = &s_wps_native->status;
    if (!status->inputs_stopped || status->tracking_fault || status->callback_depth ||
        (!status->closing && !status->terminal_seen)) return ESP_ERR_INVALID_STATE;
    if (status->sdk_state_retired) return ESP_OK;
    int error = gWpsSm ? wifi_station_wps_deinit() : ESP_OK;
    if (error != ESP_OK) { status->cleanup_error = error; return error; }
    /* Early init OOM can leave factory storage without a WPS device/context. */
    if (s_factory_info) {
        os_free(s_factory_info);
        s_factory_info = NULL;
    }
#ifdef CONFIG_WPS_RECONNECT_ON_FAIL
    if (s_previous_wifi_config) {
        bin_clear_free(s_previous_wifi_config, sizeof(*s_previous_wifi_config));
        s_previous_wifi_config = NULL;
    }
#endif
    forced_memzero(&s_wps_success_evt, sizeof(s_wps_success_evt));
    status->cleanup_error = ESP_OK;
    status->sdk_state_retired = true;
    return ESP_OK;
}

esp_err_t esp32qjs_wps_native_finish_capture(uint64_t identity)
{
    if (!esp32qjs_wps_native_exact(identity) || !s_wps_native->status.terminal_seen ||
        s_wps_native->status.closing) return ESP_ERR_INVALID_STATE;
    return esp32qjs_wps_native_stop_sdk();
}

/* Reviewed C3/S3/C5 ieee80211_scan.o prefix. Read only, on the native task;
 * no shared driver pointer escapes. Patcher pins all three original archives.
 * scan_done clears done before invoking it. scan_cancel clears active flags
 * and calls chm_cancel_op, which synchronously cancels its native operation.
 * Legacy timer callbacks can already have posted work: caller MUST first
 * drain ESP_TIMER_TASK and then enqueue this native checkpoint. */
extern void *g_scan;
static bool esp32qjs_wps_scan_quiet(void)
{
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C5
    if (!g_scan) return false;
    struct esp32qjs_wps_scan_prefix {
        uint8_t prefix[60];
        uint32_t done, argument;
        uint8_t scan_id[2], flags[2];
    } snapshot;
    _Static_assert(offsetof(struct esp32qjs_wps_scan_prefix, done) == 60, "review scan callback offset");
    _Static_assert(offsetof(struct esp32qjs_wps_scan_prefix, flags) == 70, "review scan flags offset");
    memcpy(&snapshot, g_scan, sizeof(snapshot));
    return !snapshot.done && !(snapshot.flags[0] & 0x0f) && !snapshot.flags[1];
#else
    /* Do not interpret an unreviewed target's private scan layout. */
    return false;
#endif
}

esp_err_t esp32qjs_wps_native_checkpoint(uint64_t identity, uint32_t *revision)
{
    if (!revision) return ESP_ERR_INVALID_ARG;
    if (!esp32qjs_wps_native_exact(identity)) return ESP_ERR_INVALID_STATE;
    const esp32_mquickjs_wifi_wps_native_status_t *status = &s_wps_native->status;
    if (!status->inputs_stopped || !status->sdk_state_retired || status->tracking_fault ||
        status->callback_depth || gWpsSm || s_wps_sm_cb || gWpaSm.wpa_sm_wps_disable ||
        wps_get_owner() != WPS_OWNER_NONE || wps_get_type() != WPS_TYPE_DISABLE ||
        wps_get_status() != WPS_STATUS_DISABLE) return ESP_ERR_INVALID_STATE;
    if (s_wps_native->start_attempted && !esp32qjs_wps_scan_quiet()) return ESP_ERR_NOT_FINISHED;
    *revision = s_wps_native->callback_revision;
    return ESP_OK;
}

esp_err_t esp32qjs_wps_native_release(uint64_t identity, uint32_t revision)
{
    uint32_t current;
    int error = esp32qjs_wps_native_checkpoint(identity, &current);
    if (error != ESP_OK) return error;
    if (current != revision) return ESP_ERR_NOT_FINISHED;
    if (s_wps_native->status.pin_available || s_wps_native->status.credentials_available)
        return ESP_ERR_INVALID_STATE;
    esp32qjs_wps_native_t *retired = s_wps_native;
    s_wps_native = NULL;
    bin_clear_free(retired, sizeof(*retired));
    atomic_store(&s_wps_native_held, false);
    return ESP_OK;
}

/* This callback executes on the native task, catching a public SDK disable
 * queued before the managed begin claimed its owner. No caller-thread mutation
 * may revoke the managed record's SDK type/owner before reaching this check. */
static int esp32qjs_wps_unmanaged_disable(void *ctx, void *data)
{
#ifdef CONFIG_WPS_REGISTRAR
    if (esp32qjs_wps_ap_result_held()) return ESP_ERR_INVALID_STATE;
#endif
    if (s_wps_native) return ESP_ERR_INVALID_STATE;
    int error = wps_set_type(WPS_TYPE_DISABLE);
    return error == ESP_OK ? wifi_wps_disable_internal(ctx, data) : error;
}
