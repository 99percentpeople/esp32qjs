/* Native discovery owners share the parent Session lock and single worker.
 * SDK observers retain only native records/queues; no JS values or roots. */
struct esp32_mquickjs_wifi_nan_discovery {
    esp32_mquickjs_wifi_nan_session_t *parent;
    esp32_mquickjs_event_queue_t *queue;
    esp32_mquickjs_wifi_nan_service_config_t config;
    esp32_mquickjs_wifi_nan_discovery_status_t status;
    esp32_mquickjs_wifi_nan_discovery_event_t scratch;
    uint8_t ssi[ESP_WIFI_MAX_SVC_SSI_LEN];
    uint32_t references, timeout_ms, sequence;
    int64_t deadline_us;
    bool event_busy;
    nan_vendor_ie_t vendor;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    wifi_nan_pairing_cfg_t pairing;
    esp32_mquickjs_wifi_nan_credentials_t credentials;
    uint32_t credentials_revision;
    esp_err_t credentials_error;
    bool credentials_requested;
    /* NPBA carries no operation cookie. Keep used peer tuples until native
     * service cancellation, so a later local exchange cannot reuse a tuple. */
    struct { uint8_t service_id, peer[6]; } bootstrap_peers[ESP32_MQUICKJS_NAN_BOOTSTRAP_PEERS];
    unsigned bootstrap_peer_count;
#endif
    uint8_t storage[];
};

#if CONFIG_ESP_WIFI_NAN_PAIRING
#include "esp32_mquickjs_wifi_nan_credentials.inc"
#endif

static void nan_discovery_clear_secrets(esp32_mquickjs_wifi_nan_discovery_t *service)
{
    wifi_nan_discovery_security_params_t *security = service->status.publish ?
        service->config.publish.security_cfg : service->config.subscribe.security_cfg;
    if (security) esp32_mquickjs_wireless_secure_zero(security, sizeof(*security));
}

esp_err_t esp32_mquickjs_wifi_nan_discovery_create(esp32_mquickjs_wifi_nan_session_t *parent,
    bool publish, const esp32_mquickjs_wifi_nan_service_config_t *config, uint32_t timeout_ms,
    esp32_mquickjs_wifi_nan_discovery_t **out)
{
    if (!parent || !config || !out || *out || !timeout_ms || timeout_ms > ESP32_MQUICKJS_NAN_MAX_STARTUP_MS)
        return ESP_ERR_INVALID_ARG;
    const char *name = publish ? config->publish.service_name : config->subscribe.service_name;
    const char *filter = publish ? config->publish.matching_filter : config->subscribe.matching_filter;
    const uint8_t *ssi = publish ? config->publish.ssi : config->subscribe.ssi;
    uint16_t size = publish ? config->publish.ssi_len : config->subscribe.ssi_len;
    const wifi_nan_discovery_security_params_t *security = publish ? config->publish.security_cfg : config->subscribe.security_cfg;
    const nan_vendor_ie_t *vendor = publish ? config->publish.vendor_ie : config->subscribe.vendor_ie;
    bool secured = publish ? config->publish.security_reqd : config->subscribe.security_reqd;
    if (!name[0] || !memchr(name, 0, ESP_WIFI_MAX_SVC_NAME_LEN) ||
        !memchr(filter, 0, ESP_WIFI_MAX_FILTER_LEN) || size > ESP_WIFI_MAX_SVC_SSI_LEN || (size && !ssi))
        return ESP_ERR_INVALID_ARG;
    if (secured != (security != NULL) || !esp32_mquickjs_wifi_nan_vendor_valid(vendor)) return ESP_ERR_INVALID_ARG;
    esp_err_t validation = esp32_mquickjs_wifi_nan_security_validate(security);
    if (validation) return validation;
    const wifi_nan_pairing_cfg_t *pairing = publish ? config->publish.pairing : config->subscribe.pairing;
    if (pairing) {
#if CONFIG_ESP_WIFI_NAN_PAIRING
        if (!pairing->pairing_setup || !pairing->npk_nik_caching || pairing->reserved ||
            pairing->bootstrapping_methods != (publish ? WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY : WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD) ||
            pairing->comeback_delay) return ESP_ERR_INVALID_ARG;
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }
    if (pairing && security && security->num_credentials >= ESP_WIFI_NAN_MAX_CREDS_PER_SVC)
        return ESP_ERR_INVALID_ARG;
    bool usd = publish ? config->publish.usd_discovery_flag : config->subscribe.usd_discovery_flag;
    if (usd && (pairing || filter[0] || security || vendor || (publish ? config->publish.datapath_reqd ||
            config->publish.single_replied_event : config->subscribe.datapath_reqd || config->subscribe.single_match_event)))
        return ESP_ERR_NOT_SUPPORTED;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp_err_t error = usd != parent->status.usd ? ESP_ERR_INVALID_ARG :
        security && security->group_mgmt_prot && !parent->status.group_management_protection ? ESP_ERR_INVALID_ARG :
        s_nan_runtime_closing || !parent->status.ready || parent->status.closing ? ESP_ERR_INVALID_STATE :
        s_nan_discovery_handles >= ESP32_MQUICKJS_NAN_SERVICE_HANDLES || !s_nan_discovery_next_identity ||
        parent->references == UINT32_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    uint32_t identity = 0;
    if (!error) { ++s_nan_discovery_handles; ++parent->references; identity = s_nan_discovery_next_identity++; }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (error) return error;
    size_t reserved = sizeof(esp32_mquickjs_wifi_nan_discovery_t) +
        ((security || pairing) ? sizeof(*security) : 0) + (vendor ? vendor->body_len : 0);
    esp32_mquickjs_wifi_nan_discovery_t *service = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, reserved,
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!service) {
        portENTER_CRITICAL(&s_nan_session_lock); --s_nan_discovery_handles; portEXIT_CRITICAL(&s_nan_session_lock);
        esp32_mquickjs_wifi_nan_session_release(parent);
        return ESP_ERR_NO_MEM;
    }
    service->parent = parent;
    service->references = 1;
    service->timeout_ms = timeout_ms;
    service->config = *config;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (pairing) service->pairing = *pairing;
    if (publish) service->config.publish.pairing = pairing ? &service->pairing : NULL;
    else service->config.subscribe.pairing = pairing ? &service->pairing : NULL;
#endif
    uint8_t *storage = service->storage;
    wifi_nan_discovery_security_params_t *captured_security = NULL;
    if (security || pairing) {
        captured_security = (wifi_nan_discovery_security_params_t *)storage;
        if (security) memcpy(captured_security, security, sizeof(*security));
#if CONFIG_ESP_WIFI_NAN_PAIRING
        if (pairing) {
            captured_security->creds[captured_security->num_credentials++].csid = WIFI_NAN_CSID_NCS_PK_PASN_128;
            secured = true;
        }
#endif
        /* This SDK policy is Session-wide. Normalize explicitly so the host
         * and driver receive identical advertised group capabilities. */
        captured_security->group_mgmt_prot = parent->status.group_management_protection;
        if (parent->status.group_management_protection) captured_security->group_data_prot = true;
        storage += sizeof(*security);
    }
    if (vendor) {
        service->vendor = *vendor;
        service->vendor.body = storage;
        if (vendor->body_len) memcpy(storage, vendor->body, vendor->body_len);
    }
    if (publish) { service->config.publish.security_reqd = secured; service->config.publish.security_cfg = captured_security; service->config.publish.vendor_ie = vendor ? &service->vendor : NULL; }
    else { service->config.subscribe.security_reqd = secured; service->config.subscribe.security_cfg = captured_security; service->config.subscribe.vendor_ie = vendor ? &service->vendor : NULL; }
    if (size) memcpy(service->ssi, ssi, size);
    if (publish) { service->config.publish.ssi = size ? service->ssi : NULL; service->config.publish.ndp_resp_needed = true; }
    else service->config.subscribe.ssi = size ? service->ssi : NULL;
    service->status = (esp32_mquickjs_wifi_nan_discovery_status_t){.identity = identity,
        .session_identity = parent->status.identity, .publish = publish, .reserved_bytes = reserved,
        .pairing_enabled = pairing != NULL, .security_required = secured, .credential_count = security ? security->num_credentials : 0,
        .group_data_protection = captured_security && captured_security->group_data_prot,
        .group_management_protection = captured_security && captured_security->group_mgmt_prot,
        .vendor_bytes = vendor ? vendor->body_len : 0};
    *out = service;
    return ESP_OK;
}

bool esp32_mquickjs_wifi_nan_discovery_retain(esp32_mquickjs_wifi_nan_discovery_t *service)
{
    if (!service) return false;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool valid = service->references && service->references != UINT32_MAX;
    if (valid) ++service->references;
    portEXIT_CRITICAL(&s_nan_session_lock);
    return valid;
}

void esp32_mquickjs_wifi_nan_discovery_release(void *opaque)
{
    esp32_mquickjs_wifi_nan_discovery_t *service = opaque;
    if (!service) return;
    portENTER_CRITICAL(&s_nan_session_lock);
    bool destroy = --service->references == 0;
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (!destroy) return;
    esp32_mquickjs_wifi_nan_session_t *parent = service->parent;
    esp32_mquickjs_wireless_secure_zero(service, service->status.reserved_bytes);
    esp32_mquickjs_memory_payload_free(service);
    portENTER_CRITICAL(&s_nan_session_lock); --s_nan_discovery_handles; portEXIT_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_session_release(parent);
}

esp_err_t esp32_mquickjs_wifi_nan_discovery_activate(esp32_mquickjs_wifi_nan_discovery_t *service,
    esp32_mquickjs_event_queue_t *queue)
{
    if (!service || !queue) return ESP_ERR_INVALID_ARG;
    if (!service->parent) return ESP_ERR_INVALID_STATE;
    int64_t now = esp_timer_get_time(), duration = (int64_t)service->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration || esp32_mquickjs_event_queue_is_closed(queue) ||
        !esp32_mquickjs_event_queue_retain(queue)) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_nan_session_t *parent = service->parent;
    portENTER_CRITICAL(&s_nan_session_lock);
    unsigned slot = ESP_WIFI_NAN_MAX_SVC_SUPPORTED;
    bool valid = !s_nan_runtime_closing && parent->status.ready && !parent->status.closing &&
        !service->status.activated && !service->status.closing && service->references != UINT32_MAX;
    for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i) {
        if (!parent->services[i]) slot = i;
        /* Native chooses the next raw ID. Any retiring slot fences ALL new
         * creations until its cancellation/callback/TX suffix is complete. */
        else if (parent->services[i]->status.closing) valid = false;
    }
    valid = valid && slot != ESP_WIFI_NAN_MAX_SVC_SUPPORTED;
    if (valid) {
        ++service->references; /* Parent registry, distinct from queue context. */
        parent->services[slot] = service;
        service->queue = queue;
        service->deadline_us = now + duration;
        service->status.activated = true;
        parent->next_retry_us = 0;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (!valid) esp32_mquickjs_event_queue_release(queue);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_nan_discovery_close(esp32_mquickjs_wifi_nan_discovery_t *service)
{
    if (!service) return;
    portENTER_CRITICAL(&s_nan_session_lock);
    service->status.closing = true;
    service->status.ready = false;
    if (!service->status.activated) { nan_discovery_clear_secrets(service); service->status.retired = true; }
    if (service->parent) service->parent->next_retry_us = 0;
    portEXIT_CRITICAL(&s_nan_session_lock);
}

void esp32_mquickjs_wifi_nan_discovery_queue_closed(void *opaque)
{
    esp32_mquickjs_wifi_nan_discovery_t *service = opaque;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_event_queue_t *queue = service->queue;
    service->queue = NULL;
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (queue) esp32_mquickjs_event_queue_release(queue);
}

void esp32_mquickjs_wifi_nan_discovery_status(esp32_mquickjs_wifi_nan_discovery_t *service,
    esp32_mquickjs_wifi_nan_discovery_status_t *status)
{
    portENTER_CRITICAL(&s_nan_session_lock);
    *status = service->status;
    if (service->parent && service->parent->status.closing && !status->retired) { status->closing = true; status->ready = false; }
    portEXIT_CRITICAL(&s_nan_session_lock);
}

static bool nan_discovery_pending_locked(esp32_mquickjs_wifi_nan_session_t *s, int64_t now)
{
    bool pending = false;
    for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i) {
        esp32_mquickjs_wifi_nan_discovery_t *d = s->services[i];
        if (!d) continue;
        if (d->deadline_us && now >= d->deadline_us && !d->status.ready && !d->status.closing) {
            d->status.timed_out = d->status.closing = true;
            d->status.error = ESP_ERR_TIMEOUT;
        }
        if (!d->status.ready || d->status.closing) pending = true;
#if CONFIG_ESP_WIFI_NAN_PAIRING
        if (d->credentials_requested) pending = true;
#endif
    }
    return pending;
}

static void nan_discovery_detach(esp32_mquickjs_wifi_nan_discovery_t *d)
{
    esp32_mquickjs_wifi_nan_session_t *s = d->parent;
    portENTER_CRITICAL(&s_nan_session_lock);
    for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i) if (s->services[i] == d) s->services[i] = NULL;
    nan_discovery_clear_secrets(d);
    d->status.ready = d->status.creating = false;
    d->status.closing = d->status.retired = true;
    d->parent = NULL; /* Closed handles/queues retain only their own snapshots. */
    esp32_mquickjs_event_queue_t *queue = d->queue;
    if (queue && !esp32_mquickjs_event_queue_retain(queue)) queue = NULL;
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (queue) { esp32_mquickjs_event_queue_request_close(queue); esp32_mquickjs_event_queue_release(queue); }
    esp32_mquickjs_wifi_nan_session_release(s);
    esp32_mquickjs_wifi_nan_discovery_release(d); /* Registry; queue/Futures/JS remain independent. */
}

static void nan_discovery_retire_all(esp32_mquickjs_wifi_nan_session_t *s)
{
    for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i) {
        portENTER_CRITICAL(&s_nan_session_lock);
        esp32_mquickjs_wifi_nan_discovery_t *d = s->services[i];
        portEXIT_CRITICAL(&s_nan_session_lock);
        if (d) nan_discovery_detach(d);
    }
}

static void nan_discovery_worker(esp32_mquickjs_wifi_nan_session_t *s)
{
    esp32_mquickjs_wifi_nan_discovery_t *d = NULL;
    portENTER_CRITICAL(&s_nan_session_lock);
    (void)nan_discovery_pending_locked(s, esp_timer_get_time());
    /* Finish retiring services before letting native reuse any raw ID. */
    for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i)
        if (s->services[i] && s->services[i]->status.closing) { d = s->services[i]; break; }
    if (!d) for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i)
        if (s->services[i] && !s->services[i]->status.ready) { d = s->services[i]; break; }
#if CONFIG_ESP_WIFI_NAN_PAIRING
    bool credentials = false;
    if (!d) for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i)
        if (s->services[i] && s->services[i]->credentials_requested) { d = s->services[i]; credentials = true; break; }
    if (credentials) {
        uint8_t id = d->status.native_id;
        esp32_mquickjs_wifi_radio_operation_t token = s->operation;
        portEXIT_CRITICAL(&s_nan_session_lock);
        esp32_mquickjs_wifi_nan_credentials_t snapshot = {0};
        esp_err_t error = esp32_mquickjs_wifi_radio_nan_pairing_credentials(&token, id, &snapshot);
        portENTER_CRITICAL(&s_nan_session_lock);
        d->credentials = snapshot; d->credentials_error = error;
        ++d->credentials_revision; d->credentials_requested = false;
        portEXIT_CRITICAL(&s_nan_session_lock);
        return;
    }
#endif
    bool closing = d && d->status.closing;
    bool cancelled = d && d->status.cancelled;
    uint8_t id = d ? d->status.native_id : 0;
    if (d && !closing) { d->status.creating = true; s->creating = d; }
    esp32_mquickjs_wifi_radio_operation_t token = s->operation;
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (!d) return;
    esp_err_t error = ESP_OK;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    portENTER_CRITICAL(&s_nan_session_lock);
    bool pairing_busy = nan_pairing_service_busy_locked(s, d);
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (closing && pairing_busy) return;
#endif
    if (closing && id) error = esp32_mquickjs_wifi_radio_nan_service_close(&token, id, &cancelled);
    else if (!closing) error = esp32_mquickjs_wifi_radio_nan_service_start(&token,
        d->status.publish ? &d->config.publish : NULL, d->status.publish ? NULL : &d->config.subscribe, &id);
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_nan_session_lock);
    if (!closing) {
        s->creating = NULL;
        d->status.creating = false;
        if (!error && (!id || d->status.native_id != id)) error = ESP_ERR_INVALID_RESPONSE;
        if (!error && now >= d->deadline_us) {
            d->status.timed_out = d->status.closing = true;
            d->status.error = ESP_ERR_TIMEOUT;
        }
        if (error) {
            d->status.error = error;
            /* A failed create can have native allocations without an exposed
             * ID. Physical parent teardown retains every such buffer owner. */
            if (!s->status.error) { s->status.error = error; s->status.stage = "nan-service-create"; }
            nan_session_close_locked(s, false);
        }
        d->status.ready = !error && !d->status.closing && !s->status.closing;
        if (d->status.ready) d->deadline_us = 0;
    } else {
        d->status.cancelled = cancelled;
        d->status.cleanup_error = error;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (closing && !error) nan_discovery_detach(d);
}

static void nan_discovery_notice(esp32_mquickjs_wifi_nan_session_t *s,
    const esp32_mquickjs_wifi_nan_sdk_notice_t *notice)
{
    if (notice->kind == ESP32_MQUICKJS_NAN_SDK_SERVICE_TERMINATED) {
        portENTER_CRITICAL(&s_nan_session_lock);
        for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i) {
            esp32_mquickjs_wifi_nan_discovery_t *d = s->services[i];
            if (!d || !notice->service_id || d->status.native_id != notice->service_id) continue;
            d->status.closing = d->status.cancelled = true;
            d->status.ready = false;
            s->next_retry_us = 0;
        }
        portEXIT_CRITICAL(&s_nan_session_lock);
        return;
    }
    if (notice->kind == ESP32_MQUICKJS_NAN_SDK_DISCOVERY_NDP_DENIED) {
        if (notice->status) {
            portENTER_CRITICAL(&s_nan_session_lock);
            if (!s->status.error) { s->status.error = notice->status; s->status.stage = "nan-discovery-deny"; }
            nan_session_close_locked(s, false);
            portEXIT_CRITICAL(&s_nan_session_lock);
        }
        return;
    }
    if (notice->kind == ESP32_MQUICKJS_NAN_SDK_SERVICE_BOUND) {
        portENTER_CRITICAL(&s_nan_session_lock);
        esp32_mquickjs_wifi_nan_discovery_t *d = s->creating;
        bool valid = d && notice->service_id && !d->status.native_id &&
            notice->context == (d->status.publish ? 2U : 1U) && !notice->status;
        if (valid) d->status.native_id = notice->service_id;
        else {
            if (!s->status.error) { s->status.error = ESP_ERR_INVALID_RESPONSE; s->status.stage = "nan-service-bind"; }
            nan_session_close_locked(s, false);
        }
        portEXIT_CRITICAL(&s_nan_session_lock);
        return;
    }
    if (notice->kind != ESP32_MQUICKJS_NAN_SDK_EVENT || !notice->data) return;
    const uint8_t *ssi = NULL, *peer = NULL;
    uint8_t id = 0, peer_id = 0, kind = 0;
    size_t offset = 0, length = 0;
    if (notice->event_id == WIFI_EVENT_NAN_SVC_MATCH && notice->size >= sizeof(wifi_event_nan_svc_match_t)) {
        const wifi_event_nan_svc_match_t *e = notice->data;
        id = e->subscribe_id; peer_id = e->publish_id; peer = e->pub_if_mac; kind = 1;
        ssi = e->ssi; length = e->ssi_len; offset = offsetof(wifi_event_nan_svc_match_t, ssi);
    } else if (notice->event_id == WIFI_EVENT_NAN_REPLIED && notice->size >= sizeof(wifi_event_nan_replied_t)) {
        const wifi_event_nan_replied_t *e = notice->data;
        id = e->publish_id; peer_id = e->subscribe_id; peer = e->sub_if_mac; kind = 2;
        ssi = e->ssi; length = e->ssi_len; offset = offsetof(wifi_event_nan_replied_t, ssi);
    } else if (notice->event_id == WIFI_EVENT_NAN_RECEIVE && notice->size >= sizeof(wifi_event_nan_receive_t)) {
        const wifi_event_nan_receive_t *e = notice->data;
        id = e->inst_id; peer_id = e->peer_inst_id; peer = e->peer_if_mac; kind = 3;
        ssi = e->ssi; length = e->ssi_len; offset = offsetof(wifi_event_nan_receive_t, ssi);
    } else return;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_discovery_t *d = NULL;
    for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i)
        if (s->services[i] && s->services[i]->status.native_id == id && id) { d = s->services[i]; break; }
    esp32_mquickjs_event_queue_t *queue = NULL;
    if (d && !s->status.closing && !d->status.closing) {
        if (d->sequence == UINT32_MAX) {
            d->status.error = ESP_ERR_NO_MEM; d->status.closing = true; d->status.ready = false;
            s->next_retry_us = 0;
        }
        bool valid = length <= ESP_WIFI_MAX_FUP_SSI_LEN && offset <= notice->size && length <= notice->size - offset &&
            !d->event_busy && d->queue && d->references != UINT32_MAX && d->sequence != UINT32_MAX;
        if (valid && esp32_mquickjs_event_queue_retain(d->queue)) {
            queue = d->queue; ++d->references; d->event_busy = true; ++d->sequence;
        } else if (d->status.dropped != UINT32_MAX) ++d->status.dropped;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    if (!queue) return;
    esp32_mquickjs_wifi_nan_discovery_event_t *event = &d->scratch;
    memset(event, 0, sizeof(*event));
    event->identity = d->status.identity; event->sequence = d->sequence;
    event->native_id = id; event->peer_id = peer_id; event->kind = kind;
    memcpy(event->peer, peer, 6); event->ssi_len = length;
    if (length) memcpy(event->ssi, ssi, length);
    if (kind == 1) {
        const wifi_event_nan_svc_match_t *e = notice->data;
        event->ssi_version = e->ssi_version; event->datapath_required = e->datapath_reqd;
        event->security_required = e->security_reqd; event->further_discovery = e->fsd_reqd;
        event->gas = e->fsd_gas; event->ndpe = e->ndpe_support;
    }
    (void)esp32_mquickjs_event_queue_try_send_from_callback(queue, event);
    esp32_mquickjs_wireless_secure_zero(event, sizeof(*event));
    esp32_mquickjs_event_queue_release(queue);
    portENTER_CRITICAL(&s_nan_session_lock); d->event_busy = false; portEXIT_CRITICAL(&s_nan_session_lock);
    esp32_mquickjs_wifi_nan_discovery_release(d);
}
