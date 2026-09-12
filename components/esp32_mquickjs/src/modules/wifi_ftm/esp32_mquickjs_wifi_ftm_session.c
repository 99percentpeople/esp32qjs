#include "esp32_mquickjs_wifi_ftm_session.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

struct esp32_mquickjs_wifi_ftm_session {
    uint32_t references;
    esp32_mquickjs_wifi_ftm_status_t status;
    esp32_mquickjs_wifi_ftm_token_t token;
    wifi_ftm_report_entry_t *entries;
    int64_t next_retry_us;
    bool busy;
};
static portMUX_TYPE s_ftm_session_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_ftm_session_t *s_ftm_active;
static uint32_t s_ftm_handles, s_ftm_reserved_entries;

/* Detach only while holding the Session lock and after excluding a worker that
 * can still write this span. Keep the budget reserved through actual free. */
static wifi_ftm_report_entry_t *ftm_detach_entries_locked(esp32_mquickjs_wifi_ftm_session_t *session,
    unsigned *capacity)
{
    wifi_ftm_report_entry_t *entries = session->entries;
    *capacity = session->status.retained_entries;
    session->entries = NULL;
    session->status.retained_entries = 0;
    session->status.report_ready = false;
    return entries;
}
static void ftm_free_entries(wifi_ftm_report_entry_t *entries, unsigned capacity)
{
    if (entries == NULL) return;
    esp32_mquickjs_memory_payload_free(entries);
    portENTER_CRITICAL(&s_ftm_session_lock);
    s_ftm_reserved_entries -= capacity;
    portEXIT_CRITICAL(&s_ftm_session_lock);
}

bool esp32_mquickjs_wifi_ftm_retain(esp32_mquickjs_wifi_ftm_session_t *session)
{
    if (session == NULL) return false;
    portENTER_CRITICAL(&s_ftm_session_lock);
    bool ok = session->references != 0U && session->references != UINT32_MAX;
    if (ok) ++session->references;
    portEXIT_CRITICAL(&s_ftm_session_lock);
    return ok;
}
void esp32_mquickjs_wifi_ftm_release(esp32_mquickjs_wifi_ftm_session_t *session)
{
    if (session == NULL) return;
    portENTER_CRITICAL(&s_ftm_session_lock);
    bool free_session = --session->references == 0U;
    portEXIT_CRITICAL(&s_ftm_session_lock);
    if (!free_session) return;
    /* No registry, worker or caller can access this final reference. */
    ftm_free_entries(session->entries, session->status.retained_entries);
    esp32_mquickjs_memory_payload_free(session);
    portENTER_CRITICAL(&s_ftm_session_lock);
    --s_ftm_handles;
    portEXIT_CRITICAL(&s_ftm_session_lock);
}

esp_err_t esp32_mquickjs_wifi_ftm_create(const wifi_ftm_initiator_cfg_t *config,
    unsigned capacity, esp32_mquickjs_wifi_ftm_session_t **output)
{
    if (!esp32_mquickjs_wifi_ftm_config_valid(config) || output == NULL || *output != NULL ||
        capacity > ESP32_MQUICKJS_WIFI_FTM_MAX_REPORT_ENTRIES) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_ftm_session_lock);
    bool admitted = s_ftm_handles < ESP32_MQUICKJS_WIFI_FTM_MAX_HANDLES &&
        capacity <= ESP32_MQUICKJS_WIFI_FTM_MAX_RETAINED_ENTRIES - s_ftm_reserved_entries;
    if (admitted) { ++s_ftm_handles; s_ftm_reserved_entries += capacity; }
    portEXIT_CRITICAL(&s_ftm_session_lock);
    if (!admitted) return ESP_ERR_NO_MEM;
    esp32_mquickjs_wifi_ftm_session_t *session = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*session), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    wifi_ftm_report_entry_t *entries = NULL;
    if (session != NULL && capacity != 0U)
        entries = esp32_mquickjs_memory_wireless_calloc("wifi", capacity, sizeof(*entries), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
    if (session == NULL || (capacity != 0U && entries == NULL)) {
        if (session != NULL) esp32_mquickjs_memory_payload_free(session);
        portENTER_CRITICAL(&s_ftm_session_lock);
        --s_ftm_handles;
        s_ftm_reserved_entries -= capacity;
        portEXIT_CRITICAL(&s_ftm_session_lock);
        return ESP_ERR_NO_MEM;
    }
    session->references = 1;
    session->entries = entries;
    session->status.config = *config;
    session->status.report_capacity = session->status.retained_entries = (uint16_t)capacity;
    *output = session;
    return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_ftm_start(esp32_mquickjs_wifi_ftm_session_t *session)
{
    if (session == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_ftm_session_lock);
    bool ok = s_ftm_active == NULL && !session->status.started && !session->status.end_requested &&
        !session->status.close_requested && !session->status.retired &&
        session->references != 0U && session->references != UINT32_MAX;
    if (ok) {
        ++session->references;
        s_ftm_active = session;
        session->status.started = true;
    }
    portEXIT_CRITICAL(&s_ftm_session_lock);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}
void esp32_mquickjs_wifi_ftm_end(esp32_mquickjs_wifi_ftm_session_t *session)
{
    if (session == NULL) return;
    portENTER_CRITICAL(&s_ftm_session_lock);
    session->status.end_requested = true;
    if (!session->status.started) session->status.retired = true;
    session->next_retry_us = 0;
    portEXIT_CRITICAL(&s_ftm_session_lock);
}
void esp32_mquickjs_wifi_ftm_close(esp32_mquickjs_wifi_ftm_session_t *session)
{
    if (session == NULL) return;
    wifi_ftm_report_entry_t *entries = NULL;
    unsigned capacity = 0;
    portENTER_CRITICAL(&s_ftm_session_lock);
    session->status.end_requested = session->status.close_requested = true;
    session->status.report_ready = false;
    if (!session->status.started) session->status.retired = true;
    session->next_retry_us = 0;
    if (!session->busy) entries = ftm_detach_entries_locked(session, &capacity);
    portEXIT_CRITICAL(&s_ftm_session_lock);
    ftm_free_entries(entries, capacity);
}
bool esp32_mquickjs_wifi_ftm_status(esp32_mquickjs_wifi_ftm_session_t *session,
    esp32_mquickjs_wifi_ftm_status_t *output)
{
    if (session == NULL || output == NULL) return false;
    portENTER_CRITICAL(&s_ftm_session_lock);
    *output = session->status;
    portEXIT_CRITICAL(&s_ftm_session_lock);
    return true;
}
bool esp32_mquickjs_wifi_ftm_report_entry(esp32_mquickjs_wifi_ftm_session_t *session,
    unsigned index, wifi_ftm_report_entry_t *output)
{
    if (session == NULL || output == NULL) return false;
    portENTER_CRITICAL(&s_ftm_session_lock);
    bool ok = session->status.report_ready && session->status.retired && !session->status.close_requested &&
        session->entries != NULL && index < session->status.native.copied_entries &&
        index < session->status.retained_entries;
    if (ok) *output = session->entries[index];
    portEXIT_CRITICAL(&s_ftm_session_lock);
    return ok;
}

static void ftm_worker(void *opaque)
{
    esp32_mquickjs_wifi_ftm_session_t *session = opaque;
    portENTER_CRITICAL(&s_ftm_session_lock);
    esp32_mquickjs_wifi_ftm_status_t status = session->status;
    esp32_mquickjs_wifi_ftm_token_t token = session->token;
    wifi_ftm_report_entry_t *entries = session->entries;
    portEXIT_CRITICAL(&s_ftm_session_lock);
    if (!status.submitted && !status.close_requested && !status.end_requested) {
        status.stage = "ftm-submit";
        status.error = esp32_mquickjs_wifi_radio_ftm_start(&status.config, &token);
        status.submitted = true;
    }
    /* Intent can change while synchronous SDK work is running. Only the worker
     * owns token/report mutation; a closer never frees the busy worker's span. */
    portENTER_CRITICAL(&s_ftm_session_lock);
    status.end_requested |= session->status.end_requested;
    status.close_requested |= session->status.close_requested;
    portEXIT_CRITICAL(&s_ftm_session_lock);
    status.cleanup_error = ESP_OK;
    status.cleanup_stage = NULL;
    if (token.identity != 0U) {
        if (!esp32_mquickjs_wifi_radio_ftm_status(&token, &status.native)) {
            status.cleanup_error = ESP_ERR_INVALID_STATE;
            status.cleanup_stage = "ftm-identity";
        } else {
            if (status.native.ambiguous && status.error == ESP_OK) {
                status.error = ESP_ERR_INVALID_RESPONSE;
                status.stage = "ftm-report-identity";
            }
            if ((status.end_requested || status.close_requested || status.error != ESP_OK) &&
                !status.native.terminal && !status.native.end_written) {
                status.cleanup_error = esp32_mquickjs_wifi_radio_ftm_end(&token);
                if (status.cleanup_error != ESP_OK) status.cleanup_stage = "ftm-end";
            }
            if (!status.native.report_consumed && status.native.terminal) {
                bool discard = status.close_requested || status.error != ESP_OK;
                unsigned capacity = discard ? 0U : status.retained_entries;
                esp_err_t err = esp32_mquickjs_wifi_radio_ftm_collect(&token,
                    capacity != 0U ? entries : NULL, capacity * sizeof(*entries), capacity, &status.native);
                if (err != ESP_OK) { status.cleanup_error = err; status.cleanup_stage = "ftm-report-drain"; }
            }
            if (status.native.report_consumed) {
                esp_err_t err = esp32_mquickjs_wifi_radio_ftm_retire(&token, &status.native);
                if (err == ESP_OK) { status.cleanup_error = ESP_OK; status.cleanup_stage = NULL; }
                else if (status.cleanup_error == ESP_OK) {
                    status.cleanup_error = err;
                    status.cleanup_stage = "ftm-terminal-fence";
                }
            }
        }
    }
    /* collect/retire may observe physical termination after the first status
     * read. Normalize the final snapshot before any public completion. */
    if (status.native.physical_termination && status.error == ESP_OK) {
        status.error = ESP_ERR_INVALID_STATE;
        status.stage = "ftm-physical-recovery";
    }
    status.retired = token.identity == 0U;
    status.report_ready = status.retired && status.native.report_consumed && !status.native.ambiguous &&
        !status.native.physical_termination && status.error == ESP_OK;
    if (status.error == ESP_OK) status.stage = NULL;
    wifi_ftm_report_entry_t *discarded = NULL;
    unsigned discarded_capacity = 0;
    int64_t retry = esp_timer_get_time() + 100000;
    portENTER_CRITICAL(&s_ftm_session_lock);
    status.end_requested |= session->status.end_requested;
    status.close_requested |= session->status.close_requested;
    session->status = status;
    session->token = token;
    session->next_retry_us = retry;
    if (status.close_requested || status.native.physical_termination) discarded = ftm_detach_entries_locked(session, &discarded_capacity);
    session->busy = false;
    bool detach = status.retired && s_ftm_active == session;
    if (detach) s_ftm_active = NULL;
    portEXIT_CRITICAL(&s_ftm_session_lock);
    ftm_free_entries(discarded, discarded_capacity);
    if (detach) esp32_mquickjs_wifi_ftm_release(session);
    /* Final access: no JS/runtime pointer or wake captured by queue work. */
    esp32_mquickjs_wifi_ftm_release(session);
}

bool esp32_mquickjs_wifi_ftm_service(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_ftm_session_lock);
    esp32_mquickjs_wifi_ftm_session_t *session = s_ftm_active;
    bool submit = session != NULL && !session->busy && now >= session->next_retry_us && session->references != UINT32_MAX;
    if (submit) { session->busy = true; ++session->references; }
    portEXIT_CRITICAL(&s_ftm_session_lock);
    if (!submit) return false;
    if (esp32_mquickjs_submit_background_worker(ftm_worker, session)) return true;
    wifi_ftm_report_entry_t *discarded = NULL;
    unsigned discarded_capacity = 0;
    portENTER_CRITICAL(&s_ftm_session_lock);
    session->busy = false;
    if (session->status.close_requested) discarded = ftm_detach_entries_locked(session, &discarded_capacity);
    session->next_retry_us = now + 100000;
    session->status.cleanup_error = ESP_ERR_NO_MEM;
    session->status.cleanup_stage = "ftm-worker-queue";
    portEXIT_CRITICAL(&s_ftm_session_lock);
    ftm_free_entries(discarded, discarded_capacity);
    esp32_mquickjs_wifi_ftm_release(session);
    return false;
}
bool esp32_mquickjs_wifi_ftm_prepare_runtime_destroy(void)
{
    portENTER_CRITICAL(&s_ftm_session_lock);
    if (s_ftm_active != NULL) {
        s_ftm_active->status.end_requested = s_ftm_active->status.close_requested = true;
        s_ftm_active->status.report_ready = false;
        s_ftm_active->next_retry_us = 0;
    }
    portEXIT_CRITICAL(&s_ftm_session_lock);
    (void)esp32_mquickjs_wifi_ftm_service();
    portENTER_CRITICAL(&s_ftm_session_lock);
    bool drained = s_ftm_active == NULL;
    portEXIT_CRITICAL(&s_ftm_session_lock);
    return drained;
}
bool esp32_mquickjs_wifi_ftm_current_status(esp32_mquickjs_wifi_ftm_status_t *output)
{
    if (output == NULL) return false;
    portENTER_CRITICAL(&s_ftm_session_lock);
    bool active = s_ftm_active != NULL;
    if (active) *output = s_ftm_active->status;
    portEXIT_CRITICAL(&s_ftm_session_lock);
    return active;
}
void esp32_mquickjs_wifi_ftm_counts(esp32_mquickjs_wifi_ftm_counts_t *output)
{
    if (output == NULL) return;
    portENTER_CRITICAL(&s_ftm_session_lock);
    *output = (esp32_mquickjs_wifi_ftm_counts_t){.handles = s_ftm_handles,
        .reserved_entries = s_ftm_reserved_entries, .active = s_ftm_active != NULL,
        .worker_busy = s_ftm_active != NULL && s_ftm_active->busy,
        .cleanup_pending = s_ftm_active != NULL && (s_ftm_active->status.end_requested ||
            s_ftm_active->status.close_requested || s_ftm_active->status.native.terminal ||
            s_ftm_active->status.error != ESP_OK || s_ftm_active->status.cleanup_error != ESP_OK)};
    portEXIT_CRITICAL(&s_ftm_session_lock);
}
#endif
