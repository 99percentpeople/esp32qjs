#include "esp32_mquickjs_wifi_mesh_session.h"
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

struct esp32_mquickjs_wifi_mesh_session {
    esp32_mquickjs_wifi_mesh_session_status_t status;
    esp32_mquickjs_wifi_mesh_radio_token_t token;
    esp32_mquickjs_wifi_mesh_config_t config;
    esp32_mquickjs_wifi_mesh_job_t *job;
    uint32_t references, timeout_ms, read_identity[3];
    int64_t deadline_us, next_poll_us;
    bool worker_started, allow_ap_restart;
};
struct esp32_mquickjs_wifi_mesh_job {
    esp32_mquickjs_wifi_mesh_session_t *parent;
    esp32_mquickjs_wifi_mesh_job_status_t status;
    esp32_mquickjs_wifi_mesh_send_t send;
    esp32_mquickjs_wifi_mesh_control_t control;
    uint32_t references, timeout_ms;
    int64_t deadline_us;
    size_t allocation;
    bool is_send;
    /* Routing/group SDK calls consume mesh_addr_t arrays in this storage. */
    _Alignas(esp32_mquickjs_wifi_mesh_control_data_t) uint8_t data[];
};
_Static_assert(_Alignof(esp32_mquickjs_wifi_mesh_control_data_t) >= _Alignof(esp32_mquickjs_wifi_mesh_scan_record_t) &&
    sizeof(esp32_mquickjs_wifi_mesh_control_data_t) % _Alignof(esp32_mquickjs_wifi_mesh_scan_record_t) == 0,
    "Mesh scan job record alignment");
static portMUX_TYPE s_mesh_session_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_mesh_session_t *s_mesh_active;
static uint32_t s_mesh_handles, s_mesh_jobs, s_mesh_workers, s_mesh_session_next = 1, s_mesh_job_next = 1;
static bool s_mesh_runtime_closing = true;

static void mesh_session_close_locked(esp32_mquickjs_wifi_mesh_session_t *s, bool timeout)
{
    if (timeout && !s->status.closing) {
        s->status.timed_out = true;
        if (!s->status.error) { s->status.error = ESP_ERR_TIMEOUT; s->status.stage = "mesh-start-timeout"; }
    }
    s->status.closing = true;
    s->status.ready = false;
    s->deadline_us = 0;
    s->next_poll_us = 0;
}

esp_err_t esp32_mquickjs_wifi_mesh_open_runtime(void)
{
    portENTER_CRITICAL(&s_mesh_session_lock);
    bool valid = !s_mesh_active && !s_mesh_workers;
    if (valid) s_mesh_runtime_closing = false;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_mesh_session_create(const esp32_mquickjs_wifi_mesh_config_t *config,
    bool allow_ap_restart, uint32_t timeout_ms, esp32_mquickjs_wifi_mesh_session_t **output)
{
    if (!output || *output || !timeout_ms || timeout_ms > ESP32_MQUICKJS_MESH_MAX_WAIT_MS) return ESP_ERR_INVALID_ARG;
    esp_err_t error = esp32_mquickjs_wifi_mesh_validate_config(config);
    if (error) return error;
    portENTER_CRITICAL(&s_mesh_session_lock);
    error = s_mesh_runtime_closing ? ESP_ERR_INVALID_STATE :
        s_mesh_handles == ESP32_MQUICKJS_MESH_HANDLES || !s_mesh_session_next ? ESP_ERR_NO_MEM : ESP_OK;
    uint32_t identity = 0;
    if (!error) { identity = s_mesh_session_next++; ++s_mesh_handles; }
    portEXIT_CRITICAL(&s_mesh_session_lock);
    if (error) return error;
    esp32_mquickjs_wifi_mesh_session_t *s = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, sizeof(*s),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!s) {
        portENTER_CRITICAL(&s_mesh_session_lock); --s_mesh_handles; portEXIT_CRITICAL(&s_mesh_session_lock);
        return ESP_ERR_NO_MEM;
    }
    s->references = 1; s->config = *config; s->allow_ap_restart = allow_ap_restart; s->timeout_ms = timeout_ms;
    s->status.identity = identity; s->status.reserved_bytes = sizeof(*s);
    *output = s;
    return ESP_OK;
}

bool esp32_mquickjs_wifi_mesh_session_retain(esp32_mquickjs_wifi_mesh_session_t *s)
{
    if (!s) return false;
    portENTER_CRITICAL(&s_mesh_session_lock);
    bool valid = s->references && s->references != UINT32_MAX;
    if (valid) ++s->references;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    return valid;
}

void esp32_mquickjs_wifi_mesh_session_release(esp32_mquickjs_wifi_mesh_session_t *s)
{
    if (!s) return;
    portENTER_CRITICAL(&s_mesh_session_lock);
    bool destroy = --s->references == 0;
    if (s->references == 1 && s_mesh_active == s) mesh_session_close_locked(s, false);
    portEXIT_CRITICAL(&s_mesh_session_lock);
    if (!destroy) return;
    esp32_mquickjs_wireless_secure_zero(s, sizeof(*s)); esp32_mquickjs_memory_payload_free(s);
    portENTER_CRITICAL(&s_mesh_session_lock); --s_mesh_handles; portEXIT_CRITICAL(&s_mesh_session_lock);
}

esp_err_t esp32_mquickjs_wifi_mesh_session_activate(esp32_mquickjs_wifi_mesh_session_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time(), duration = (int64_t)s->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_mesh_session_lock);
    bool valid = !s_mesh_runtime_closing && !s_mesh_active && !s->status.activated &&
        !s->status.closing && s->references && s->references != UINT32_MAX;
    if (valid) { ++s->references; s_mesh_active = s; s->status.activated = true; s->deadline_us = now + duration; }
    portEXIT_CRITICAL(&s_mesh_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_mesh_session_close(esp32_mquickjs_wifi_mesh_session_t *s, bool timeout)
{
    if (!s) return;
    portENTER_CRITICAL(&s_mesh_session_lock); mesh_session_close_locked(s, timeout); portEXIT_CRITICAL(&s_mesh_session_lock);
}

esp_err_t esp32_mquickjs_wifi_mesh_session_recover(esp32_mquickjs_wifi_mesh_session_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_mesh_session_lock);
    bool valid = s_mesh_active == s && s->status.closing && s->status.cleanup_error &&
        !s->status.worker_busy && !s->status.recovery_pending && !s->status.native.restart_required &&
        s->status.recovery_attempts != UINT32_MAX;
    if (valid) { s->status.recovery_pending = true; ++s->status.recovery_attempts; s->next_poll_us = 0; }
    portEXIT_CRITICAL(&s_mesh_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_mesh_session_status(esp32_mquickjs_wifi_mesh_session_t *s,
    esp32_mquickjs_wifi_mesh_session_status_t *status)
{
    if (!s || !status) return;
    portENTER_CRITICAL(&s_mesh_session_lock); *status = s->status;
    esp32_mquickjs_wifi_mesh_radio_token_t token = s->token;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    /* A long SDK send must not hide a newly captured disconnect from status.
     * This is a try-lock native snapshot, not SDK polling on the JS task. */
    if (token.identity && esp32_mquickjs_wifi_radio_mesh_status(&token, &status->native) != ESP_OK) {
        status->native.native.native_snapshot_valid = false;
        status->native.ip_ready = false;
    }
    if (status->native.native.native_stop_seen || status->native.restart_required)
        status->ready = false;
}

esp_err_t esp32_mquickjs_wifi_mesh_job_create(esp32_mquickjs_wifi_mesh_session_t *s,
    const esp32_mquickjs_wifi_mesh_send_t *send, const esp32_mquickjs_wifi_mesh_control_t *control,
    uint32_t timeout_ms, esp32_mquickjs_wifi_mesh_job_t **output)
{
    if (!s || !output || *output || (!send == !control) || !timeout_ms || timeout_ms > ESP32_MQUICKJS_MESH_MAX_WAIT_MS)
        return ESP_ERR_INVALID_ARG;
    if (send && (!send->bytes || !send->length || send->length > MESH_MPS)) return ESP_ERR_INVALID_SIZE;
    if (control && ((unsigned)control->kind >= ESP32_MQUICKJS_MESH_CONTROL_COUNT ||
        control->capacity > ESP32_MQUICKJS_MESH_MAX_NODES || control->count > control->capacity ||
        (control->count && !control->addresses))) return ESP_ERR_INVALID_ARG;
    bool extended = control && control->kind >= ESP32_MQUICKJS_MESH_CONFIGURATION;
    if (extended != (control && control->detail)) return ESP_ERR_INVALID_ARG;
    size_t detail_bytes = extended ? sizeof(*control->detail) : 0;
    bool scan_record = control && control->kind == ESP32_MQUICKJS_MESH_SCAN_NEXT;
    if (scan_record) detail_bytes += sizeof(esp32_mquickjs_wifi_mesh_scan_record_t);
    size_t bytes = send ? send->length : detail_bytes + control->capacity * sizeof(mesh_addr_t);
    if (bytes > SIZE_MAX - sizeof(esp32_mquickjs_wifi_mesh_job_t)) return ESP_ERR_INVALID_SIZE;
    portENTER_CRITICAL(&s_mesh_session_lock);
    esp_err_t error = s_mesh_runtime_closing || !s->status.ready || s->status.closing ? ESP_ERR_INVALID_STATE :
        s_mesh_jobs == ESP32_MQUICKJS_MESH_JOBS || !s_mesh_job_next || s->references == UINT32_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    uint32_t identity = 0;
    if (!error) { ++s->references; ++s_mesh_jobs; identity = s_mesh_job_next++; }
    portEXIT_CRITICAL(&s_mesh_session_lock);
    if (error) return error;
    esp32_mquickjs_wifi_mesh_job_t *j = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, sizeof(*j) + bytes,
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!j) {
        portENTER_CRITICAL(&s_mesh_session_lock); --s_mesh_jobs; portEXIT_CRITICAL(&s_mesh_session_lock);
        esp32_mquickjs_wifi_mesh_session_release(s); return ESP_ERR_NO_MEM;
    }
    j->parent = s; j->references = 1; j->timeout_ms = timeout_ms; j->allocation = sizeof(*j) + bytes;
    j->status.identity = identity; j->status.bytes = send ? bytes : 0; j->is_send = send != NULL;
    if (send) { j->send = *send; memcpy(j->data, send->bytes, bytes); j->send.bytes = j->data; }
    else {
        j->control = *control;
        j->control.detail = extended ? (void *)j->data : NULL;
        if (extended) {
            *j->control.detail = *control->detail;
            j->control.detail->scan_record = scan_record ? (void *)(j->data + sizeof(*control->detail)) : NULL;
        }
        j->control.addresses = control->capacity ? (void *)(j->data + detail_bytes) : NULL;
        if (control->count) memcpy(j->control.addresses, control->addresses, control->count * sizeof(mesh_addr_t));
    }
    *output = j; return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_mesh_job_submit(esp32_mquickjs_wifi_mesh_job_t *j)
{
    if (!j) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time(), duration = (int64_t)j->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_mesh_session_lock);
    esp32_mquickjs_wifi_mesh_session_t *s = j->parent;
    bool valid = s_mesh_active == s && s->status.ready && !s->status.closing && !s->job &&
        !j->status.cancelled && !j->status.dispatched && !j->deadline_us && j->references != UINT32_MAX;
    if (valid) { ++j->references; s->job = j; j->deadline_us = now + duration; s->next_poll_us = 0; }
    portEXIT_CRITICAL(&s_mesh_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_mesh_job_cancel(esp32_mquickjs_wifi_mesh_job_t *j)
{
    if (!j) return;
    portENTER_CRITICAL(&s_mesh_session_lock); j->status.cancelled = true; portEXIT_CRITICAL(&s_mesh_session_lock);
}

void esp32_mquickjs_wifi_mesh_job_release(esp32_mquickjs_wifi_mesh_job_t *j)
{
    if (!j) return;
    portENTER_CRITICAL(&s_mesh_session_lock); bool destroy = --j->references == 0; portEXIT_CRITICAL(&s_mesh_session_lock);
    if (!destroy) return;
    esp32_mquickjs_wifi_mesh_session_t *parent = j->parent;
    esp32_mquickjs_wireless_secure_zero(j, j->allocation); esp32_mquickjs_memory_payload_free(j);
    portENTER_CRITICAL(&s_mesh_session_lock); --s_mesh_jobs; portEXIT_CRITICAL(&s_mesh_session_lock);
    esp32_mquickjs_wifi_mesh_session_release(parent);
}

void esp32_mquickjs_wifi_mesh_job_result(esp32_mquickjs_wifi_mesh_job_t *j,
    esp32_mquickjs_wifi_mesh_job_status_t *status, const mesh_addr_t **addresses)
{
    if (!j || !status) return;
    portENTER_CRITICAL(&s_mesh_session_lock);
    *status = j->status;
    if (addresses) *addresses = j->status.done && !j->is_send ? j->control.addresses : NULL;
    portEXIT_CRITICAL(&s_mesh_session_lock);
}

const esp32_mquickjs_wifi_mesh_control_data_t *esp32_mquickjs_wifi_mesh_job_detail(esp32_mquickjs_wifi_mesh_job_t *j)
{
    if (!j) return NULL;
    portENTER_CRITICAL(&s_mesh_session_lock);
    const esp32_mquickjs_wifi_mesh_control_data_t *result = j->status.done ? j->control.detail : NULL;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    return result;
}

esp_err_t esp32_mquickjs_wifi_mesh_scan_read_claim(esp32_mquickjs_wifi_mesh_session_t *s, uint32_t *identity)
{
    if (!s || !identity || *identity) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_mesh_session_lock);
    esp_err_t error = !s->status.ready || s->status.closing || s->read_identity[2] ? ESP_ERR_INVALID_STATE :
        !s_mesh_job_next ? ESP_ERR_NO_MEM : ESP_OK;
    if (!error) *identity = s->read_identity[2] = s_mesh_job_next++;
    portEXIT_CRITICAL(&s_mesh_session_lock); return error;
}
void esp32_mquickjs_wifi_mesh_scan_read_release(esp32_mquickjs_wifi_mesh_session_t *s, uint32_t identity)
{
    if (!s || !identity) return;
    portENTER_CRITICAL(&s_mesh_session_lock);
    if (s->read_identity[2] == identity) s->read_identity[2] = 0;
    portEXIT_CRITICAL(&s_mesh_session_lock);
}
esp_err_t esp32_mquickjs_wifi_mesh_scan_commit(esp32_mquickjs_wifi_mesh_session_t *s,
    uint32_t read_identity, uint32_t scan_identity, uint32_t sequence)
{
    if (!s || !read_identity) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_mesh_session_lock);
    bool valid = s->status.ready && !s->status.closing && s->read_identity[2] == read_identity;
    esp32_mquickjs_wifi_mesh_radio_token_t token = s->token;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    return valid ? esp32_mquickjs_wifi_radio_mesh_scan_commit(&token, scan_identity, sequence) : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_mesh_read_claim(esp32_mquickjs_wifi_mesh_session_t *s, bool to_ds, uint32_t *identity)
{
    if (!s || !identity || *identity) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_mesh_session_lock);
    esp_err_t error = !s->status.ready || s->status.closing || s->read_identity[to_ds] ? ESP_ERR_INVALID_STATE :
        !s_mesh_job_next ? ESP_ERR_NO_MEM : ESP_OK;
    if (!error) *identity = s->read_identity[to_ds] = s_mesh_job_next++;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    return error;
}
void esp32_mquickjs_wifi_mesh_read_release(esp32_mquickjs_wifi_mesh_session_t *s, bool to_ds, uint32_t identity)
{
    if (!s || !identity) return;
    portENTER_CRITICAL(&s_mesh_session_lock);
    if (s->read_identity[to_ds] == identity) s->read_identity[to_ds] = 0;
    portEXIT_CRITICAL(&s_mesh_session_lock);
}

static bool mesh_session_read_token(esp32_mquickjs_wifi_mesh_session_t *s, bool to_ds,
    uint32_t identity, esp32_mquickjs_wifi_mesh_radio_token_t *token)
{
    if (!s) return false;
    portENTER_CRITICAL(&s_mesh_session_lock);
    bool valid = s->status.ready && !s->status.closing && (!identity || s->read_identity[to_ds] == identity);
    if (valid) *token = s->token;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    return valid;
}
esp_err_t esp32_mquickjs_wifi_mesh_message_copy(esp32_mquickjs_wifi_mesh_session_t *s,
    bool to_ds, uint32_t identity, esp32_mquickjs_wifi_mesh_message_t *message, uint8_t *bytes, size_t capacity)
{
    esp32_mquickjs_wifi_mesh_radio_token_t token;
    return identity && mesh_session_read_token(s, to_ds, identity, &token) ?
        esp32_mquickjs_wifi_radio_mesh_message_copy(&token, to_ds, message, bytes, capacity) : ESP_ERR_INVALID_STATE;
}
esp_err_t esp32_mquickjs_wifi_mesh_message_commit(esp32_mquickjs_wifi_mesh_session_t *s,
    bool to_ds, uint32_t identity, uint32_t sequence)
{
    esp32_mquickjs_wifi_mesh_radio_token_t token;
    return identity && mesh_session_read_token(s, to_ds, identity, &token) ?
        esp32_mquickjs_wifi_radio_mesh_message_commit(&token, to_ds, sequence) : ESP_ERR_INVALID_STATE;
}
esp_err_t esp32_mquickjs_wifi_mesh_event_copy(esp32_mquickjs_wifi_mesh_session_t *s,
    esp32_mquickjs_wifi_mesh_notice_t *notice)
{
    esp32_mquickjs_wifi_mesh_radio_token_t token;
    return mesh_session_read_token(s, false, 0, &token) ? esp32_mquickjs_wifi_radio_mesh_event_copy(&token, notice) : ESP_ERR_INVALID_STATE;
}
esp_err_t esp32_mquickjs_wifi_mesh_event_commit(esp32_mquickjs_wifi_mesh_session_t *s, uint32_t sequence)
{
    esp32_mquickjs_wifi_mesh_radio_token_t token;
    return mesh_session_read_token(s, false, 0, &token) ? esp32_mquickjs_wifi_radio_mesh_event_commit(&token, sequence) : ESP_ERR_INVALID_STATE;
}

static void mesh_session_worker(void *opaque)
{
    esp32_mquickjs_wifi_mesh_session_t *s = opaque;
    portENTER_CRITICAL(&s_mesh_session_lock);
    bool first = !s->worker_started && !s->status.closing;
    s->worker_started = true;
    esp32_mquickjs_wifi_mesh_radio_token_t token = s->token;
    esp32_mquickjs_wifi_mesh_radio_status_t native = s->status.native;
    esp32_mquickjs_wifi_mesh_job_t *job = s->job;
    bool closing = s->status.closing;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    esp_err_t error = ESP_OK;
    if (first) {
        error = esp32_mquickjs_wifi_radio_mesh_begin(&s->config, s->allow_ap_restart, &token, &native);
    }
    /* Also scrub a Session cancelled before its first worker was dispatched. */
    esp32_mquickjs_wireless_secure_zero(&s->config, sizeof(s->config));
    if (job) {
        int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&s_mesh_session_lock);
        bool dispatch = !s->status.closing && !job->status.cancelled && now < job->deadline_us;
        if (dispatch) job->status.dispatched = true;
        portEXIT_CRITICAL(&s_mesh_session_lock);
        esp_err_t result = !dispatch ? now >= job->deadline_us ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE :
            job->is_send ? esp32_mquickjs_wifi_radio_mesh_send(&token, &job->send) :
                esp32_mquickjs_wifi_radio_mesh_control(&token, &job->control);
        int64_t completed = esp_timer_get_time();
        portENTER_CRITICAL(&s_mesh_session_lock);
        job->status.timed_out = now >= job->deadline_us || (dispatch && completed >= job->deadline_us);
        job->status.native_error = dispatch ? result : ESP_OK;
        job->status.error = job->status.timed_out ? ESP_ERR_TIMEOUT : result;
        job->status.returned = dispatch;
        job->status.completed_us = completed;
        job->status.count = job->is_send ? 0 : job->control.count;
        job->status.done = true;
        s->job = NULL;
        portEXIT_CRITICAL(&s_mesh_session_lock);
        esp32_mquickjs_wifi_mesh_job_release(job);
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mesh_session_lock);
    if (!s->status.closing && s->deadline_us && now >= s->deadline_us) mesh_session_close_locked(s, true);
    if (error) {
        if (!s->status.error) { s->status.error = error; s->status.stage = native.stage; }
        mesh_session_close_locked(s, false);
    }
    closing = s->status.closing;
    bool recover = s->status.recovery_pending;
    s->status.recovery_pending = false;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    esp_err_t received[2] = {ESP_OK, ESP_OK};
    if (!closing && token.identity) {
        /* Poll on the worker even without a waiting Future. Two native retained
         * slots bound ingress; polling never consumes a pending JS delivery. */
        esp_err_t polled = esp32_mquickjs_wifi_radio_mesh_poll(&token, &native);
        /* Getter/network failures invalidate observations. A captured native
         * STOP or an uncertain driver state also ends command admission. */
        if (native.restart_required || native.native.native_stop_seen || native.native.closing) {
            portENTER_CRITICAL(&s_mesh_session_lock);
            if (!s->status.error) {
                s->status.error = native.native.error ? native.native.error : polled ? polled : ESP_ERR_INVALID_STATE;
                s->status.stage = "mesh-native-stopped";
            }
            mesh_session_close_locked(s, false);
            portEXIT_CRITICAL(&s_mesh_session_lock);
        } else {
            received[0] = esp32_mquickjs_wifi_radio_mesh_receive(&token, false);
            if (native.native.native_snapshot_valid && native.native.root)
                received[1] = esp32_mquickjs_wifi_radio_mesh_receive(&token, true);
        }
    }
    now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mesh_session_lock);
    closing = s->status.closing;
    /* A new job may have arrived while this worker was polling. Seal and
     * retire that pending reference before detaching the active Session. */
    esp32_mquickjs_wifi_mesh_job_t *abandoned = closing ? s->job : NULL;
    if (abandoned) {
        s->job = NULL;
        abandoned->status.cancelled = abandoned->status.done = true;
        abandoned->status.error = ESP_ERR_INVALID_STATE;
        abandoned->status.completed_us = now;
    }
    portEXIT_CRITICAL(&s_mesh_session_lock);
    esp32_mquickjs_wifi_mesh_job_release(abandoned);
    if (closing) error = !token.identity ? ESP_OK : recover ?
        esp32_mquickjs_wifi_radio_mesh_recover(&token, &native) : esp32_mquickjs_wifi_radio_mesh_close(&token, &native);
    now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mesh_session_lock);
    s->token = token; s->status.native = native;
    s->status.ready = native.ready && !s->status.closing;
    if (s->status.ready) s->deadline_us = 0;
    s->status.receive_error[0] = received[0]; s->status.receive_error[1] = received[1];
    s->status.cleanup_error = closing ? error : ESP_OK;
    bool detach = s->status.closing && !token.identity;
    if (detach) { s->status.retired = true; s_mesh_active = NULL; }
    s->status.reserved_bytes = sizeof(*s) + (token.identity ? native.reserved_bytes : 0);
    s->status.worker_busy = false;
    s->next_poll_us = now <= INT64_MAX - 25000 ? now + 25000 : INT64_MAX;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    if (detach) esp32_mquickjs_wifi_mesh_session_release(s);
    esp32_mquickjs_wifi_mesh_session_release(s);
    portENTER_CRITICAL(&s_mesh_session_lock); --s_mesh_workers; portEXIT_CRITICAL(&s_mesh_session_lock);
}

bool esp32_mquickjs_wifi_mesh_service(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mesh_session_lock);
    esp32_mquickjs_wifi_mesh_session_t *s = s_mesh_active;
    if (s && !s->status.closing && s->deadline_us && now >= s->deadline_us) mesh_session_close_locked(s, true);
    bool run = s && !s->status.worker_busy && !s->status.retired &&
        !s->status.native.restart_required &&
        s->references != UINT32_MAX && now >= s->next_poll_us;
    if (run) { ++s->references; ++s_mesh_workers; s->status.worker_busy = true; }
    portEXIT_CRITICAL(&s_mesh_session_lock);
    if (!run) return false;
    if (esp32_mquickjs_submit_background_worker(mesh_session_worker, s)) return true;
    portENTER_CRITICAL(&s_mesh_session_lock);
    s->status.worker_busy = false;
    s->status.cleanup_error = ESP_ERR_NO_MEM;
    s->next_poll_us = now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_mesh_session_lock);
    esp32_mquickjs_wifi_mesh_session_release(s);
    portENTER_CRITICAL(&s_mesh_session_lock); --s_mesh_workers; portEXIT_CRITICAL(&s_mesh_session_lock);
    return false;
}

bool esp32_mquickjs_wifi_mesh_prepare_runtime_destroy(void)
{
    portENTER_CRITICAL(&s_mesh_session_lock);
    s_mesh_runtime_closing = true;
    if (s_mesh_active) mesh_session_close_locked(s_mesh_active, false);
    portEXIT_CRITICAL(&s_mesh_session_lock);
    (void)esp32_mquickjs_wifi_mesh_service();
    portENTER_CRITICAL(&s_mesh_session_lock); bool drained = !s_mesh_active && !s_mesh_workers; portEXIT_CRITICAL(&s_mesh_session_lock);
    return drained;
}
#endif
