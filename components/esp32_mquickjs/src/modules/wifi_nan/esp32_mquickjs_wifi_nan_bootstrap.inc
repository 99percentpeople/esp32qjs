/* NPBA is unauthenticated coordination. It cannot set consent, complete PASN,
 * or release any callback/TX owner. All state uses the existing Session lock. */
static void nan_bootstrap_drop_locked(esp32_mquickjs_wifi_nan_discovery_t *d)
{ if (d->status.pairing_requests_dropped != UINT32_MAX) ++d->status.pairing_requests_dropped; }

esp_err_t esp32_mquickjs_wifi_nan_pairing_request(esp32_mquickjs_wifi_nan_pairing_t *p)
{
    if (!p) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool valid = p->status.initiator && !p->status.verification && !p->status.activated && !p->status.closing;
    if (valid) p->status.bootstrap = true;
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_nan_pairing_receive(esp32_mquickjs_wifi_nan_discovery_t *d,
    esp32_mquickjs_wifi_nan_pairing_t **out)
{
    if (!d || !out || *out) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_t *s = d->parent;
    bool valid = s && !s_nan_runtime_closing && !s->status.closing && d->status.publish &&
        d->status.ready && !d->status.closing && d->status.pairing_enabled;
    esp32_mquickjs_wifi_nan_pairing_t *p = valid ? s->pairing : NULL;
    if (p && !p->status.closing && p->deadline_us && esp_timer_get_time() >= p->deadline_us)
        nan_pairing_close_locked(p, true);
    if (p && p->service == d && p->status.incoming && !p->status.delivered && !p->receiving &&
        !p->status.closing && p->references != UINT32_MAX) {
        p->receiving = true; ++p->references; *out = p;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool esp32_mquickjs_wifi_nan_pairing_deliver(esp32_mquickjs_wifi_nan_pairing_t *p)
{
    if (!p) return false;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool valid = p->receiving && !p->status.delivered && !p->status.closing && !s_nan_runtime_closing &&
        p->service->parent && !p->service->parent->status.closing && !p->service->status.closing &&
        esp_timer_get_time() < p->deadline_us;
    if (valid) { p->receiving = false; p->status.delivered = true; p->service->status.pairing_request_pending = false; }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid;
}

void esp32_mquickjs_wifi_nan_pairing_unreserve(esp32_mquickjs_wifi_nan_pairing_t *p)
{
    if (!p) return;
    portENTER_CRITICAL(&s_nan_session_lock); p->receiving = false; portEXIT_CRITICAL(&s_nan_session_lock);
}

static void nan_pairing_notice(esp32_mquickjs_wifi_nan_session_t *s,
    const esp32_mquickjs_wifi_nan_sdk_notice_t *notice)
{
    if (!notice->data || notice->size != sizeof(esp32_mquickjs_wifi_nan_sdk_bootstrap_t) ||
        !notice->service_id || (notice->peer[0] & 1) || !memcmp(notice->peer, (uint8_t[6]){0}, 6)) return;
    const esp32_mquickjs_wifi_nan_sdk_bootstrap_t *data = notice->data;
    if (!data->peer_service_id) return;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_nan_session_lock);
    if (!s->status.ready || s->status.closing || s_nan_runtime_closing) goto done;
    esp32_mquickjs_wifi_nan_discovery_t *d = NULL;
    for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i)
        if (s->services[i] && s->services[i]->status.native_id == notice->service_id) { d = s->services[i]; break; }
    if (!d || !d->status.ready || d->status.closing || !d->status.pairing_enabled) goto done;
    if (data->type == ESP32_MQUICKJS_NAN_BOOTSTRAP_REQUEST && d->status.publish) {
        if (data->methods != WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD || data->status != ESP32_MQUICKJS_NAN_BOOTSTRAP_ACCEPTED ||
            s->pairing || s->pairing_request.service || d->references == UINT32_MAX ||
            now < 0 || now > INT64_MAX - (int64_t)ESP32_MQUICKJS_NAN_PAIRING_CONFIRM_MS * 1000) {
            nan_bootstrap_drop_locked(d); goto done;
        }
        for (unsigned i = 0; i < d->bootstrap_peer_count; ++i)
            if (d->bootstrap_peers[i].service_id == data->peer_service_id &&
                !memcmp(d->bootstrap_peers[i].peer, notice->peer, 6)) { nan_bootstrap_drop_locked(d); goto done; }
        if (d->bootstrap_peer_count == ESP32_MQUICKJS_NAN_BOOTSTRAP_PEERS) { nan_bootstrap_drop_locked(d); goto done; }
        ++d->references; s->pairing_request.service = d;
        s->pairing_request.peer_service_id = data->peer_service_id;
        memcpy(s->pairing_request.peer, notice->peer, 6);
        s->pairing_request.deadline_us = now + (int64_t)ESP32_MQUICKJS_NAN_PAIRING_CONFIRM_MS * 1000;
        d->status.pairing_request_pending = true; s->next_retry_us = 0;
    } else if (data->type == ESP32_MQUICKJS_NAN_BOOTSTRAP_RESPONSE && !d->status.publish) {
        esp32_mquickjs_wifi_nan_pairing_t *p = s->pairing;
        if (!p || p->service != d || !p->status.bootstrap || p->status.incoming || p->status.closing ||
            !p->status.bootstrap_attempted || p->status.peer_responded ||
            p->status.peer_service_id != data->peer_service_id || memcmp(p->status.peer, notice->peer, 6)) goto done;
        if (now >= p->deadline_us) { nan_pairing_close_locked(p, true); goto done; }
        p->status.peer_responded = true;
        p->status.peer_accepted = data->status == ESP32_MQUICKJS_NAN_BOOTSTRAP_ACCEPTED &&
            data->methods == WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY;
        if (!p->status.peer_accepted) {
            p->status.error = data->status == ESP32_MQUICKJS_NAN_BOOTSTRAP_COMEBACK ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_RESPONSE;
            nan_pairing_close_locked(p, false);
        }
        s->next_retry_us = 0;
    }
done:
    portEXIT_CRITICAL(&s_nan_session_lock);
}

static void nan_bootstrap_adopt(esp32_mquickjs_wifi_nan_session_t *s, bool stopped)
{
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_discovery_t *d = s->pairing_request.service;
    if (!d) { portEXIT_CRITICAL(&s_nan_session_lock); return; }
    int64_t deadline = s->pairing_request.deadline_us;
    bool drop = stopped || s->status.closing || d->status.closing || esp_timer_get_time() >= deadline;
    if (!drop && s->pairing) { portEXIT_CRITICAL(&s_nan_session_lock); return; }
    uint8_t peer_id = s->pairing_request.peer_service_id, peer[6];
    memcpy(peer, s->pairing_request.peer, 6);
    portEXIT_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_pairing_t *p = NULL;
    esp_err_t error = drop ? ESP_ERR_INVALID_STATE : esp32_mquickjs_wifi_nan_pairing_create(d,
        peer_id, peer, false, ESP32_MQUICKJS_NAN_PAIRING_CONFIRM_MS, &p);
    if (!error) {
        p->status.bootstrap = p->status.incoming = true; p->deadline_us = deadline;
        error = esp32_mquickjs_wifi_nan_pairing_activate(p);
    }
    portENTER_CRITICAL(&s_nan_session_lock);
    memset(&s->pairing_request, 0, sizeof(s->pairing_request));
    if (error) { d->status.pairing_request_pending = false; nan_bootstrap_drop_locked(d); }
    portEXIT_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_pairing_release(p);
    esp32_mquickjs_wifi_nan_discovery_release(d); /* Incoming capture reference. */
}

static bool nan_pairing_message_started_locked(esp32_mquickjs_wifi_nan_session_t *s,
    esp32_mquickjs_wifi_nan_message_t *message)
{
    esp32_mquickjs_wifi_nan_pairing_t *p = s->pairing;
    if (!p || p->bootstrap_message != message) return false;
    if (!p->status.closing && p->deadline_us && esp_timer_get_time() >= p->deadline_us) nan_pairing_close_locked(p, true);
    if (p->status.closing && (!p->status.incoming || p->bootstrap_message_accept)) return false;
    p->status.bootstrap_attempted = true;
    return true;
}

static bool nan_bootstrap_progress(esp32_mquickjs_wifi_nan_session_t *s, esp32_mquickjs_wifi_nan_pairing_t *p, bool stopped)
{
    if (!p->status.bootstrap) return true;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_message_t *message = p->bootstrap_message;
    bool closing = p->status.closing;
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (message) {
        if (closing && (!p->status.incoming || p->bootstrap_message_accept)) esp32_mquickjs_wifi_nan_message_cancel(message, false);
        esp32_mquickjs_wifi_nan_message_status_t status;
        esp32_mquickjs_wifi_nan_message_status(message, &status);
        portENTER_CRITICAL(&s_nan_session_lock);
        if (status.done && status.error && !p->status.closing) {
            if (!p->status.error) p->status.error = status.error;
            nan_pairing_close_locked(p, status.timed_out);
        }
        if (status.retired) {
            p->status.bootstrap_sent = status.tx.tx_done && status.tx.tx_succeeded && !status.error;
            p->bootstrap_completed_us = status.tx.retired_us > status.tx.completed_us ? status.tx.retired_us : status.tx.completed_us;
            p->status.bootstrap_tx_pending = false; p->bootstrap_message_finished = true; p->bootstrap_message = NULL;
        }
        portEXIT_CRITICAL(&s_nan_session_lock);
        if (status.retired) esp32_mquickjs_wifi_nan_message_release(message);
    }
    portENTER_CRITICAL(&s_nan_session_lock);
    bool skip = stopped || s->status.closing || p->service->status.closing ||
        (p->status.closing && !p->status.incoming);
    if (p->status.closing && !p->bootstrap_message_started && s->message) skip = true;
    bool send = !p->bootstrap_message_started && !skip &&
        (!p->status.incoming || p->status.closing || (p->status.confirmed && p->status.native.active && !p->status.native.error));
    bool available = !s->message;
    bool accept = !p->status.closing;
    if (skip && !p->bootstrap_message_started) p->bootstrap_message_finished = true;
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (send && available) {
        esp32_mquickjs_wifi_nan_message_t *created = NULL;
        esp_err_t error = esp32_mquickjs_wifi_nan_message_bootstrap_create(p->service, p->status.peer_service_id,
            p->status.peer, p->status.incoming, accept, 10000, &created);
        if (!error) error = esp32_mquickjs_wifi_nan_message_activate(created);
        portENTER_CRITICAL(&s_nan_session_lock);
        p->bootstrap_message_started = true; p->bootstrap_message_accept = accept;
        if (!error) { p->bootstrap_message = created; p->status.bootstrap_tx_pending = true; }
        else {
            p->bootstrap_message_finished = true;
            if (!p->status.error) p->status.error = error;
            nan_pairing_close_locked(p, false);
        }
        portEXIT_CRITICAL(&s_nan_session_lock);
        if (error) esp32_mquickjs_wifi_nan_message_release(created);
    }
    portENTER_CRITICAL(&s_nan_session_lock);
    bool retired = p->bootstrap_message_finished;
    portEXIT_CRITICAL(&s_nan_session_lock);
    return retired;
}
