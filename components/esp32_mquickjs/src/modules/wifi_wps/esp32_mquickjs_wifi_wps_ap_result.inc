/* Included in build-local esp_hostpad_wps.c. The only cross-task access is
 * the atomic admission hint; SDK hooks and future worker IPC run on Wi-Fi. */
#include <stdatomic.h>
#include "esp32_mquickjs_wifi_wps_ap_result.h"
#include "esp32_mquickjs_wifi_wps_sdk.h"
extern bool current_task_is_wifi_task(void);

typedef struct {
    esp32_mquickjs_wifi_wps_ap_result_status_t status;
    void *context;
    uint8_t pin[8];
    bool ever_bound;
    bool retiring, start_attempted, heap_retired;
    uint32_t callback_revision;
} esp32qjs_wps_ap_result_t;

static esp32qjs_wps_ap_result_t *s_wps_ap_result;
static uint32_t s_wps_ap_result_last_identity;
static atomic_bool s_wps_ap_result_held;

bool esp32qjs_wps_ap_result_held(void)
{
    return atomic_load(&s_wps_ap_result_held);
}

static bool esp32qjs_wps_ap_result_exact(uint32_t identity)
{
    return current_task_is_wifi_task() && identity && s_wps_ap_result &&
        s_wps_ap_result->status.identity == identity;
}

/* A zero identity denotes an ordinary SDK command. Check on execution, not
 * just on submission: an old eloop command may follow a managed reservation. */
static bool esp32qjs_wps_ap_command_allowed(uint32_t identity)
{
    if (!current_task_is_wifi_task()) return false;
    if (!s_wps_ap_result || !s_wps_ap_result->status.retained) return identity == 0;
    return esp32qjs_wps_ap_result_exact(identity);
}

static void esp32qjs_wps_ap_result_scrub(void)
{
    forced_memzero(s_wps_ap_result->pin, sizeof(s_wps_ap_result->pin));
    s_wps_ap_result->status.pin_available = false;
}

static void esp32qjs_wps_ap_result_free(void)
{
    bin_clear_free(s_wps_ap_result, sizeof(*s_wps_ap_result));
    s_wps_ap_result = NULL;
    atomic_store(&s_wps_ap_result_held, false);
}

static esp_err_t esp32qjs_wps_ap_result_create(bool retained)
{
    if (!current_task_is_wifi_task() || s_wps_ap_result ||
        esp32qjs_wps_native_held() || gWpsSm || wps_get_owner() != WPS_OWNER_NONE)
        return ESP_ERR_INVALID_STATE;
    if (s_wps_ap_result_last_identity == UINT32_MAX) return ESP_ERR_NO_MEM;
    esp32qjs_wps_ap_result_t *entry = os_zalloc(sizeof(*entry));
    if (!entry) return ESP_ERR_NO_MEM;
    entry->status.identity = ++s_wps_ap_result_last_identity;
    entry->status.event_id = -1;
    entry->status.failure_reason = -1;
    entry->status.retained = retained;
    entry->status.reserved_bytes = sizeof(*entry);
    s_wps_ap_result = entry;
    atomic_store(&s_wps_ap_result_held, true);
    return ESP_OK;
}

esp_err_t esp32qjs_wps_ap_result_reserve(uint32_t *identity)
{
    if (!identity || *identity) return ESP_ERR_INVALID_ARG;
    esp_err_t error = esp32qjs_wps_ap_result_create(true);
    if (error == ESP_OK) *identity = s_wps_ap_result->status.identity;
    return error;
}

esp_err_t esp32qjs_wps_ap_result_discard_unbound(uint32_t identity)
{
    if (!esp32qjs_wps_ap_result_exact(identity) || s_wps_ap_result->ever_bound)
        return ESP_ERR_INVALID_STATE;
    esp32qjs_wps_ap_result_free();
    return ESP_OK;
}

esp_err_t esp32qjs_wps_ap_result_bind(void *context)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    if (!context) return ESP_ERR_INVALID_ARG;
    if (!esp32qjs_wps_ap_result_can_bind()) return ESP_ERR_INVALID_STATE;
    if (!s_wps_ap_result) {
        esp_err_t error = esp32qjs_wps_ap_result_create(false);
        if (error != ESP_OK) return error;
    }
    if (s_wps_ap_result->ever_bound || s_wps_ap_result->status.closing ||
        esp32qjs_wps_native_held()) return ESP_ERR_INVALID_STATE;
    s_wps_ap_result->context = context;
    s_wps_ap_result->ever_bound = true;
    s_wps_ap_result->status.sdk_attached = true;
    return ESP_OK;
}

bool esp32qjs_wps_ap_result_can_bind(void)
{
    return current_task_is_wifi_task() && !gWpsSm && wps_get_owner() == WPS_OWNER_NONE &&
        !esp32qjs_wps_native_held() && (!s_wps_ap_result ||
        (!s_wps_ap_result->ever_bound && !s_wps_ap_result->status.closing));
}

void esp32qjs_wps_ap_result_cleanup_error(void *context, esp_err_t error)
{
    if (esp32qjs_wps_ap_result_identity(context))
        s_wps_ap_result->status.cleanup_error = error;
}

void esp32qjs_wps_ap_result_detach(void *context, esp_err_t error)
{
    if (!current_task_is_wifi_task() || !s_wps_ap_result ||
        !s_wps_ap_result->status.sdk_attached || s_wps_ap_result->context != context)
        return;
    if (s_wps_ap_result->status.callback_depth || s_wps_ap_result->status.tracking_fault ||
        !esp32qjs_wps_ap_eapol_drained(s_wps_ap_result->status.identity)) {
        s_wps_ap_result->status.cleanup_error = ESP_ERR_INVALID_STATE;
        return;
    }
    s_wps_ap_result->context = NULL;
    s_wps_ap_result->status.sdk_attached = false;
    esp32qjs_wps_ap_result_scrub();
    if (error != ESP_OK && !s_wps_ap_result->status.terminal) {
        s_wps_ap_result->status.error = error;
        s_wps_ap_result->status.event_id = WIFI_EVENT_AP_WPS_RG_FAILED;
        s_wps_ap_result->status.terminal = true;
    }
    /* A retained result outlives SDK heap teardown. Release after a proven
     * native drain is deliberately a separate lifecycle implementation. */
    if (!s_wps_ap_result->status.retained) esp32qjs_wps_ap_result_free();
}

uint32_t esp32qjs_wps_ap_result_identity(const void *context)
{
    if (!current_task_is_wifi_task() || !s_wps_ap_result ||
        !s_wps_ap_result->status.sdk_attached || s_wps_ap_result->context != context)
        return 0;
    return s_wps_ap_result->status.identity;
}

void *esp32qjs_wps_ap_result_context(uint32_t identity)
{
    if (!esp32qjs_wps_ap_result_exact(identity) || !s_wps_ap_result->status.sdk_attached ||
        s_wps_ap_result->status.closing || s_wps_ap_result->status.terminal) return NULL;
    return s_wps_ap_result->context;
}

void *esp32qjs_wps_ap_result_callback_enter(uint32_t identity)
{
    if (!esp32qjs_wps_ap_result_exact(identity) || s_wps_ap_result->status.terminal) return NULL;
    return esp32qjs_wps_ap_result_activity_enter(identity);
}

void *esp32qjs_wps_ap_result_native_context(uint32_t identity)
{
    if (!esp32qjs_wps_ap_result_exact(identity) || !s_wps_ap_result->status.sdk_attached ||
        s_wps_ap_result->status.closing || s_wps_ap_result->status.tracking_fault) return NULL;
    return s_wps_ap_result->context;
}

void *esp32qjs_wps_ap_result_activity_enter(uint32_t identity)
{
    if (!esp32qjs_wps_ap_result_exact(identity)) return NULL;
    if (s_wps_ap_result->callback_revision == UINT32_MAX) {
        s_wps_ap_result->status.tracking_fault = true;
        s_wps_ap_result->status.cleanup_error = ESP_ERR_INVALID_STATE;
        return NULL;
    }
    ++s_wps_ap_result->callback_revision;
    void *context = esp32qjs_wps_ap_result_native_context(identity);
    if (!context || s_wps_ap_result->status.tracking_fault) return NULL;
    if (s_wps_ap_result->status.callback_depth == UINT32_MAX) {
        s_wps_ap_result->status.tracking_fault = true;
        s_wps_ap_result->status.cleanup_error = ESP_ERR_INVALID_STATE;
        return NULL;
    }
    ++s_wps_ap_result->status.callback_depth;
    return context;
}

bool esp32qjs_wps_ap_result_busy(const void *context)
{
    return esp32qjs_wps_ap_result_identity(context) &&
        (s_wps_ap_result->status.callback_depth || s_wps_ap_result->status.tracking_fault);
}

esp_err_t esp32qjs_wps_ap_result_peer_error(uint32_t identity, esp_err_t error,
    const uint8_t peer[6])
{
    void *context = esp32qjs_wps_ap_result_context(identity);
    if (!context) return ESP_ERR_INVALID_STATE;
    if (!peer || error == ESP_OK) return ESP_ERR_INVALID_ARG;
    wifi_event_ap_wps_rg_fail_reason_t event = {0};
    event.reason = WPS_AP_FAIL_REASON_NORMAL;
    memcpy(event.peer_macaddr, peer, sizeof(event.peer_macaddr));
    s_wps_ap_result->status.error = error;
    return esp32qjs_wps_ap_result_event(context, WIFI_EVENT_AP_WPS_RG_FAILED,
                                       &event, sizeof(event));
}

void esp32qjs_wps_ap_result_callback_leave(uint32_t identity)
{
    /* Closing or a terminal event revokes entry, never the matching leave. */
    if (!esp32qjs_wps_ap_result_exact(identity)) return;
    if (!s_wps_ap_result->status.callback_depth) {
        s_wps_ap_result->status.tracking_fault = true;
        s_wps_ap_result->status.cleanup_error = ESP_ERR_INVALID_STATE;
        return;
    }
    --s_wps_ap_result->status.callback_depth;
}

esp_err_t esp32qjs_wps_ap_result_prepare_close(void *context)
{
    uint32_t identity = esp32qjs_wps_ap_result_identity(context);
    if (!identity) return ESP_ERR_INVALID_STATE;
    esp32qjs_wps_ap_result_close_intent(identity);
    esp32_mquickjs_wifi_wps_ap_result_status_t *status = &s_wps_ap_result->status;
    if (status->callback_depth || status->tracking_fault) {
        status->cleanup_error = ESP_ERR_INVALID_STATE;
        return status->cleanup_error;
    }
    while (status->cleanup_stage < ESP32QJS_WPS_AP_CLOSE_PREPARED) {
        esp_err_t error = esp32qjs_wps_ap_cleanup_step(context, status->cleanup_stage);
        status->cleanup_error = error;
        if (error != ESP_OK) return error;
        ++status->cleanup_stage;
    }
    status->cleanup_error = ESP_OK;
    status->close_prepared = true;
    return ESP_OK;
}

bool esp32qjs_wps_ap_result_deinit_allowed(void *context)
{
    if (!esp32qjs_wps_ap_result_identity(context)) return false;
    const esp32_mquickjs_wifi_wps_ap_result_status_t *status = &s_wps_ap_result->status;
    /* The managed retire command opens this permission only for the duration
     * of its synchronous SDK destructor, after the worker's ordered drain. */
    return status->close_prepared && !status->callback_depth &&
        !status->tracking_fault && (!status->retained || s_wps_ap_result->retiring);
}

esp_err_t esp32qjs_wps_ap_result_status(uint32_t identity,
    esp32_mquickjs_wifi_wps_ap_result_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!esp32qjs_wps_ap_result_exact(identity)) return ESP_ERR_INVALID_STATE;
    *status = s_wps_ap_result->status;
    return ESP_OK;
}

esp_err_t esp32qjs_wps_ap_result_pin_copy(uint32_t identity, uint8_t pin[8])
{
    if (!pin) return ESP_ERR_INVALID_ARG;
    if (!esp32qjs_wps_ap_result_exact(identity) || !s_wps_ap_result->status.pin_available ||
        s_wps_ap_result->status.closing || s_wps_ap_result->status.terminal)
        return ESP_ERR_INVALID_STATE;
    memcpy(pin, s_wps_ap_result->pin, sizeof(s_wps_ap_result->pin));
    return ESP_OK;
}

esp_err_t esp32qjs_wps_ap_result_pin_commit(uint32_t identity)
{
    if (!esp32qjs_wps_ap_result_exact(identity) || !s_wps_ap_result->status.pin_available ||
        s_wps_ap_result->status.closing || s_wps_ap_result->status.terminal)
        return ESP_ERR_INVALID_STATE;
    esp32qjs_wps_ap_result_scrub();
    return ESP_OK;
}

esp_err_t esp32qjs_wps_ap_result_close_intent(uint32_t identity)
{
    if (!esp32qjs_wps_ap_result_exact(identity)) return ESP_ERR_INVALID_STATE;
    s_wps_ap_result->status.closing = true;
    esp32qjs_wps_ap_result_scrub();
    /* This revokes result delivery only. It neither stops SDK state nor frees
     * anything still referenced by native callbacks. */
    return ESP_OK;
}

esp_err_t esp32qjs_wps_ap_result_event(void *context, int32_t event_id,
    const void *data, size_t size)
{
    uint32_t identity = esp32qjs_wps_ap_result_identity(context);
    if (!identity || !esp32qjs_wps_ap_result_context(identity)) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_wps_ap_result_status_t *status = &s_wps_ap_result->status;
    bool valid = true;
    if (event_id == WIFI_EVENT_AP_WPS_RG_PIN) {
        valid = data && size == sizeof(s_wps_ap_result->pin);
        if (valid) {
            memcpy(s_wps_ap_result->pin, data, sizeof(s_wps_ap_result->pin));
            status->pin_available = true;
        }
    } else if (event_id == WIFI_EVENT_AP_WPS_RG_SUCCESS) {
        valid = data && size == sizeof(wifi_event_ap_wps_rg_success_t);
        if (valid) memcpy(status->peer, ((const wifi_event_ap_wps_rg_success_t *)data)->peer_macaddr, 6);
    } else if (event_id == WIFI_EVENT_AP_WPS_RG_FAILED) {
        valid = data && size == sizeof(wifi_event_ap_wps_rg_fail_reason_t);
        if (valid) {
            wifi_event_ap_wps_rg_fail_reason_t event;
            memcpy(&event, data, sizeof(event));
            valid = event.reason >= WPS_AP_FAIL_REASON_NORMAL && event.reason < WPS_AP_FAIL_REASON_MAX;
            if (valid) { status->failure_reason = event.reason; memcpy(status->peer, event.peer_macaddr, 6); }
        }
    } else if (event_id == WIFI_EVENT_AP_WPS_RG_TIMEOUT || event_id == WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP) {
        valid = !data && size == 0;
    } else return ESP_ERR_INVALID_ARG;
    if (!valid) {
        status->error = ESP_ERR_INVALID_SIZE;
        event_id = WIFI_EVENT_AP_WPS_RG_FAILED;
    }
    if (event_id != WIFI_EVENT_AP_WPS_RG_PIN) {
        status->event_id = event_id;
        status->terminal = true;
        if (status->error == ESP_OK && event_id != WIFI_EVENT_AP_WPS_RG_SUCCESS)
            status->error = event_id == WIFI_EVENT_AP_WPS_RG_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        esp32qjs_wps_ap_result_scrub();
    }
    /* Commit native state first. Managed results (including PIN) are polled by
     * the worker and never put on this observation queue. Legacy SDK callers
     * retain their event shape with zero wait; queue failure cannot undo state. */
    if (!valid) return ESP_ERR_INVALID_SIZE;
    if (status->retained) return ESP_OK;
    return esp_event_post(WIFI_EVENT, event_id, data, size, 0);
}
