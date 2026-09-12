/* Shares the Session worker/lock; native operations retain their own exact
 * identities until the Radio release boundary proves physical retirement. */
struct esp32_mquickjs_wifi_nan_path {
    esp32_mquickjs_wifi_nan_session_t *parent;
    esp32_mquickjs_wifi_nan_discovery_t *service;
    esp32_mquickjs_wifi_nan_path_status_t status;
    wifi_nan_datapath_req_t request;
    uint32_t references, timeout_ms;
    int64_t deadline_us;
    uint16_t response_length;
    uint8_t response_ssi[ESP_WIFI_MAX_SVC_SSI_LEN];
    bool attempted, response_attempted, reject_sent, receiving;
};

static bool nan_path_enabled(const esp32_mquickjs_wifi_nan_discovery_t *d)
{ return d->status.publish ? d->config.publish.datapath_reqd : d->config.subscribe.datapath_reqd; }

static void nan_path_close_locked(esp32_mquickjs_wifi_nan_path_t *p, bool timeout)
{
    if (timeout && !p->status.closing) { p->status.timed_out = true; if (!p->status.error) p->status.error = ESP_ERR_TIMEOUT; }
    p->status.closing = true; p->status.ready = false;
    if (!p->status.activated) p->status.retired = true;
    if (p->parent) p->parent->next_retry_us = 0;
}

static esp_err_t nan_path_create(esp32_mquickjs_wifi_nan_discovery_t *service,
    const wifi_nan_datapath_req_t *request, uint32_t timeout_ms, bool outgoing,
    esp32_mquickjs_wifi_nan_path_t **out)
{
    if (!service || !request || !out || *out || !request->pub_id || !timeout_ms || timeout_ms > 120000 ||
        (request->peer_mac[0] & 1) || !memcmp(request->peer_mac, (uint8_t[6]){0}, 6)) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_t *parent = service->parent;
    esp_err_t error = !parent || s_nan_runtime_closing || !parent->status.ready || parent->status.closing ||
        !service->status.ready || service->status.closing || !nan_path_enabled(service) ||
        service->status.publish == outgoing ? ESP_ERR_INVALID_STATE :
        s_nan_path_handles >= ESP32_MQUICKJS_NAN_PATH_HANDLES || !s_nan_path_next_identity ||
        service->references == UINT32_MAX || parent->references == UINT32_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    uint32_t identity = 0, service_identity = service->status.identity, session_identity = parent ? parent->status.identity : 0;
    if (!error) { ++s_nan_path_handles; ++service->references; ++parent->references; identity = s_nan_path_next_identity++; }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (error) return error;
    esp32_mquickjs_wifi_nan_path_t *p = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*p),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!p) {
        portENTER_CRITICAL(&s_nan_session_lock); --s_nan_path_handles; portEXIT_CRITICAL(&s_nan_session_lock);
        esp32_mquickjs_wifi_nan_discovery_release(service); esp32_mquickjs_wifi_nan_session_release(parent);
        return ESP_ERR_NO_MEM;
    }
    p->parent = parent; p->service = service; p->request = *request; p->references = 1; p->timeout_ms = timeout_ms;
    p->status = (esp32_mquickjs_wifi_nan_path_status_t){.identity = identity, .service_identity = service_identity,
        .session_identity = session_identity, .outgoing = outgoing, .delivered = outgoing, .reserved_bytes = sizeof(*p)};
    p->status.native.publisher_id = request->pub_id;
    memcpy(p->status.native.peer, request->peer_mac, 6);
    *out = p; return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_nan_path_create(esp32_mquickjs_wifi_nan_discovery_t *service,
    const wifi_nan_datapath_req_t *request, uint32_t timeout_ms, esp32_mquickjs_wifi_nan_path_t **out)
{ return nan_path_create(service, request, timeout_ms, true, out); }

bool esp32_mquickjs_wifi_nan_path_retain(esp32_mquickjs_wifi_nan_path_t *p)
{
    if (!p) return false;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool valid = p->references && p->references != UINT32_MAX;
    if (valid) ++p->references;
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid;
}

void esp32_mquickjs_wifi_nan_path_release(esp32_mquickjs_wifi_nan_path_t *p)
{
    if (!p) return;
    portENTER_CRITICAL(&s_nan_session_lock); bool destroy = --p->references == 0; portEXIT_CRITICAL(&s_nan_session_lock);
    if (!destroy) return;
    esp32_mquickjs_wifi_nan_session_t *parent = p->parent;
    esp32_mquickjs_wifi_nan_discovery_t *service = p->service;
    esp32_mquickjs_wireless_secure_zero(p, sizeof(*p)); esp32_mquickjs_memory_payload_free(p);
    portENTER_CRITICAL(&s_nan_session_lock); --s_nan_path_handles; portEXIT_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_discovery_release(service); esp32_mquickjs_wifi_nan_session_release(parent);
}

esp_err_t esp32_mquickjs_wifi_nan_path_activate(esp32_mquickjs_wifi_nan_path_t *p)
{
    if (!p) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time(), duration = (int64_t)p->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_t *s = p->parent;
    bool valid = s && !s_nan_runtime_closing && s->status.ready && !s->status.closing &&
        !p->status.activated && !p->status.closing && p->service->parent == s &&
        p->service->status.ready && !p->service->status.closing && p->references != UINT32_MAX;
    /* Secure NDP requires the complete four-message handshake. Reject an
     * explicit opt-out before the first driver call instead of changing it. */
    if (p->status.outgoing && p->service->status.security_required && !p->request.confirm_required) valid = false;
    unsigned slot = ESP_WIFI_NAN_DATAPATH_MAX_PEERS;
    if (valid) for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        if (!s->paths[i]) slot = i;
        else if (!memcmp(s->paths[i]->request.peer_mac, p->request.peer_mac, 6)) valid = false;
    }
    valid = valid && slot < ESP_WIFI_NAN_DATAPATH_MAX_PEERS;
    if (valid) {
        ++p->references; s->paths[slot] = p; p->status.activated = true;
        p->deadline_us = now + duration;
        if (!p->status.outgoing && p->status.native.started_us > 0 && p->status.native.started_us <= now)
            p->deadline_us = p->status.native.started_us + duration;
        s->next_retry_us = 0;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_nan_path_close(esp32_mquickjs_wifi_nan_path_t *p, bool timeout)
{
    if (!p) return;
    portENTER_CRITICAL(&s_nan_session_lock); nan_path_close_locked(p, timeout); portEXIT_CRITICAL(&s_nan_session_lock);
}

static void nan_path_observe_locked(esp32_mquickjs_wifi_nan_path_t *p,
    const esp32_mquickjs_wifi_nan_ndp_status_t *native, int64_t now)
{
    if (p->status.retired) return;
    if (native) p->status.native = *native;
    if (p->parent->status.closing || p->service->status.closing) nan_path_close_locked(p, false);
    bool expected_reject = native && p->reject_sent && native->rejected && native->error == ESP_FAIL;
    if (native && native->error && !expected_reject && !p->status.error) p->status.error = native->error;
    if (p->status.error) nan_path_close_locked(p, false);
    if (native && (native->rejected || native->terminated || native->native_deleted)) {
        if (!p->status.closing && !p->status.error) p->status.error = ESP_FAIL;
        nan_path_close_locked(p, false);
    }
    if (!p->status.closing && native && native->accepted && native->completed_us &&
        (!p->deadline_us || native->completed_us <= p->deadline_us)) {
        p->status.ready = true; p->deadline_us = 0;
    }
    if (!p->status.closing && p->deadline_us && now >= p->deadline_us) nan_path_close_locked(p, true);
}

void esp32_mquickjs_wifi_nan_path_status(esp32_mquickjs_wifi_nan_path_t *p,
    esp32_mquickjs_wifi_nan_path_status_t *status)
{
    portENTER_CRITICAL(&s_nan_session_lock); uint32_t identity = p->status.native.identity; portEXIT_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_ndp_status_t native;
    bool found = esp32_mquickjs_wifi_nan_ndp_status(identity, &native);
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_nan_session_lock);
    nan_path_observe_locked(p, found ? &native : NULL, now); *status = p->status;
    portEXIT_CRITICAL(&s_nan_session_lock);
}

esp_err_t esp32_mquickjs_wifi_nan_path_respond(esp32_mquickjs_wifi_nan_path_t *p, bool accept,
    const uint8_t *ssi, uint16_t length)
{
    if (!p || length > ESP_WIFI_MAX_SVC_SSI_LEN || (length && !ssi)) return ESP_ERR_INVALID_ARG;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_nan_session_lock);
    bool valid = p->parent && !s_nan_runtime_closing && !p->parent->status.closing && !p->service->status.closing &&
        p->status.activated && p->status.delivered && !p->status.outgoing && !p->status.closing &&
        !p->status.response_requested && !p->status.native.response_attempted && !p->status.ready &&
        !p->status.error && p->deadline_us && now < p->deadline_us;
    if (valid) {
        p->status.response_requested = true; p->status.accept = accept;
        p->response_length = length; if (length) memcpy(p->response_ssi, ssi, length);
        if (!accept) nan_path_close_locked(p, false);
        p->parent->next_retry_us = 0;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_nan_path_receive(esp32_mquickjs_wifi_nan_discovery_t *service,
    esp32_mquickjs_wifi_nan_path_t **out)
{
    if (!service || !out || *out) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_t *s = service->parent;
    bool valid = s && !s_nan_runtime_closing && !s->status.closing && service->status.publish &&
        service->status.ready && !service->status.closing && nan_path_enabled(service);
    if (valid) for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        esp32_mquickjs_wifi_nan_path_t *p = s->paths[i];
        if (p && p->service == service && !p->status.outgoing && !p->status.delivered && !p->receiving &&
            !p->status.closing && p->references != UINT32_MAX) {
            p->receiving = true; ++p->references; *out = p; break;
        }
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool esp32_mquickjs_wifi_nan_path_deliver(esp32_mquickjs_wifi_nan_path_t *p)
{
    portENTER_CRITICAL(&s_nan_session_lock);
    bool valid = p->receiving && !p->status.delivered && !p->status.closing && !s_nan_runtime_closing &&
        p->parent && !p->parent->status.closing && !p->service->status.closing;
    if (valid) { p->receiving = false; p->status.delivered = true; }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid;
}

void esp32_mquickjs_wifi_nan_path_unreserve(esp32_mquickjs_wifi_nan_path_t *p)
{
    if (!p) return;
    portENTER_CRITICAL(&s_nan_session_lock); p->receiving = false; portEXIT_CRITICAL(&s_nan_session_lock);
}

static bool nan_path_pending_locked(esp32_mquickjs_wifi_nan_session_t *s)
{
    for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) if (s->paths[i]) return true;
    /* Native claim can precede application notification (including failed
     * handshakes). Poll the bounded ledger while any data service exists. */
    for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i)
        if (s->services[i] && nan_path_enabled(s->services[i])) return true;
    return false;
}

static void nan_path_parent_fault(esp32_mquickjs_wifi_nan_session_t *s, esp_err_t error)
{
    portENTER_CRITICAL(&s_nan_session_lock);
    if (!s->status.error) { s->status.error = error; s->status.stage = "nan-path-cleanup"; }
    nan_session_close_locked(s, false);
    portEXIT_CRITICAL(&s_nan_session_lock);
}

static void nan_path_adopt(esp32_mquickjs_wifi_nan_session_t *s)
{
    esp32_mquickjs_wifi_nan_ndp_status_t entries[ESP_WIFI_NAN_DATAPATH_MAX_PEERS];
    unsigned count = esp32_mquickjs_wifi_nan_ndp_list(entries);
    for (unsigned n = 0; n < count; ++n) {
        esp32_mquickjs_wifi_nan_ndp_status_t *native = &entries[n];
        portENTER_CRITICAL(&s_nan_session_lock);
        bool known = s->status.closing;
        for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i)
            if (s->paths[i] && s->paths[i]->status.native.identity == native->identity) known = true;
        esp32_mquickjs_wifi_nan_discovery_t *service = NULL;
        for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i)
            if (s->services[i] && s->services[i]->status.native_id == native->service_id) service = s->services[i];
        esp32_mquickjs_wifi_radio_operation_t token = s->operation;
        portEXIT_CRITICAL(&s_nan_session_lock);
        if (known) continue;
        esp_err_t error = ESP_OK;
        if (native->native_deleted) {
            error = esp32_mquickjs_wifi_radio_nan_ndp_release(&token, native->identity, native);
            if (error != ESP_OK && error != ESP_ERR_TIMEOUT) nan_path_parent_fault(s, error);
            continue;
        }
        if (native->outgoing) { nan_path_parent_fault(s, ESP_ERR_INVALID_STATE); continue; }
        if (!native->host_bound) {
            if (esp_timer_get_time() - native->started_us >= INT64_C(10000000)) nan_path_parent_fault(s, ESP_ERR_TIMEOUT);
            continue;
        }
        esp32_mquickjs_wifi_nan_path_t *p = NULL;
        if (!native->response_attempted && !native->error && service) {
            wifi_nan_datapath_req_t request = {.pub_id = native->publisher_id, .confirm_required = true};
            memcpy(request.peer_mac, native->peer, 6);
            error = nan_path_create(service, &request, 10000, false, &p);
            if (!error) {
                p->status.native = *native; p->attempted = true;
                (void)esp32_mquickjs_wifi_nan_ndp_ssi(native->identity, p->status.ssi, &p->status.ssi_len);
                error = esp32_mquickjs_wifi_nan_path_activate(p);
                esp32_mquickjs_wifi_nan_path_release(p); /* Construction; registry retains on success. */
                if (!error) continue;
            }
        }
        /* Capacity/OOM/closing service: decline without constructing JS or
         * placing a cleanup obligation in a droppable observation queue. */
        if (!native->response_attempted)
            error = esp32_mquickjs_wifi_radio_nan_ndp_response(&token, native->identity, false, NULL, 0);
        if (error || esp_timer_get_time() - native->started_us >= INT64_C(10000000))
            nan_path_parent_fault(s, error ? error : ESP_ERR_TIMEOUT);
    }
}

static void nan_path_detach(esp32_mquickjs_wifi_nan_session_t *s, esp32_mquickjs_wifi_nan_path_t *p, bool stopped)
{
    portENTER_CRITICAL(&s_nan_session_lock);
    for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) if (s->paths[i] == p) s->paths[i] = NULL;
    nan_path_close_locked(p, false); p->status.retired = true; p->status.cleanup_error = ESP_OK;
    if (stopped) { p->status.native.frames_retired = true; if (p->status.native.native_bound) p->status.native.native_deleted = true; }
    p->parent = NULL;
    portEXIT_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_release(s); esp32_mquickjs_wifi_nan_path_release(p);
}

static void nan_path_worker(esp32_mquickjs_wifi_nan_session_t *s, bool stopped)
{
    if (!stopped) nan_path_adopt(s);
    for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        portENTER_CRITICAL(&s_nan_session_lock);
        esp32_mquickjs_wifi_nan_path_t *p = s->paths[i];
        bool closing = s->status.closing;
        esp32_mquickjs_wifi_radio_operation_t token = s->operation;
        portEXIT_CRITICAL(&s_nan_session_lock);
        if (!p) continue;
        if (stopped) { nan_path_detach(s, p, true); continue; }
        if (closing) continue; /* Whole-Session STOP owns the remaining suffix. */
        esp32_mquickjs_wifi_nan_ndp_status_t native = {0};
        portENTER_CRITICAL(&s_nan_session_lock); uint32_t identity = p->status.native.identity; portEXIT_CRITICAL(&s_nan_session_lock);
        bool tracked = esp32_mquickjs_wifi_nan_ndp_status(identity, &native);
        int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&s_nan_session_lock);
        nan_path_observe_locked(p, tracked ? &native : NULL, now);
        bool submit = !p->attempted && !p->status.closing;
        if (submit) p->attempted = true;
        uint8_t service_id = p->service->status.native_id;
        portEXIT_CRITICAL(&s_nan_session_lock);
        esp_err_t error = ESP_OK;
        if (submit) {
            error = esp32_mquickjs_wifi_radio_nan_ndp_request(&token, service_id, &p->request, &identity);
            tracked = esp32_mquickjs_wifi_nan_ndp_status(identity, &native);
            portENTER_CRITICAL(&s_nan_session_lock);
            p->status.native.identity = identity;
            if (error && !p->status.error) p->status.error = error;
            if (!error && !tracked) p->status.error = ESP_ERR_INVALID_RESPONSE;
            nan_path_observe_locked(p, tracked ? &native : NULL, esp_timer_get_time());
            portEXIT_CRITICAL(&s_nan_session_lock);
        }
        portENTER_CRITICAL(&s_nan_session_lock);
        closing = p->status.closing;
        bool respond = tracked && !native.outgoing && !native.response_attempted && !native.native_deleted &&
            !p->response_attempted && (p->status.response_requested || closing);
        bool accept = respond && !closing && p->status.accept;
        if (respond) { p->response_attempted = true; p->status.accept = accept; }
        bool end = closing && tracked && native.native_bound && !native.native_deleted &&
            !native.end_attempted && !respond && !p->reject_sent;
        portEXIT_CRITICAL(&s_nan_session_lock);
        if (respond) {
            error = esp32_mquickjs_wifi_radio_nan_ndp_response(&token, identity, accept, p->response_ssi, p->response_length);
            portENTER_CRITICAL(&s_nan_session_lock);
            p->reject_sent = !accept && !error;
            if (error && !p->status.error) p->status.error = error;
            portEXIT_CRITICAL(&s_nan_session_lock);
        } else if (end) {
            error = esp32_mquickjs_wifi_radio_nan_ndp_end(&token, identity);
            portENTER_CRITICAL(&s_nan_session_lock); p->status.cleanup_error = error; portEXIT_CRITICAL(&s_nan_session_lock);
            if (error) nan_path_parent_fault(s, error);
        }
        tracked = esp32_mquickjs_wifi_nan_ndp_status(identity, &native);
        uint8_t ssi[ESP_WIFI_MAX_SVC_SSI_LEN]; uint16_t length = 0;
        bool copied = tracked && esp32_mquickjs_wifi_nan_ndp_ssi(identity, ssi, &length);
        portENTER_CRITICAL(&s_nan_session_lock);
        nan_path_observe_locked(p, tracked ? &native : NULL, esp_timer_get_time());
        if (copied) { p->status.ssi_len = length; if (length) memcpy(p->status.ssi, ssi, length); }
        closing = p->status.closing;
        bool retire = closing && !identity;
        portEXIT_CRITICAL(&s_nan_session_lock);
        if (closing && tracked && (native.native_deleted || (!native.native_bound && !native.submitted))) {
            error = esp32_mquickjs_wifi_radio_nan_ndp_release(&token, identity, &native);
            portENTER_CRITICAL(&s_nan_session_lock);
            p->status.cleanup_error = error;
            if (!error) p->status.native = native;
            portEXIT_CRITICAL(&s_nan_session_lock);
            retire = error == ESP_OK;
            if (error && (error != ESP_ERR_TIMEOUT || (!native.native_bound && native.ndp_id)))
                nan_path_parent_fault(s, error);
        }
        if (retire) nan_path_detach(s, p, false);
    }
}
