/* Included after the pinned SDK host helpers. All submits are made by the
 * Radio mutation worker. No event-group waits, JS roots or implicit retries. */
static bool esp32qjs_nan_ndp_enabled(void)
{
    bool enabled = false;
    NAN_DATA_LOCK();
    for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i) {
        uint8_t id = s_nan_ctx.own_svc[i].svc_id;
        if (id && id != NAN_SVC_ID_PENDING && !esp32qjs_nan_is_discovery_only(id)) enabled = true;
    }
    NAN_DATA_UNLOCK();
    return enabled;
}

extern int nan_dp_alloc_ndp(void *ndl, uint8_t id, void **ndp);
extern void nan_dp_delete_peer(void *ndl);
extern void *nan_get_peer_svc_record(const uint8_t *peer, uint8_t id, void *service);
extern int __real_nan_datapath_send_req(wifi_nan_datapath_req_t *request, uint8_t *id, uint8_t *ipv6);
extern uint8_t s_ndp_id;

int __wrap_nan_datapath_send_req(wifi_nan_datapath_req_t *request, uint8_t *id, uint8_t *ipv6)
{
    uint32_t identity = esp32_mquickjs_wifi_nan_ndp_request_enter(request);
    if (!identity) return ESP_ERR_INVALID_STATE;
    uint8_t selected = 0;
    int result = esp32_mquickjs_wifi_nan_ndp_select_native_id(s_ndp_id, &selected);
    if (result == ESP_OK) {
        s_ndp_id = selected;
        result = __real_nan_datapath_send_req(request, id, ipv6);
    }
    esp32_mquickjs_wifi_nan_ndp_request_leave(identity, result);
    return result;
}

/* Both native allocation calls are retargeted, including the same-object
 * request/RX call sites that ld --wrap cannot intercept. Bind before the M1
 * builder and before any subsequent callback on the WiFi task. */
int esp32qjs_nan_ndp_alloc(void *ndl, uint8_t id, void **ndp)
{
    if (!ndl || !id || !ndp) return ESP_ERR_INVALID_ARG;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    /* NDL starts with its peer NMI in the pinned ABI. No new data handshake
     * may race the admitted pairing's replacement of this peer's keys. */
    if (esp32_mquickjs_wifi_nan_pairing_services(ndl, NULL, NULL)) return ESP_ERR_INVALID_STATE;
#endif
    /* Existing discovery-only publications retain their default-deny path;
     * it needs TX tracking, but never acquires a framework datapath slot. */
    if (!esp32qjs_nan_ndp_enabled()) return nan_dp_alloc_ndp(ndl, id, ndp);
    uint32_t identity = 0;
    esp_err_t error = esp32_mquickjs_wifi_nan_ndp_native_claim(ndl, id, &identity);
    if (error != ESP_OK) return error;
    int result = nan_dp_alloc_ndp(ndl, id, ndp);
    esp32_mquickjs_wifi_nan_ndp_native_bound(identity, result == 0);
    esp32_mquickjs_wifi_nan_ndp_status_t state;
    if (result == 0 && esp32_mquickjs_wifi_nan_ndp_status(identity, &state) && state.outgoing) {
        NAN_DATA_LOCK();
        struct ndl_info *host = nan_find_ndl_by_pub_id_and_peer(state.publisher_id, state.peer);
        bool bound = host && (host->ndp_id == 0 || host->ndp_id == id) &&
            host->own_role == ESP_WIFI_NDP_ROLE_INITIATOR;
        if (bound) host->ndp_id = id;
        NAN_DATA_UNLOCK();
        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_BOUND, state.service_id, id,
            bound ? ESP_OK : ESP_ERR_INVALID_RESPONSE, identity, state.peer);
        if (!bound) result = ESP_ERR_INVALID_RESPONSE;
    }
    return result;
}

void esp32qjs_nan_ndp_delete(void *ndl)
{
    if (!ndl) { nan_dp_delete_peer(NULL); return; }
    uint8_t peer[6], ndp_id = ((const uint8_t *)ndl)[9];
    memcpy(peer, ndl, sizeof(peer));
    uint32_t identity = esp32_mquickjs_wifi_nan_ndp_deleting(ndl);
    esp32_mquickjs_wifi_nan_ndp_status_t state;
    bool tracked = identity && esp32_mquickjs_wifi_nan_ndp_status(identity, &state);
    esp32_mquickjs_wifi_nan_timer_peer_delete(ndl, true);
    nan_dp_delete_peer(ndl);
    esp32_mquickjs_wifi_nan_timer_peer_delete(ndl, false);
    /* Native clears keys, queues and the current peer before this point.
     * Callback/timer tickets and the framework record remain independent. */
    NAN_DATA_LOCK();
    struct ndl_info *host = ndp_id ? nan_find_ndl(ndp_id, peer) : NULL;
    if (host) forced_memzero(host, sizeof(*host));
    esp_netif_t *netif = !nan_is_datapath_active() ? s_nan_ctx.nan_netif : NULL;
    NAN_DATA_UNLOCK();
    if (netif) esp_netif_action_disconnected(netif, WIFI_EVENT, WIFI_EVENT_NDP_TERMINATED, NULL);
    if (!tracked) return;
    esp32_mquickjs_wifi_nan_ndp_deleted(identity);
    esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_DELETED, state.service_id, state.ndp_id,
        ESP_OK, identity, state.peer);
}

void *esp32qjs_nan_ndp_peer_service(const uint8_t *peer, uint8_t id, void *service)
{
    /* The pinned service object stores its raw service ID at byte 4. All six
     * datapath lookups pass this object, including secure M1 retry builders. */
    if (!service || !esp32_mquickjs_wifi_nan_ndp_peer_service(peer, id, ((uint8_t *)service)[4])) return NULL;
    return nan_get_peer_svc_record(peer, id, service);
}

esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_request(uint8_t service_id,
    wifi_nan_datapath_req_t *request, uint32_t *identity)
{
    if (!service_id || !request || !identity || *identity || !request->pub_id ||
        (request->peer_mac[0] & 1) || MACADDR_EQUAL(request->peer_mac, null_mac)) return ESP_ERR_INVALID_ARG;
    if (s_usd_in_progress || !s_nan_data_lock || !nan_event_group ||
        esp32qjs_nan_is_discovery_only(service_id)) return ESP_ERR_INVALID_STATE;
    uint32_t caps = 0;
    NAN_DATA_LOCK();
    struct own_svc_info *own = nan_find_own_svc(service_id);
    struct peer_svc_info *peer = nan_find_peer_svc(service_id, request->pub_id, request->peer_mac);
    bool valid = own && peer && own->type == ESP_NAN_SUBSCRIBE && peer->type == ESP_NAN_PUBLISH;
    if (valid) caps = peer->device_caps;
    NAN_DATA_UNLOCK();
    if (!valid) return ESP_ERR_NOT_FOUND;
    ip_addr_t ipv6 = {0};
    if (caps & NAN_CAPS_NDPE_ATTR) {
        uint8_t mac[6];
        esp_err_t error = esp_wifi_get_mac(WIFI_IF_NAN, mac);
        if (error != ESP_OK) return error;
        esp_wifi_nan_get_ipv6_linklocal_from_mac(&ipv6.u_addr.ip6, mac);
    }
    NAN_DATA_LOCK();
    own = nan_find_own_svc(service_id);
    peer = nan_find_peer_svc(service_id, request->pub_id, request->peer_mac);
    if (!own || !peer || own->type != ESP_NAN_SUBSCRIBE || peer->type != ESP_NAN_PUBLISH ||
        peer->device_caps != caps || nan_find_ndl(0, request->peer_mac) || ndl_limit_reached()) {
        NAN_DATA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = esp32_mquickjs_wifi_nan_ndp_begin(service_id, request, identity);
    if (error != ESP_OK) { NAN_DATA_UNLOCK(); return error; }
    /* Open and secure requests both preclaim: an early confirm must never
     * depend on the requesting worker returning from ioctl first. */
    nan_record_new_ndl(0, request->pub_id, request->peer_mac, ESP_WIFI_NDP_ROLE_INITIATOR, caps);
    struct ndl_info *host = nan_find_ndl_by_pub_id_and_peer(request->pub_id, request->peer_mac);
    if (!host) error = ESP_ERR_NO_MEM;
#ifdef CONFIG_ESP_WIFI_NAN_SECURITY
    if (error == ESP_OK) error = nan_security_populate_initiator_ndl(host, own, peer, request->peer_mac);
#endif
    if (error != ESP_OK && host) forced_memzero(host, sizeof(*host));
    NAN_DATA_UNLOCK();
    uint8_t id = 0;
    if (error == ESP_OK) error = esp_nan_internal_datapath_req(request, &id, (uint8_t *)&ipv6.u_addr.ip6.addr[2]);
    esp32_mquickjs_wifi_nan_ndp_status_t state;
    if (esp32_mquickjs_wifi_nan_ndp_status(*identity, &state)) {
        if (state.submit_error) error = state.submit_error;
        if (error == ESP_OK && (!id || state.ndp_id != id || !state.host_bound)) error = ESP_ERR_INVALID_RESPONSE;
    } else error = ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_nan_ndp_submitted(*identity, error);
    return error;
}

esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_response(uint32_t identity, bool accept,
    const uint8_t *ssi, uint16_t length)
{
    if (!identity || length > ESP_WIFI_MAX_SVC_SSI_LEN || (length && !ssi)) return ESP_ERR_INVALID_ARG;
    if (!s_nan_data_lock || !nan_event_group || s_usd_in_progress) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_nan_ndp_status_t state;
    esp_err_t error = esp32_mquickjs_wifi_nan_ndp_mutation(identity, false, &state);
    if (error != ESP_OK) return error;
    NAN_DATA_LOCK();
    struct ndl_info *host = nan_find_ndl(state.ndp_id, state.peer);
    bool valid = host && host->publisher_id == state.service_id && host->own_role == ESP_WIFI_NDP_ROLE_RESPONDER;
#if CONFIG_ESP_WIFI_NAN_SECURITY
    struct own_svc_info *own = valid ? nan_find_own_svc(state.service_id) : NULL;
    if (accept && own && own->user_cfg.num_credentials &&
        (host->security_ctx.type != WIFI_NAN_SECURITY_ENCRYPTED || host->handshake_state != NAN_HANDSHAKE_M1_RCVD))
        valid = false;
#endif
    uint32_t caps = valid ? host->device_caps : 0;
    NAN_DATA_UNLOCK();
    if (!valid) error = ESP_ERR_INVALID_STATE;
    ip_addr_t ipv6 = {0};
    if (error == ESP_OK && (caps & NAN_CAPS_NDPE_ATTR)) {
        uint8_t mac[6];
        error = esp_wifi_get_mac(WIFI_IF_NAN, mac);
        if (error == ESP_OK) esp_wifi_nan_get_ipv6_linklocal_from_mac(&ipv6.u_addr.ip6, mac);
    }
    if (error == ESP_OK) {
        wifi_nan_datapath_resp_t response = {.accept = accept, .ndp_id = state.ndp_id,
            .ssi = (uint8_t *)ssi, .ssi_len = length};
        MACADDR_COPY(response.peer_mac, state.peer);
        error = esp_nan_internal_datapath_resp(&response, (uint8_t *)&ipv6.u_addr.ip6.addr[2]);
    }
    esp32_mquickjs_wifi_nan_ndp_mutation_done(identity, false, error);
    return error;
}

esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_end(uint32_t identity)
{
    if (!identity) return ESP_ERR_INVALID_ARG;
    if (!s_nan_data_lock || !nan_event_group || s_usd_in_progress) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_nan_ndp_status_t state;
    esp_err_t error = esp32_mquickjs_wifi_nan_ndp_mutation(identity, true, &state);
    if (error != ESP_OK) return error;
    wifi_nan_datapath_end_req_t end = {.ndp_id = state.ndp_id};
    MACADDR_COPY(end.peer_mac, state.peer);
    error = esp_nan_internal_datapath_end(&end);
    esp32_mquickjs_wifi_nan_ndp_mutation_done(identity, true, error);
    return error;
}

esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_release_native(uint32_t identity,
    esp32_mquickjs_wifi_nan_ndp_status_t *status)
{
    if (!status || !identity) return ESP_ERR_INVALID_ARG;
    if (!s_nan_data_lock || !nan_event_group || s_usd_in_progress) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_nan_ndp_status_t state;
    esp_err_t error = esp32_mquickjs_wifi_nan_ndp_retire_native(identity, false, &state);
    if (error != ESP_OK) return error;
    NAN_DATA_LOCK();
    /* Native deletion already clears established host rows. A submission
     * that never acquired a native NDL can still own its zero-ID preclaim. */
    struct ndl_info *host = state.ndp_id ? nan_find_ndl(state.ndp_id, state.peer) :
        nan_find_ndl_by_pub_id_and_peer(state.publisher_id, state.peer);
    if (host && (host->ndp_id != state.ndp_id || host->publisher_id != state.publisher_id ||
        host->own_role != (state.outgoing ? ESP_WIFI_NDP_ROLE_INITIATOR : ESP_WIFI_NDP_ROLE_RESPONDER)))
        error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK && host) forced_memzero(host, sizeof(*host));
    NAN_DATA_UNLOCK();
    if (error == ESP_OK) error = esp32_mquickjs_wifi_nan_ndp_retire_native(identity, true, status);
    return error;
}
