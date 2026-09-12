/* The parent worker serializes SDK submission and service mutations. Public
 * Future ownership ends independently from registry/buffer retirement. */
struct esp32_mquickjs_wifi_nan_message {
    esp32_mquickjs_wifi_nan_session_t *parent;
    esp32_mquickjs_wifi_nan_discovery_t *service;
    wifi_nan_followup_params_t params;
    esp32_mquickjs_wifi_nan_message_status_t status;
    uint8_t ssi[ESP_WIFI_MAX_FUP_SSI_LEN];
    uint32_t references, timeout_ms, native_context;
    int64_t deadline_us;
    bool attempted, submit_returned, uncertain, usd;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    bool bootstrap, bootstrap_response, bootstrap_accept;
#endif
    size_t reserved_bytes;
    nan_vendor_ie_t vendor;
    uint8_t storage[];
};

esp_err_t esp32_mquickjs_wifi_nan_message_create(esp32_mquickjs_wifi_nan_discovery_t *service,
    const wifi_nan_followup_params_t *params, uint32_t timeout_ms, esp32_mquickjs_wifi_nan_message_t **out)
{
    if (!service || !params || !out || *out || !params->peer_inst_id || (params->peer_mac[0] & 1) ||
        !memcmp(params->peer_mac, (uint8_t[6]){0}, 6) || params->ssi_len > ESP_WIFI_MAX_FUP_SSI_LEN ||
        (params->ssi_len && !params->ssi) || !timeout_ms || timeout_ms > 120000) return ESP_ERR_INVALID_ARG;
    if (!esp32_mquickjs_wifi_nan_vendor_valid(params->vendor_ie)) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_t *parent = service->parent;
    esp_err_t error = parent && parent->status.usd && params->vendor_ie ? ESP_ERR_NOT_SUPPORTED :
        !parent || s_nan_runtime_closing || !service->status.ready || service->status.closing ||
        parent->status.closing ? ESP_ERR_INVALID_STATE : s_nan_message_handles >= ESP32_MQUICKJS_NAN_MESSAGE_HANDLES ||
        !s_nan_message_next_identity || service->references == UINT32_MAX || parent->references == UINT32_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    uint32_t identity = 0;
    if (!error) { ++service->references; ++parent->references; ++s_nan_message_handles; identity = s_nan_message_next_identity++; }
    uint8_t id = service->status.native_id;
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (error) return error;
    size_t reserved = sizeof(esp32_mquickjs_wifi_nan_message_t) + (params->vendor_ie ? params->vendor_ie->body_len : 0);
    esp32_mquickjs_wifi_nan_message_t *m = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, reserved,
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!m) {
        portENTER_CRITICAL(&s_nan_session_lock); --s_nan_message_handles; portEXIT_CRITICAL(&s_nan_session_lock);
        esp32_mquickjs_wifi_nan_discovery_release(service); esp32_mquickjs_wifi_nan_session_release(parent);
        return ESP_ERR_NO_MEM;
    }
    m->parent = parent; m->service = service; m->references = 1; m->timeout_ms = timeout_ms;
    m->usd = parent->status.usd;
    m->params = *params; m->params.inst_id = id;
    m->reserved_bytes = reserved;
    if (params->vendor_ie) {
        m->vendor = *params->vendor_ie; m->vendor.body = m->storage;
        if (m->vendor.body_len) memcpy(m->storage, params->vendor_ie->body, m->vendor.body_len);
        m->params.vendor_ie = &m->vendor;
    }
    if (params->ssi_len) memcpy(m->ssi, params->ssi, params->ssi_len);
    m->params.ssi = params->ssi_len ? m->ssi : NULL;
    m->status = (esp32_mquickjs_wifi_nan_message_status_t){.identity = identity, .service_identity = service->status.identity,
        .service_id = id, .peer_id = params->peer_inst_id, .bytes = params->ssi_len};
    memcpy(m->status.peer, params->peer_mac, 6);
    *out = m;
    return ESP_OK;
}

void esp32_mquickjs_wifi_nan_message_release(esp32_mquickjs_wifi_nan_message_t *m)
{
    if (!m) return;
    portENTER_CRITICAL(&s_nan_session_lock); bool destroy = --m->references == 0; portEXIT_CRITICAL(&s_nan_session_lock);
    if (!destroy) return;
    esp32_mquickjs_wifi_nan_session_t *parent = m->parent;
    esp32_mquickjs_wifi_nan_discovery_t *service = m->service;
    esp32_mquickjs_wireless_secure_zero(m, m->reserved_bytes); esp32_mquickjs_memory_payload_free(m);
    portENTER_CRITICAL(&s_nan_session_lock); --s_nan_message_handles; portEXIT_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_discovery_release(service); esp32_mquickjs_wifi_nan_session_release(parent);
}

#if CONFIG_ESP_WIFI_NAN_PAIRING
esp_err_t esp32_mquickjs_wifi_nan_message_bootstrap_create(esp32_mquickjs_wifi_nan_discovery_t *service,
    uint8_t peer_service_id, const uint8_t peer[6], bool response, bool accept, uint32_t timeout_ms,
    esp32_mquickjs_wifi_nan_message_t **out)
{
    if (!peer) return ESP_ERR_INVALID_ARG;
    wifi_nan_followup_params_t params = {.peer_inst_id = peer_service_id};
    memcpy(params.peer_mac, peer, 6);
    esp_err_t error = esp32_mquickjs_wifi_nan_message_create(service, &params, timeout_ms, out);
    if (!error) { (*out)->bootstrap = true; (*out)->bootstrap_response = response; (*out)->bootstrap_accept = accept; }
    return error;
}
#endif

esp_err_t esp32_mquickjs_wifi_nan_message_activate(esp32_mquickjs_wifi_nan_message_t *m)
{
    if (!m) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time(), duration = (int64_t)m->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_t *s = m->parent;
    bool valid = s && !s_nan_runtime_closing && !s->status.closing && !s->message &&
        m->service->parent == s && m->service->status.ready && !m->service->status.closing &&
        m->service->status.native_id == m->params.inst_id && !m->status.activated && !m->status.done;
    if (valid) {
        s->message = m; ++m->references; m->status.activated = true; m->deadline_us = now + duration;
        m->service->status.send_identity = m->status.identity;
        m->service->status.send_pending = true; m->service->status.send_cleanup_pending = false;
        s->next_retry_us = 0;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static void nan_message_cancel_locked(esp32_mquickjs_wifi_nan_message_t *m, bool timeout)
{
    if (!m->status.done) {
        m->status.cancelled = m->status.done = true;
        m->status.timed_out = timeout;
        m->status.error = timeout ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
    }
    if (!m->status.activated) m->status.retired = true;
    if (m->status.activated && !m->status.retired && m->service->status.send_identity == m->status.identity) {
        m->service->status.send_pending = false;
        m->service->status.send_cleanup_pending = true;
    }
    if (m->parent) m->parent->next_retry_us = 0;
}

void esp32_mquickjs_wifi_nan_message_cancel(esp32_mquickjs_wifi_nan_message_t *m, bool timeout)
{
    if (!m) return;
    portENTER_CRITICAL(&s_nan_session_lock); nan_message_cancel_locked(m, timeout); portEXIT_CRITICAL(&s_nan_session_lock);
}

static void nan_message_observe_locked(esp32_mquickjs_wifi_nan_message_t *m,
    const esp32_mquickjs_wifi_nan_message_tx_status_t *tx, int64_t now)
{
    if (tx) m->status.tx = *tx;
    if (!m->status.activated || m->status.done) return;
    if (m->submit_returned && m->status.tx.tx_done && m->status.tx.completed_us <= m->deadline_us) {
        m->status.done = true;
        m->status.error = m->status.tx.tx_succeeded ? ESP_OK : ESP_FAIL;
    } else if (now >= m->deadline_us) {
        nan_message_cancel_locked(m, true);
    } else if (m->submit_returned && m->status.tx.buffer_retired) {
        m->status.done = true; m->status.error = ESP_FAIL;
    }
    if (m->status.done && m->service->status.send_identity == m->status.identity) {
        m->service->status.send_pending = false;
        m->service->status.send_cleanup_pending = !m->status.retired;
        if (m->parent) m->parent->next_retry_us = 0;
    }
}

void esp32_mquickjs_wifi_nan_message_status(esp32_mquickjs_wifi_nan_message_t *m,
    esp32_mquickjs_wifi_nan_message_status_t *status)
{
    esp32_mquickjs_wifi_nan_message_tx_status_t tx;
    bool tracked = false;
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    if (m->usd) tracked = esp32_mquickjs_wifi_nan_usd_message_status(m->status.identity, &tx);
    else
#endif
    {
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
        tracked = esp32_mquickjs_wifi_nan_tx_message_status(m->status.identity, &tx);
#endif
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_nan_session_lock);
    /* Observe terminal state without waiting for the 100 ms cleanup worker.
     * A timely native completion remains timely when that worker was delayed. */
    nan_message_observe_locked(m, tracked ? &tx : NULL, now);
    *status = m->status;
    portEXIT_CRITICAL(&s_nan_session_lock);
}

static void nan_message_worker(esp32_mquickjs_wifi_nan_session_t *s, bool stopped)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_message_t *m = s->message;
    if (!m) { portEXIT_CRITICAL(&s_nan_session_lock); return; }
    if (s->status.closing || m->service->status.closing) nan_message_cancel_locked(m, false);
    if (!m->attempted && !m->status.done && now >= m->deadline_us) nan_message_cancel_locked(m, true);
    bool submit = !m->attempted && !m->status.done && !stopped;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (submit && m->bootstrap && !nan_pairing_message_started_locked(s, m)) {
        nan_message_cancel_locked(m, false); submit = false;
    }
#endif
    if (submit) m->attempted = m->status.submit_started = true;
    esp32_mquickjs_wifi_radio_operation_t operation = s->operation;
    portEXIT_CRITICAL(&s_nan_session_lock);
    esp_err_t error = ESP_OK;
    if (submit) {
#if CONFIG_ESP_WIFI_NAN_PAIRING
        if (m->bootstrap) error = esp32_mquickjs_wifi_radio_nan_bootstrap_send(&operation, &m->params,
            m->status.identity, &m->native_context, m->bootstrap_response, m->bootstrap_accept);
        else
#endif
            error = esp32_mquickjs_wifi_radio_nan_message_send(&operation, &m->params, m->status.identity, &m->native_context);
    }
    esp32_mquickjs_wifi_nan_message_tx_status_t tx = {0};
    bool tracked = false, released = true;
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    if (m->usd) {
        tracked = esp32_mquickjs_wifi_nan_usd_message_status(m->status.identity, &tx);
        released = !tracked || (tx.buffer_retired && !esp32_mquickjs_wifi_nan_usd_message_release(m->status.identity));
    } else
#endif
    {
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
        tracked = esp32_mquickjs_wifi_nan_tx_message_status(m->status.identity, &tx);
        released = !tracked || (tx.buffer_retired && !esp32_mquickjs_wifi_nan_tx_message_release(m->status.identity));
#endif
    }
    now = esp_timer_get_time();
    portENTER_CRITICAL(&s_nan_session_lock);
    if (tracked) m->status.tx = tx;
    if (submit) {
        m->submit_returned = true;
        m->status.submitted = !error;
        if (!error && (!tracked || !tx.entered || (!m->usd && !tx.allocated))) {
            error = ESP_ERR_INVALID_RESPONSE;
            m->uncertain = true;
            if (!s->status.error) { s->status.error = error; s->status.stage = "nan-message-binding"; }
            nan_session_close_locked(s, false);
        }
        if (error && !m->status.done) { m->status.error = error; m->status.done = true; }
    }
    nan_message_observe_locked(m, tracked ? &tx : NULL, now);
    bool retire = stopped || (!m->attempted && m->status.done) || (tracked && released && !m->uncertain) ||
        (submit && error && !tracked && !s->status.closing);
    m->service->status.send_pending = !m->status.done;
    m->service->status.send_cleanup_pending = m->status.done && !retire;
    if (retire) {
        if (!m->status.done) nan_message_cancel_locked(m, false);
        m->status.retired = true; m->status.tx.buffer_retired = true; m->parent = NULL; s->message = NULL;
        m->service->status.send_identity = 0;
        m->service->status.send_pending = m->service->status.send_cleanup_pending = false;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (retire) { esp32_mquickjs_wifi_nan_session_release(s); esp32_mquickjs_wifi_nan_message_release(m); }
}
