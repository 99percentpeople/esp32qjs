#include "esp32_mquickjs_wifi_roc_session.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_action_sdk.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <limits.h>

struct esp32_mquickjs_wifi_roc_session {
    uint32_t references;
    esp32_mquickjs_wifi_roc_status_t status;
    esp32_mquickjs_wifi_action_token_t token;
    int64_t next_retry_us;
    bool busy;
};
static portMUX_TYPE s_roc_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_roc_session_t *s_roc_active;
static uint32_t s_roc_handles;

bool esp32_mquickjs_wifi_roc_retain(esp32_mquickjs_wifi_roc_session_t *session)
{
    if (session == NULL) return false;
    portENTER_CRITICAL(&s_roc_lock);
    bool ok = session->references != 0U && session->references != UINT32_MAX;
    if (ok) ++session->references;
    portEXIT_CRITICAL(&s_roc_lock);
    return ok;
}
void esp32_mquickjs_wifi_roc_release(esp32_mquickjs_wifi_roc_session_t *session)
{
    if (session == NULL) return;
    portENTER_CRITICAL(&s_roc_lock);
    bool free_session = --session->references == 0U;
    if (free_session) --s_roc_handles;
    portEXIT_CRITICAL(&s_roc_lock);
    if (free_session) esp32_mquickjs_memory_payload_free(session);
}
esp_err_t esp32_mquickjs_wifi_roc_create(const wifi_roc_req_t *request,
    esp32_mquickjs_wifi_roc_session_t **output)
{
    if (!request || !output || *output || request->type != WIFI_ROC_REQ || request->op_id != 0 ||
        request->rx_cb != esp32_mquickjs_wifi_action_receive || request->done_cb != NULL ||
        (request->ifx != WIFI_IF_STA && request->ifx != WIFI_IF_AP) || request->channel == 0 ||
        request->wait_time_ms < 1 || request->wait_time_ms > 60000 ||
        (request->sec_channel != WIFI_SECOND_CHAN_NONE && request->sec_channel != WIFI_SECOND_CHAN_ABOVE &&
         request->sec_channel != WIFI_SECOND_CHAN_BELOW)) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_roc_lock);
    bool admitted = s_roc_handles < ESP32_MQUICKJS_WIFI_ROC_MAX_HANDLES;
    if (admitted) ++s_roc_handles;
    portEXIT_CRITICAL(&s_roc_lock);
    if (!admitted) return ESP_ERR_NO_MEM;
    esp32_mquickjs_wifi_roc_session_t *session = esp32_mquickjs_memory_wireless_calloc("wifi.action", 1, sizeof(*session), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!session) {
        portENTER_CRITICAL(&s_roc_lock);
        --s_roc_handles;
        portEXIT_CRITICAL(&s_roc_lock);
        return ESP_ERR_NO_MEM;
    }
    session->references = 1;
    session->status.request = *request;
    *output = session;
    return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_roc_start(esp32_mquickjs_wifi_roc_session_t *session)
{
    if (session == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_roc_lock);
    bool ok = s_roc_active == NULL && !session->status.started && !session->status.close_requested &&
        !session->status.retired && session->references != UINT32_MAX && session->references != 0U;
    if (ok) {
        ++session->references; /* Active registry survives Future and JS GC. */
        s_roc_active = session;
        session->status.started = true;
    }
    portEXIT_CRITICAL(&s_roc_lock);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}
void esp32_mquickjs_wifi_roc_close(esp32_mquickjs_wifi_roc_session_t *session)
{
    if (session == NULL) return;
    portENTER_CRITICAL(&s_roc_lock);
    session->status.close_requested = true;
    if (!session->status.started) session->status.retired = true;
    session->next_retry_us = 0;
    portEXIT_CRITICAL(&s_roc_lock);
}
bool esp32_mquickjs_wifi_roc_status(esp32_mquickjs_wifi_roc_session_t *session,
    esp32_mquickjs_wifi_roc_status_t *output)
{
    if (!session || !output) return false;
    portENTER_CRITICAL(&s_roc_lock);
    *output = session->status;
    portEXIT_CRITICAL(&s_roc_lock);
    return true;
}

static void roc_worker(void *opaque)
{
    esp32_mquickjs_wifi_roc_session_t *session = opaque;
    portENTER_CRITICAL(&s_roc_lock);
    esp32_mquickjs_wifi_roc_status_t status = session->status;
    esp32_mquickjs_wifi_action_token_t token = session->token;
    portEXIT_CRITICAL(&s_roc_lock);
    if (!status.submitted && !status.close_requested) {
        status.stage = "roc-submit";
        status.error = esp32_mquickjs_wifi_radio_action_roc(&status.request, &token);
        status.submitted = true;
    }
    /* Re-read close intent after synchronous SDK I/O. Never publish an older
     * copy over a timeout/finalizer/runtime request racing this worker. */
    portENTER_CRITICAL(&s_roc_lock);
    status.close_requested |= session->status.close_requested;
    portEXIT_CRITICAL(&s_roc_lock);
    status.cleanup_error = ESP_OK;
    status.cleanup_stage = NULL;
    if (token.identity != 0U) {
        if (!esp32_mquickjs_wifi_radio_action_status(&token, &status.native)) {
            status.cleanup_error = ESP_ERR_INVALID_STATE;
            status.cleanup_stage = "roc-identity";
        } else {
            if (status.native.physical_termination && status.error == ESP_OK) {
                status.error = ESP_ERR_INVALID_STATE;
                status.stage = "roc-native-terminated";
            }
            if ((status.close_requested || status.error != ESP_OK) && !status.native.terminal && !status.native.cancel_written) {
                status.cleanup_error = esp32_mquickjs_wifi_radio_action_cancel(&token);
                if (status.cleanup_error != ESP_OK) status.cleanup_stage = "roc-cancel";
            }
            {
                /* A normal ROC may end without a delivered event. Probe native
                 * retirement as well; an in-progress observation is not a fault. */
                esp_err_t error = esp32_mquickjs_wifi_radio_action_retire(&token, &status.native);
                if (error == ESP_OK) { status.cleanup_error = ESP_OK; status.cleanup_stage = NULL; }
                else if (status.cleanup_error == ESP_OK &&
                    (error != ESP_ERR_TIMEOUT || status.close_requested || status.error != ESP_OK ||
                     status.native.terminal || status.native.ambiguous || status.native.sdk_quiescent)) {
                    status.cleanup_error = error;
                    status.cleanup_stage = "roc-terminal-fence";
                }
            }
        }
    }
    status.retired = token.identity == 0U;
    if (status.error == ESP_OK) status.stage = NULL;
    int64_t retry = esp_timer_get_time() + 100000;
    portENTER_CRITICAL(&s_roc_lock);
    status.close_requested |= session->status.close_requested;
    session->status = status;
    session->token = token;
    session->next_retry_us = retry;
    session->busy = false;
    bool detach = status.retired && s_roc_active == session;
    if (detach) s_roc_active = NULL;
    portEXIT_CRITICAL(&s_roc_lock);
    if (detach) esp32_mquickjs_wifi_roc_release(session);
    /* Final session access. No runtime pointer/wake in worker queue items. */
    esp32_mquickjs_wifi_roc_release(session);
}

bool esp32_mquickjs_wifi_roc_service(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_roc_lock);
    esp32_mquickjs_wifi_roc_session_t *session = s_roc_active;
    bool submit = session && !session->busy && now >= session->next_retry_us && session->references != UINT32_MAX;
    if (submit) { session->busy = true; ++session->references; }
    portEXIT_CRITICAL(&s_roc_lock);
    if (!submit) return false;
    if (esp32_mquickjs_submit_background_worker(roc_worker, session)) return true;
    portENTER_CRITICAL(&s_roc_lock);
    session->busy = false;
    session->next_retry_us = now + 100000;
    portEXIT_CRITICAL(&s_roc_lock);
    esp32_mquickjs_wifi_roc_release(session);
    return false;
}
bool esp32_mquickjs_wifi_roc_prepare_runtime_destroy(void)
{
    portENTER_CRITICAL(&s_roc_lock);
    if (s_roc_active) {
        s_roc_active->status.close_requested = true;
        s_roc_active->next_retry_us = 0;
    }
    portEXIT_CRITICAL(&s_roc_lock);
    (void)esp32_mquickjs_wifi_roc_service();
    portENTER_CRITICAL(&s_roc_lock);
    bool drained = s_roc_active == NULL;
    portEXIT_CRITICAL(&s_roc_lock);
    return drained;
}
bool esp32_mquickjs_wifi_roc_current_status(esp32_mquickjs_wifi_roc_status_t *output)
{
    if (!output) return false;
    portENTER_CRITICAL(&s_roc_lock);
    bool active = s_roc_active != NULL;
    if (active) *output = s_roc_active->status;
    portEXIT_CRITICAL(&s_roc_lock);
    return active;
}
void esp32_mquickjs_wifi_roc_counts(uint32_t *handles, bool *active, bool *cleanup)
{
    portENTER_CRITICAL(&s_roc_lock);
    if (handles) *handles = s_roc_handles;
    if (active) *active = s_roc_active != NULL;
    if (cleanup) *cleanup = s_roc_active && (s_roc_active->status.close_requested ||
        s_roc_active->status.native.terminal || s_roc_active->status.error != ESP_OK || s_roc_active->status.cleanup_error != ESP_OK);
    portEXIT_CRITICAL(&s_roc_lock);
}
#endif
