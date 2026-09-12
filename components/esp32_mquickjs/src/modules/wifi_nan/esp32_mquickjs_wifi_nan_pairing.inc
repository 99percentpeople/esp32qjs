/* One live PASN lane, owned by the existing Session registry and worker.
 * Historical JS/Future handles retain only their own native snapshot. */
struct esp32_mquickjs_wifi_nan_pairing {
    esp32_mquickjs_wifi_nan_discovery_t *service;
    esp32_mquickjs_wifi_nan_pairing_status_t status;
    uint32_t references, timeout_ms, pin;
    int64_t deadline_us;
    esp32_mquickjs_wifi_nan_message_t *bootstrap_message;
    int64_t bootstrap_completed_us;
    bool receiving, bootstrap_message_started, bootstrap_message_finished, bootstrap_message_accept;
};

static bool nan_pairing_service_busy_locked(esp32_mquickjs_wifi_nan_session_t *s, esp32_mquickjs_wifi_nan_discovery_t *d)
{ return (s->pairing && s->pairing->service == d) || s->pairing_request.service == d; }

static esp_err_t nan_bootstrap_claim_locked(esp32_mquickjs_wifi_nan_pairing_t *p)
{
    esp32_mquickjs_wifi_nan_discovery_t *d = p->service;
    for (unsigned i = 0; i < d->bootstrap_peer_count; ++i)
        if (d->bootstrap_peers[i].service_id == p->status.peer_service_id &&
            !memcmp(d->bootstrap_peers[i].peer, p->status.peer, 6)) return ESP_ERR_INVALID_STATE;
    if (d->bootstrap_peer_count >= ESP32_MQUICKJS_NAN_BOOTSTRAP_PEERS) return ESP_ERR_NO_MEM;
    unsigned slot = d->bootstrap_peer_count++;
    d->bootstrap_peers[slot].service_id = p->status.peer_service_id;
    memcpy(d->bootstrap_peers[slot].peer, p->status.peer, 6);
    return ESP_OK;
}

static void nan_pairing_close_locked(esp32_mquickjs_wifi_nan_pairing_t *p, bool timeout)
{
    if (!p->status.closing && timeout) {
        p->status.timed_out = true;
        if (!p->status.error) p->status.error = ESP_ERR_TIMEOUT;
    }
    p->status.closing = true;
    p->status.ready = false;
    esp32_mquickjs_wireless_secure_zero(&p->pin, sizeof(p->pin));
    if (!p->status.activated) p->status.retired = true;
    if (p->service->parent) p->service->parent->next_retry_us = 0;
}

esp_err_t esp32_mquickjs_wifi_nan_pairing_create(esp32_mquickjs_wifi_nan_discovery_t *service,
    uint8_t peer_service_id, const uint8_t peer[6], bool initiator, uint32_t timeout_ms,
    esp32_mquickjs_wifi_nan_pairing_t **out)
{
    if (!service || !out || *out || !peer_service_id || !peer || (peer[0] & 1) ||
        !memcmp(peer, (uint8_t[6]){0}, 6) || !timeout_ms || timeout_ms > ESP32_MQUICKJS_NAN_MAX_STARTUP_MS)
        return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_t *s = service->parent;
    esp_err_t error = !s ? ESP_ERR_INVALID_STATE : s->status.usd ? ESP_ERR_NOT_SUPPORTED :
        s_nan_runtime_closing || !s->status.ready || s->status.closing || s->pairing ||
        !service->status.ready || service->status.closing || !service->status.pairing_enabled ||
        initiator == service->status.publish ? ESP_ERR_INVALID_STATE :
        s_nan_pairing_handles >= ESP32_MQUICKJS_NAN_PAIRING_HANDLES || !s_nan_pairing_next_identity ||
        service->references == UINT32_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    esp32_mquickjs_wifi_nan_pairing_status_t initial = {0};
    if (!error) {
        ++service->references; ++s_nan_pairing_handles;
        initial = (esp32_mquickjs_wifi_nan_pairing_status_t){.identity = s_nan_pairing_next_identity++,
            .service_identity = service->status.identity, .session_identity = s->status.identity,
            .service_id = service->status.native_id, .peer_service_id = peer_service_id,
            .initiator = initiator, .reserved_bytes = sizeof(esp32_mquickjs_wifi_nan_pairing_t)};
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (error) return error;
    esp32_mquickjs_wifi_nan_pairing_t *p = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*p),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!p) {
        esp32_mquickjs_wifi_nan_discovery_release(service);
        portENTER_CRITICAL(&s_nan_session_lock); --s_nan_pairing_handles; portEXIT_CRITICAL(&s_nan_session_lock);
        return ESP_ERR_NO_MEM;
    }
    p->service = service; p->references = 1; p->timeout_ms = timeout_ms;
    p->status = initial;
    memcpy(p->status.peer, peer, 6);
    *out = p;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_nan_pairing_select(esp32_mquickjs_wifi_nan_pairing_t *p, uint32_t credential_id)
{
    if (!p || !credential_id) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool valid = p->service->parent && p->service->pairing.pairing_verification && !p->status.activated && !p->status.closing;
    if (valid) { p->status.verification = true; p->status.credential_id = credential_id; }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_nan_pairing_activate(esp32_mquickjs_wifi_nan_pairing_t *p)
{
    if (!p) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time(), duration = (int64_t)p->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_t *s = p->service->parent;
    bool valid = s && !s_nan_runtime_closing && !s->pairing && (!s->pairing_request.service || p->status.incoming) && s->status.ready && !s->status.closing &&
        p->service->status.ready && !p->service->status.closing &&
        !p->status.activated && !p->status.closing && p->references && p->references != UINT32_MAX;
    if (valid && p->status.incoming && now >= p->deadline_us) valid = false;
    esp_err_t error = valid ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!error && p->status.bootstrap) error = nan_bootstrap_claim_locked(p);
    if (!error) {
        ++p->references; s->pairing = p; p->status.activated = true;
        if (!p->status.incoming) p->deadline_us = now + duration;
        p->status.delivered = !p->status.incoming; s->next_retry_us = 0;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return error;
}

bool esp32_mquickjs_wifi_nan_pairing_retain(esp32_mquickjs_wifi_nan_pairing_t *p)
{
    if (!p) return false;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool retained = p->references && p->references != UINT32_MAX;
    if (retained) ++p->references;
    portEXIT_CRITICAL(&s_nan_session_lock);
    return retained;
}

void esp32_mquickjs_wifi_nan_pairing_release(void *opaque)
{
    esp32_mquickjs_wifi_nan_pairing_t *p = opaque;
    if (!p) return;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool destroy = --p->references == 0;
    if (p->references == 1 && p->service->parent && p->service->parent->pairing == p &&
        (!p->status.incoming || p->status.delivered)) nan_pairing_close_locked(p, false);
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (!destroy) return;
    esp32_mquickjs_wifi_nan_discovery_t *service = p->service;
    esp32_mquickjs_wireless_secure_zero(p, sizeof(*p));
    esp32_mquickjs_memory_payload_free(p);
    esp32_mquickjs_wifi_nan_discovery_release(service);
    portENTER_CRITICAL(&s_nan_session_lock); --s_nan_pairing_handles; portEXIT_CRITICAL(&s_nan_session_lock);
}

esp_err_t esp32_mquickjs_wifi_nan_pairing_confirm(esp32_mquickjs_wifi_nan_pairing_t *p,
    bool accept, uint32_t pin)
{
    if (!p || pin > 999999 || (!accept && pin)) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_t *s = p->service->parent;
    if (!p->status.closing && p->deadline_us && now >= p->deadline_us) nan_pairing_close_locked(p, true);
    bool valid = s && !s_nan_runtime_closing && s->pairing == p && s->status.ready && !s->status.closing &&
        p->service->status.ready && !p->service->status.closing && !p->status.closing && !p->status.confirmed &&
        p->status.delivered && (!p->status.verification || !pin);
    if (valid) {
        p->status.confirmed = true; p->status.rejected = !accept;
        if (accept) p->pin = pin;
        else nan_pairing_close_locked(p, false);
        s->next_retry_us = 0;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_nan_pairing_close(esp32_mquickjs_wifi_nan_pairing_t *p, bool timeout)
{
    if (!p) return;
    portENTER_CRITICAL(&s_nan_session_lock); nan_pairing_close_locked(p, timeout); portEXIT_CRITICAL(&s_nan_session_lock);
}

void esp32_mquickjs_wifi_nan_pairing_status(esp32_mquickjs_wifi_nan_pairing_t *p,
    esp32_mquickjs_wifi_nan_pairing_status_t *status)
{
    portENTER_CRITICAL(&s_nan_session_lock);
    *status = p->status;
    if ((p->service->status.closing || !p->service->parent || p->service->parent->status.closing) && !status->retired) {
        status->closing = true; status->ready = false;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
}

#include "esp32_mquickjs_wifi_nan_bootstrap.inc"

static void nan_pairing_worker(esp32_mquickjs_wifi_nan_session_t *s, bool stopped)
{
    nan_bootstrap_adopt(s, stopped);
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_pairing_t *p = s->pairing;
    if (!p) { portEXIT_CRITICAL(&s_nan_session_lock); return; }
    int64_t now = esp_timer_get_time();
    if (stopped || s->status.closing || p->service->status.closing) nan_pairing_close_locked(p, false);
    if (!p->status.start_attempted && !p->status.ready && !p->status.closing && p->deadline_us && now >= p->deadline_us)
        nan_pairing_close_locked(p, true);
    bool closing = p->status.closing;
    portEXIT_CRITICAL(&s_nan_session_lock);
    bool transport_retired = nan_bootstrap_progress(s, p, stopped);
    portENTER_CRITICAL(&s_nan_session_lock);
    closing = p->status.closing;
    bool bootstrap_ready = !p->status.bootstrap ||
        (p->status.bootstrap_sent && !p->status.bootstrap_tx_pending);
    bool start = !closing && p->status.confirmed && !p->status.start_attempted &&
        (!p->status.bootstrap || p->status.incoming || (p->status.peer_accepted && bootstrap_ready));
    esp32_mquickjs_wifi_nan_pairing_config_t config = {.service_identity = p->status.service_identity,
        .service_id = p->status.service_id, .peer_service_id = p->status.peer_service_id,
        .initiator = p->status.initiator, .verification = p->status.verification,
        .credential_id = p->status.verification ? p->status.credential_id : 0, .pincode = start ? p->pin : 0};
    memcpy(config.peer, p->status.peer, 6);
    if (start) { p->status.start_attempted = true; esp32_mquickjs_wireless_secure_zero(&p->pin, sizeof(p->pin)); }
    uint32_t committed = p->status.ready ? p->status.credential_id : 0;
    esp32_mquickjs_wifi_nan_pasn_status_t native = p->status.native;
    esp32_mquickjs_wifi_radio_operation_t token = s->operation;
    portEXIT_CRITICAL(&s_nan_session_lock);
    esp_err_t error = ESP_OK;
    bool retired = stopped || (closing && !native.identity);
    if (start) error = esp32_mquickjs_wifi_radio_nan_pairing_start(&token, &config, &native);
    else if (!stopped && native.identity)
        error = esp32_mquickjs_wifi_radio_nan_pairing_poll(&token, native.identity, closing, &native);
    esp32_mquickjs_wireless_secure_zero(&config, sizeof(config));
    if (closing && !error && native.identity) retired = true;
    now = esp_timer_get_time();
    int64_t completed_us = native.completed_us > p->bootstrap_completed_us ? native.completed_us : p->bootstrap_completed_us;
    /* Preserve the protocol/TX completion timestamp as the timeout authority.
     * An expired completion must not publish a reusable credential. */
    if (!closing && !error && !native.error && native.paired && !native.traffic_pending && bootstrap_ready && !committed &&
        (!p->deadline_us || completed_us < p->deadline_us))
        error = esp32_mquickjs_wifi_radio_nan_pairing_commit(&token, native.identity, &committed);
    portENTER_CRITICAL(&s_nan_session_lock);
    p->status.native = native;
    if (closing) p->status.cleanup_error = error;
    else {
        if (!error) error = native.error;
        if (error) { if (!p->status.error) p->status.error = error; nan_pairing_close_locked(p, false); }
        bool completed = native.paired && !native.traffic_pending && bootstrap_ready;
        if (!p->status.closing && p->deadline_us &&
            (completed ? completed_us >= p->deadline_us : now >= p->deadline_us)) nan_pairing_close_locked(p, true);
        p->status.ready = completed && committed && !p->status.error && !p->status.closing;
        if (p->status.ready) p->status.credential_id = committed;
        if (p->status.ready) p->deadline_us = 0;
    }
    retired = retired && transport_retired;
    if (retired) {
        p->status.retired = true; p->status.cleanup_error = ESP_OK;
        p->status.native.active = p->status.native.traffic_pending = false; p->status.native.retired = true;
        s->pairing = NULL;
        if (p->status.incoming) p->service->status.pairing_request_pending = false;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (retired) esp32_mquickjs_wifi_nan_pairing_release(p); /* Registry. */
}
